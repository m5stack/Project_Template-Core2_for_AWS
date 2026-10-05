#include <unity.h>

#include <string.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_SD";

bool test_sd_require_mount( void )
{
    esp_err_t err = core2foraws_sd_mount();
    if ( err != ESP_OK )
    {
        ESP_LOGW( TAG, "SD mount failed (%s) - skipping card test",
                  esp_err_to_name( err ) );
        TEST_IGNORE_MESSAGE(
            "No SD card mounted; insert a FAT-formatted card to run SD tests" );
    }
    return true;
}

/* Full mount -> write -> read-back -> unmount round trip through the feature
 * API, validating data integrity and the reported byte counts. */
void test_sd_mount_write_read_unmount( void )
{
    test_sd_require_mount();

    size_t wrote = 0;
    esp_err_t err =
        core2foraws_sd_write( TEST_SD_FILE_PATH, TEST_SD_PAYLOAD, &wrote );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, err, "SD write should succeed" );
    TEST_ASSERT_EQUAL_UINT_MESSAGE(
        strlen( TEST_SD_PAYLOAD ), wrote,
        "Reported write length should match the payload length" );

    char buffer[ 128 ] = { 0 };
    err = core2foraws_sd_read( TEST_SD_FILE_PATH, buffer, sizeof( buffer ) );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, err, "SD read should succeed" );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        TEST_SD_PAYLOAD, buffer,
        "Data read back must match the data written" );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_sd_unmount(),
                               "SD unmount should succeed after mount" );
}

/* Unmounting without an active mount must be rejected, not crash or hang on
 * the shared bus. */
void test_sd_unmount_requires_mount( void )
{
    esp_err_t err = core2foraws_sd_unmount();
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_STATE, err,
        "Unmount without a prior mount should return ESP_ERR_INVALID_STATE" );
}
