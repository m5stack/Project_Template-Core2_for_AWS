#include <unity.h>

#include <ctype.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_CRYPTO";

/* RFC 6979 A.2.5 P-256/SHA-256 vector. Verify uses only caller-provided data
 * and does not read or modify an ATECC data slot. */
static const uint8_t _verify_digest[ ATCA_KEY_SIZE ] = {
    0xAF, 0x2B, 0xDB, 0xE1, 0xAA, 0x9B, 0x6E, 0xC1,
    0xE2, 0xAD, 0xE1, 0xD6, 0x94, 0xF4, 0x1F, 0xC7,
    0x1A, 0x83, 0x1D, 0x02, 0x68, 0xE9, 0x89, 0x15,
    0x62, 0x11, 0x3D, 0x8A, 0x62, 0xAD, 0xD1, 0xBF,
};

static const uint8_t _verify_signature[ ATCA_SIG_SIZE ] = {
    0xEF, 0xD4, 0x8B, 0x2A, 0xAC, 0xB6, 0xA8, 0xFD,
    0x11, 0x40, 0xDD, 0x9C, 0xD4, 0x5E, 0x81, 0xD6,
    0x9D, 0x2C, 0x87, 0x7B, 0x56, 0xAA, 0xF9, 0x91,
    0xC3, 0x4D, 0x0E, 0xA8, 0x4E, 0xAF, 0x37, 0x16,
    0xF7, 0xCB, 0x1C, 0x94, 0x2D, 0x65, 0x7C, 0x41,
    0xD4, 0x36, 0xC7, 0xA1, 0xB6, 0xE2, 0x9F, 0x65,
    0xF3, 0xE9, 0x00, 0xDB, 0xB9, 0xAF, 0xF4, 0x06,
    0x4D, 0xC4, 0xAB, 0x2F, 0x84, 0x3A, 0xCD, 0xA8,
};

static const uint8_t _verify_public_key[ ATCA_PUB_KEY_SIZE ] = {
    0x60, 0xFE, 0xD4, 0xBA, 0x25, 0x5A, 0x9D, 0x31,
    0xC9, 0x61, 0xEB, 0x74, 0xC6, 0x35, 0x6D, 0x68,
    0xC0, 0x49, 0xB8, 0x92, 0x3B, 0x61, 0xFA, 0x6C,
    0xE6, 0x69, 0x62, 0x2E, 0x60, 0xF2, 0x9F, 0xB6,
    0x79, 0x03, 0xFE, 0x10, 0x08, 0xB8, 0xBC, 0x99,
    0xA4, 0x1A, 0xE9, 0xE9, 0x56, 0x28, 0xBC, 0x64,
    0xF2, 0xF1, 0xB2, 0x0C, 0x2D, 0x7E, 0x9F, 0x51,
    0x77, 0xA3, 0xC2, 0x94, 0xD4, 0x46, 0x22, 0x99,
};

/* The ATECC608 serial is a fixed 18-hex-character string over the shared bus. */
void test_crypto_serial_get( void )
{
    char serial[ CRYPTO_SERIAL_STR_SIZE ] = { 0 };
    esp_err_t err = core2foraws_crypto_serial_get( serial );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, err,
                               "Reading the secure-element serial should succeed" );

    size_t len = strlen( serial );
    ESP_LOGI( TAG, "serial (%u chars) = %s", ( unsigned )len, serial );
    TEST_ASSERT_EQUAL_UINT_MESSAGE( CRYPTO_SERIAL_STR_SIZE - 1, len,
                                    "Serial string should be fully populated" );
    for ( size_t i = 0; i < len; i++ )
    {
        TEST_ASSERT_TRUE_MESSAGE( isxdigit( ( int )serial[ i ] ),
                                  "Serial must be hexadecimal" );
    }
}

/* Info and Read commands do not write EEPROM. The ATECC608B datasheet lists
 * read endurance as unlimited. */
void test_crypto_device_info_read_only( void )
{
    uint8_t revision[ 4 ] = { 0 };
    ATCA_STATUS status = atcab_info( revision );
    TEST_ASSERT_EQUAL_MESSAGE( ATCA_SUCCESS, status,
                               "Reading device revision should succeed" );

    bool all_zero = true;
    bool all_ff = true;
    for ( size_t i = 0; i < sizeof( revision ); i++ )
    {
        all_zero = all_zero && revision[ i ] == 0x00;
        all_ff = all_ff && revision[ i ] == 0xFF;
    }

    ESP_LOGI( TAG, "revision = %02X%02X%02X%02X", revision[ 0 ],
              revision[ 1 ], revision[ 2 ], revision[ 3 ] );
    TEST_ASSERT_FALSE_MESSAGE( all_zero, "Revision must not be all zero" );
    TEST_ASSERT_FALSE_MESSAGE( all_ff, "Revision must not be all 0xFF" );
}

void test_crypto_config_and_lock_state_read_only( void )
{
    uint8_t config[ ATCA_ECC_CONFIG_SIZE ] = { 0 };
    ATCA_STATUS status = atcab_read_config_zone( config );
    TEST_ASSERT_EQUAL_MESSAGE( ATCA_SUCCESS, status,
                               "Reading the configuration zone should succeed" );

    bool has_nonzero = false;
    bool has_non_ff = false;
    for ( size_t i = 0; i < sizeof( config ); i++ )
    {
        has_nonzero = has_nonzero || config[ i ] != 0x00;
        has_non_ff = has_non_ff || config[ i ] != 0xFF;
    }
    TEST_ASSERT_TRUE_MESSAGE( has_nonzero,
                              "Configuration zone must not be all zero" );
    TEST_ASSERT_TRUE_MESSAGE( has_non_ff,
                              "Configuration zone must not be all 0xFF" );

    bool config_locked = false;
    bool data_locked = false;
    TEST_ASSERT_EQUAL_MESSAGE(
        ATCA_SUCCESS, atcab_is_locked( LOCK_ZONE_CONFIG, &config_locked ),
        "Reading configuration lock state should succeed" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ATCA_SUCCESS, atcab_is_locked( LOCK_ZONE_DATA, &data_locked ),
        "Reading data lock state should succeed" );

    uint16_t slot_lock_mask = 0;
    for ( uint16_t slot = 0; slot < 16; slot++ )
    {
        bool slot_locked = false;
        TEST_ASSERT_EQUAL_MESSAGE(
            ATCA_SUCCESS, atcab_is_slot_locked( slot, &slot_locked ),
            "Reading slot lock state should succeed" );
        if ( slot_locked )
        {
            slot_lock_mask |= ( uint16_t )( 1U << slot );
        }
    }

    ESP_LOGI( TAG, "config_locked=%d data_locked=%d slot_lock_mask=0x%04X",
              config_locked, data_locked, ( unsigned )slot_lock_mask );
    TEST_ASSERT_TRUE_MESSAGE( config_locked,
                              "Trust&Go configuration zone should be locked" );
    TEST_ASSERT_TRUE_MESSAGE( data_locked,
                              "Trust&Go data zone should be locked" );
}

/* Trust&Go parts ship with a provisioned key; skip if unavailable. */
void test_crypto_pubkey_get( void )
{
    char pubkey[ CRYPTO_PUB_KEY_SIZE ] = { 0 };
    esp_err_t err = core2foraws_crypto_pubkey_base64_get( pubkey );
    if ( err != ESP_OK )
    {
        TEST_IGNORE_MESSAGE( "Public key unavailable (part may be unprovisioned)" );
    }
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        0, strlen( pubkey ), "Public key string should be non-empty" );
}

void test_crypto_external_verify_read_only( void )
{
    bool verified = false;
    ATCA_STATUS status = atcab_verify_extern(
        _verify_digest, _verify_signature, _verify_public_key, &verified );
    TEST_ASSERT_EQUAL_MESSAGE( ATCA_SUCCESS, status,
                               "External-key verify should succeed" );
    TEST_ASSERT_TRUE_MESSAGE( verified,
                              "RFC 6979 signature should verify" );
}

static i2c_master_dev_handle_t _general_call;
static i2c_master_dev_handle_t _atecc;

/* An idle or sleeping ATECC608 NACKs its address; an awake one ACKs. */
static bool _atecc_awake( void )
{
    uint8_t response[ 4 ];
    return core2foraws_i2c_read( CORE2FORAWS_I2C_INTERNAL, _atecc,
                                 CORE2FORAWS_I2C_NO_REG, response,
                                 sizeof( response ) ) == ESP_OK;
}

/* Any SDA-low stretch >= 60 us wakes the chip; a general-call zero byte at
 * 100 kHz is the cleanest way to produce one outside cryptoauthlib. */
static void _unsolicited_wake( void )
{
    const uint8_t zero = 0;
    ( void )core2foraws_i2c_write( CORE2FORAWS_I2C_INTERNAL, _general_call,
                                   CORE2FORAWS_I2C_NO_REG, &zero, 1 );
    esp_rom_delay_us( 2000 );
}

static void _spin_until( int64_t target_us )
{
    while( target_us - esp_timer_get_time() > 20000 ) vTaskDelay( 1 );
    while( esp_timer_get_time() < target_us ) {}
}

/* Ordinary internal-bus traffic wakes the ATECC608 behind cryptoauthlib's
 * back; commands must still succeed when its watchdog is about to expire. */
void test_crypto_survives_unsolicited_wake( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_i2c_device_add(
        CORE2FORAWS_I2C_INTERNAL, 0x00, 100000, &_general_call ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_i2c_device_add(
        CORE2FORAWS_I2C_INTERNAL, 0x35, 100000, &_atecc ) );

    uint8_t sn[ ATCA_SERIAL_NUM_SIZE ];
    TEST_ASSERT_EQUAL( ATCA_SUCCESS, atcab_read_serial_number( sn ) );
    TEST_ASSERT_FALSE_MESSAGE( _atecc_awake(), "cryptoauthlib should idle the chip" );

    _unsolicited_wake();
    const int64_t woke_us = esp_timer_get_time();
    while( _atecc_awake() && esp_timer_get_time() - woke_us < 3000000 )
    {
        esp_rom_delay_us( 1000 );
    }
    const int watchdog_ms = ( int )( ( esp_timer_get_time() - woke_us ) / 1000 );
    ESP_LOGI( TAG, "watchdog after unsolicited wake: %d ms", watchdog_ms );
    TEST_ASSERT_TRUE_MESSAGE( watchdog_ms > 500 && watchdog_ms < 2500,
                              "Unsolicited wake should start the ~1.3 s watchdog" );

    int woke = 0;
    for( int round = 0; round < 10; round++ )
    {
        /* NACKed (harmlessly) when the chip is already idle */
        ( void )atcab_idle();
        float current;
        ( void )core2foraws_power_batt_current_get( &current );
        woke += _atecc_awake();
    }
    ESP_LOGI( TAG, "AXP192 reads that woke an idle ATECC608: %d of 10", woke );

    const int offsets_ms[] = { -35, -20, -10, -5, -3, -1 };
    int failures = 0;
    for( size_t i = 0; i < sizeof( offsets_ms ) / sizeof( offsets_ms[ 0 ] ); i++ )
    {
        ( void )atcab_idle();
        _unsolicited_wake();
        _spin_until( esp_timer_get_time() +
                     ( int64_t )( watchdog_ms + offsets_ms[ i ] ) * 1000 );
        ATCA_STATUS status = atcab_read_serial_number( sn );
        if( status != ATCA_SUCCESS )
        {
            failures++;
            ESP_LOGW( TAG, "command %d ms before watchdog expiry: status 0x%02X",
                      -offsets_ms[ i ], ( unsigned )( uint8_t )status );
        }
    }

    ( void )core2foraws_i2c_device_remove( _general_call );
    ( void )core2foraws_i2c_device_remove( _atecc );
    TEST_ASSERT_EQUAL_MESSAGE( 0, failures,
        "Commands must succeed even when bus traffic has already woken the chip" );
}
