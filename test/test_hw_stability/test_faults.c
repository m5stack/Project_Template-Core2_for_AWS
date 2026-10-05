/*
 * Tier 3 - Fault injection and error paths.
 *
 * Everything here exercises the paths that only run when something has already
 * gone wrong: invalid arguments, absent hardware, unmounted media, and a bus
 * transaction that fails mid-flight. These are the paths that ship untested and
 * surface in the field as a hang or a reset, so they are asserted to fail
 * *predictably* rather than to succeed.
 */

#include <unity.h>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "core2foraws_i2c.h"
#include "test_hw.h"

static const char *TAG = "TEST_FAULTS";

/* An address with no device on the internal bus. Used to force a real NACK. */
#define FAULT_ABSENT_I2C_ADDR 0x2A

/* Records the observed error so a wrong-but-nonzero code is still visible in
 * the log, while failing the test only when the call wrongly reports success. */
#define ASSERT_REJECTS( call, what )                                           \
    do                                                                         \
    {                                                                          \
        esp_err_t _err = ( call );                                             \
        ESP_LOGI( TAG, "%-46s -> %s", what, esp_err_to_name( _err ) );         \
        TEST_ASSERT_NOT_EQUAL_MESSAGE( ESP_OK, _err,                           \
                                       what " must reject invalid input" );    \
    } while ( 0 )

/* Every getter that takes an out-pointer must reject NULL instead of
 * dereferencing it. A crash here is the bug, not the test. */
void test_fault_null_output_pointers( void )
{
    ASSERT_REJECTS( core2foraws_power_batt_volts_get( NULL ),
                    "power_batt_volts_get(NULL)" );
    ASSERT_REJECTS( core2foraws_power_batt_current_get( NULL ),
                    "power_batt_current_get(NULL)" );
    ASSERT_REJECTS( core2foraws_power_charging_get( NULL ),
                    "power_charging_get(NULL)" );
    ASSERT_REJECTS( core2foraws_power_plugged_get( NULL ),
                    "power_plugged_get(NULL)" );
    ASSERT_REJECTS( core2foraws_power_rail_state_get( POWER_RAIL_DCDC3, NULL ),
                    "power_rail_state_get(NULL)" );
    ASSERT_REJECTS( core2foraws_power_rail_mv_get( POWER_RAIL_DCDC3, NULL ),
                    "power_rail_mv_get(NULL)" );

    ASSERT_REJECTS( core2foraws_motion_temperature_get( NULL ),
                    "motion_temperature_get(NULL)" );

    float axis = 0.0f;
    ASSERT_REJECTS( core2foraws_motion_accel_get( NULL, &axis, &axis ),
                    "motion_accel_get(NULL,,)" );
    ASSERT_REJECTS( core2foraws_motion_accel_get( &axis, NULL, &axis ),
                    "motion_accel_get(,NULL,)" );
    ASSERT_REJECTS( core2foraws_motion_gyro_get( &axis, &axis, NULL ),
                    "motion_gyro_get(,,NULL)" );

    ASSERT_REJECTS( core2foraws_rtc_time_get( NULL ), "rtc_time_get(NULL)" );
    ASSERT_REJECTS( core2foraws_rtc_utc_time_get( NULL ),
                    "rtc_utc_time_get(NULL)" );

    ASSERT_REJECTS( core2foraws_display_get_touch_handle( NULL ),
                    "display_get_touch_handle(NULL)" );

    bool level = false;
    ASSERT_REJECTS( core2foraws_expports_digital_read( PORT_B_ADC_PIN, NULL ),
                    "expports_digital_read(NULL)" );
    (void)level;
}

/* Out-of-range scalars must be refused at the API boundary rather than being
 * clamped silently or written through to the hardware. */
void test_fault_out_of_range_arguments( void )
{
    ASSERT_REJECTS( core2foraws_rgb_led_single_color_set( RGB_LED_NUMS, 0x101010 ),
                    "rgb_led_single_color_set(out of range)" );
    ASSERT_REJECTS( core2foraws_rgb_led_brightness_set( 101 ),
                    "rgb_led_brightness_set(101)" );
    ASSERT_REJECTS( core2foraws_rgb_led_side_color_set(
                        ( rgb_led_side_type_t )9, 0x101010 ),
                    "rgb_led_side_color_set(bad side)" );

    bool rail_state = false;
    ASSERT_REJECTS(
        core2foraws_power_rail_state_get( POWER_RAIL_COUNT, &rail_state ),
        "power_rail_state_get(bad rail)" );

    ASSERT_REJECTS( core2foraws_button_register_callback(
                        ( enum core2foraws_button_btns )42, PRESS, NULL ),
                    "button_register_callback(bad button)" );

    ASSERT_REJECTS(
        core2foraws_motion_accel_range_set( ( motion_accel_range_t )77 ),
        "motion_accel_range_set(bad range)" );
    ASSERT_REJECTS( core2foraws_motion_gyro_range_set( ( motion_gyro_range_t )77 ),
                    "motion_gyro_range_set(bad range)" );

    /* Leave the sensor in the documented default range for later tests. */
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_motion_accel_range_set( MOTION_ACCEL_RANGE_8G ) );
}

/* Storage APIs used before the card is mounted must report state, not fault. */
void test_fault_storage_before_mount( void )
{
    ( void )core2foraws_sd_unmount();

    char buffer[ 32 ] = { 0 };
    size_t wrote = 0;

    ASSERT_REJECTS( core2foraws_sd_read( TEST_SD_FILE_PATH, buffer,
                                         sizeof( buffer ) ),
                    "sd_read(before mount)" );
    ASSERT_REJECTS( core2foraws_sd_write( TEST_SD_FILE_PATH, "x", &wrote ),
                    "sd_write(before mount)" );
    ASSERT_REJECTS( core2foraws_sd_unmount(), "sd_unmount(not mounted)" );
}

/* Reading a file that does not exist must fail cleanly and leave the mount
 * usable, since a missing config/asset file is a normal first-boot condition. */
void test_fault_missing_file_read( void )
{
    test_sd_require_mount();

    char buffer[ 64 ] = { 0 };
    ASSERT_REJECTS(
        core2foraws_sd_read( "/definitely_absent.txt", buffer, sizeof( buffer ) ),
        "sd_read(missing file)" );

    size_t wrote = 0;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_sd_write( TEST_SD_FILE_PATH, TEST_SD_PAYLOAD, &wrote ),
        "The mount must stay usable after a failed read" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_unmount() );
}

/* A NACK from an absent device must not poison the shared internal I2C bus.
 * This is the highest-value fault case on this board: every sensor, the PMU,
 * touch, and the secure element share one bus, so a single bad transaction that
 * left the peripheral wedged would take the whole device down. */
void test_fault_i2c_absent_device_recovery( void )
{
    char serial_before[ CRYPTO_SERIAL_STR_SIZE ] = { 0 };
    char serial_after[ CRYPTO_SERIAL_STR_SIZE ] = { 0 };
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_crypto_serial_get( serial_before ),
                               "Baseline secure-element read must succeed" );

    i2c_master_dev_handle_t ghost = NULL;
    esp_err_t add_err = core2foraws_i2c_device_add(
        CORE2FORAWS_I2C_INTERNAL, FAULT_ABSENT_I2C_ADDR, 100000, &ghost );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, add_err,
                               "Registering an address must not require a device" );

    uint8_t scratch[ 4 ] = { 0 };
    for ( int i = 0; i < 5; i++ )
    {
        esp_err_t read_err = core2foraws_i2c_read(
            CORE2FORAWS_I2C_INTERNAL, ghost,
            CORE2FORAWS_I2C_NO_REG, scratch, sizeof( scratch ) );
        TEST_ASSERT_NOT_EQUAL_MESSAGE(
            ESP_OK, read_err,
            "Reading an absent device must fail rather than return stale data" );
    }

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_i2c_device_remove( ghost ) );

    /* The real assertion: every other device on the bus still works, and the
     * secure element returns the identical constant it did before the faults. */
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_crypto_serial_get( serial_after ),
                               "Secure element must respond after bus faults" );
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        serial_before, serial_after,
        "Bus faults must not corrupt subsequent transactions" );

    float volts = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
    struct tm now = { 0 };
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_power_batt_volts_get( &volts ),
                               "PMU must respond after bus faults" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_motion_accel_get( &x, &y, &z ),
                               "IMU must respond after bus faults" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_rtc_time_get( &now ),
                               "RTC must respond after bus faults" );

    ESP_LOGI( TAG, "bus healthy after 5 forced NACKs (serial %s)", serial_after );
}

/* Repeated teardown must be idempotent. Double-deinit is what happens on an
 * error path that unwinds twice, and it must not double-free. */
void test_fault_repeated_teardown( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_deinit(),
                               "display_deinit must be idempotent" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_init(),
                               "display must re-initialize after double deinit" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_deinit() );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_rgb_led_deinit(),
                               "rgb_led_deinit must be idempotent" );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_init() );

    /* Audio disable when already disabled must be a no-op, not an error path
     * that leaves the amplifier powered. */
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( false ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( false ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_mic_enable( false ) );
}
