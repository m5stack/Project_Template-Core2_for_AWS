/*
 * Tier 3 - Storage durability and integrity.
 *
 * A consumer device loses power at arbitrary moments and must come back with
 * its configuration and user data intact. Without a switchable supply this
 * suite cannot cut power mid-write, so it proves the next strongest property:
 * that data is actually flushed to the medium rather than sitting in a cache,
 * verified by tearing the mount down and reading it back through a fresh one.
 */

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_STORAGE";

#define STORAGE_NVS_NAMESPACE "hwtest"
#define STORAGE_NVS_KEY       "durable"
#define STORAGE_LARGE_BYTES   2048
#define STORAGE_REWRITE_CYCLES 12

static esp_err_t _nvs_ready( void )
{
    esp_err_t err = nvs_flash_init();
    if ( err == ESP_ERR_NVS_NO_FREE_PAGES ||
         err == ESP_ERR_NVS_NEW_VERSION_FOUND )
    {
        ESP_LOGW( TAG, "NVS needs erase (%s); reformatting", esp_err_to_name( err ) );
        TEST_ASSERT_EQUAL( ESP_OK, nvs_flash_erase() );
        err = nvs_flash_init();
    }
    return err;
}

/* Configuration written by an application must survive a close/reopen cycle.
 * This is the path every credential, calibration value and user setting takes. */
void test_storage_nvs_commit_durability( void )
{
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, _nvs_ready(), "NVS must initialize" );

    const uint32_t written = 0xC0FFEE01;
    nvs_handle_t handle;

    TEST_ASSERT_EQUAL( ESP_OK, nvs_open( STORAGE_NVS_NAMESPACE, NVS_READWRITE,
                                         &handle ) );
    TEST_ASSERT_EQUAL( ESP_OK, nvs_set_u32( handle, STORAGE_NVS_KEY, written ) );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, nvs_commit( handle ),
                               "Commit must succeed for the value to be durable" );
    nvs_close( handle );

    uint32_t read_back = 0;
    TEST_ASSERT_EQUAL( ESP_OK, nvs_open( STORAGE_NVS_NAMESPACE, NVS_READONLY,
                                         &handle ) );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, nvs_get_u32( handle, STORAGE_NVS_KEY, &read_back ),
        "A committed key must be readable through a fresh handle" );
    nvs_close( handle );

    TEST_ASSERT_EQUAL_HEX32_MESSAGE( written, read_back,
                                     "NVS returned a different value than committed" );

    TEST_ASSERT_EQUAL( ESP_OK, nvs_open( STORAGE_NVS_NAMESPACE, NVS_READWRITE,
                                         &handle ) );
    ( void )nvs_erase_key( handle, STORAGE_NVS_KEY );
    ( void )nvs_commit( handle );
    nvs_close( handle );
}

/* Reading a key that was never written must report NOT_FOUND. First boot and
 * post-factory-reset both depend on this being distinguishable from a zero. */
void test_storage_nvs_missing_key( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, _nvs_ready() );

    nvs_handle_t handle;
    esp_err_t open_err = nvs_open( STORAGE_NVS_NAMESPACE, NVS_READWRITE, &handle );
    TEST_ASSERT_EQUAL( ESP_OK, open_err );

    uint32_t value = 0;
    esp_err_t err = nvs_get_u32( handle, "never_written", &value );
    nvs_close( handle );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NVS_NOT_FOUND, err,
        "An absent key must report NOT_FOUND, not a default value" );
}

/* The core durability property available without a power cut: data written
 * through the feature API must survive unmounting and remounting the card. */
void test_storage_sd_survives_remount( void )
{
    test_sd_require_mount();

    size_t wrote = 0;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_sd_write( TEST_SD_FILE_PATH, TEST_SD_PAYLOAD, &wrote ),
        "SD write must succeed" );
    TEST_ASSERT_EQUAL_UINT( strlen( TEST_SD_PAYLOAD ), wrote );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_sd_unmount(),
                               "Unmount must flush and release the card" );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_sd_mount(),
                               "Card must remount after a clean unmount" );

    char buffer[ 128 ] = { 0 };
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_read( TEST_SD_FILE_PATH, buffer,
                                                    sizeof( buffer ) ) );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        TEST_SD_PAYLOAD, buffer,
        "Data must survive the unmount/remount cycle byte for byte" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_unmount() );
}

/* A payload larger than the driver's internal chunking must round-trip exactly.
 * Chunk-boundary corruption is invisible to short-string tests. */
void test_storage_sd_large_payload_integrity( void )
{
    test_sd_require_mount();

    char *payload = malloc( STORAGE_LARGE_BYTES + 1 );
    char *readback = malloc( STORAGE_LARGE_BYTES + 1 );
    TEST_ASSERT_NOT_NULL( payload );
    TEST_ASSERT_NOT_NULL( readback );

    /* Printable, non-repeating pattern so any offset error shows up as a
     * mismatch rather than aliasing onto identical bytes. */
    for ( int i = 0; i < STORAGE_LARGE_BYTES; i++ )
    {
        payload[ i ] = ( char )( 'A' + ( i % 26 ) );
    }
    payload[ STORAGE_LARGE_BYTES ] = '\0';
    memset( readback, 0, STORAGE_LARGE_BYTES + 1 );

    size_t wrote = 0;
    esp_err_t write_err = core2foraws_sd_write( "/large.txt", payload, &wrote );
    esp_err_t read_err = core2foraws_sd_read( "/large.txt", readback,
                                              STORAGE_LARGE_BYTES + 1 );

    const bool match = ( strcmp( payload, readback ) == 0 );
    const size_t reported = wrote;

    free( payload );
    free( readback );
    ( void )core2foraws_sd_unmount();

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, write_err, "Large SD write must succeed" );
    TEST_ASSERT_EQUAL_UINT_MESSAGE( STORAGE_LARGE_BYTES, reported,
                                    "Reported write length must match the payload" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, read_err, "Large SD read must succeed" );
    TEST_ASSERT_TRUE_MESSAGE( match,
                              "Large payload did not round-trip byte for byte" );
}

/* Rewriting the same path repeatedly must replace the contents, not append or
 * accumulate. Log files and rolling state depend on truncation semantics. */
void test_storage_sd_repeated_rewrite( void )
{
    test_sd_require_mount();

    char expected[ 64 ];
    char buffer[ 128 ];

    for ( int i = 0; i < STORAGE_REWRITE_CYCLES; i++ )
    {
        snprintf( expected, sizeof( expected ), "rewrite-cycle-%02d", i );

        size_t wrote = 0;
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, core2foraws_sd_write( "/rewrite.txt", expected, &wrote ),
            "Every rewrite must succeed" );

        memset( buffer, 0, sizeof( buffer ) );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_read( "/rewrite.txt", buffer,
                                                        sizeof( buffer ) ) );
        TEST_ASSERT_EQUAL_STRING_MESSAGE(
            expected, buffer,
            "Rewriting a file must replace its contents exactly" );
    }

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_unmount() );
}
