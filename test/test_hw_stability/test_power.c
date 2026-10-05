#include <unity.h>

#include <math.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_POWER";

/* Battery/USB telemetry must return plausible, finite values. */
void test_power_readings( void )
{
    float volts = 0.0f;
    float m_amps = 0.0f;
    bool charging = false;
    bool plugged = false;

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_batt_volts_get( &volts ) );
    TEST_ASSERT_TRUE_MESSAGE( isfinite( volts ) && volts > 2.0f && volts < 5.0f,
                              "Battery voltage should be a plausible Li-ion level" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_batt_current_get( &m_amps ) );
    TEST_ASSERT_TRUE_MESSAGE( isfinite( m_amps ),
                              "Battery current should be finite" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_charging_get( &charging ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_plugged_get( &plugged ) );

    ESP_LOGI( TAG, "batt=%.3fV cur=%.1fmA charging=%d plugged=%d", volts, m_amps,
              charging, plugged );
}

/* Backlight PWM must accept the full 0-255 range and settle back visible. */
void test_power_backlight_sweep( void )
{
    const uint8_t levels[] = { 0, 64, 128, 192, 255 };
    for ( size_t i = 0; i < sizeof( levels ); i++ )
    {
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, core2foraws_power_backlight_set( levels[ i ] ),
            "Backlight set should accept 0-255" );
        vTaskDelay( pdMS_TO_TICKS( 20 ) );
    }
    /* Leave the panel at a usable brightness. */
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_backlight_set( 80 ) );
}

/* Every rail must report its state. LDO1 is the always-on RTC rail, so it must
 * read as enabled and refuse to be switched or adjusted with NOT_SUPPORTED
 * rather than being indistinguishable from a caller error. */
void test_power_rails_read( void )
{
    for ( power_rail_t rail = POWER_RAIL_DCDC1; rail < POWER_RAIL_COUNT; rail++ )
    {
        bool enabled = false;
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, core2foraws_power_rail_state_get( rail, &enabled ),
            "rail_state_get should succeed for every defined rail" );

        uint16_t mv = 0;
        esp_err_t v_err = core2foraws_power_rail_mv_get( rail, &mv );
        TEST_ASSERT_TRUE_MESSAGE(
            v_err == ESP_OK || v_err == ESP_ERR_NOT_SUPPORTED,
            "rail_mv_get must succeed or report NOT_SUPPORTED" );

        ESP_LOGI( TAG, "rail %d: enabled=%d mv=%u%s", rail, enabled,
                  ( unsigned )mv,
                  v_err == ESP_ERR_NOT_SUPPORTED ? " (not adjustable)" : "" );
    }

    bool ldo1 = false;
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_power_rail_state_get( POWER_RAIL_LDO1,
                                                         &ldo1 ) );
    TEST_ASSERT_TRUE_MESSAGE( ldo1,
                              "LDO1 is always on and must report enabled" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NOT_SUPPORTED,
        core2foraws_power_rail_state_set( POWER_RAIL_LDO1, false ),
        "Switching the always-on LDO1 must report NOT_SUPPORTED" );

    /* An out-of-range rail is a caller error, which stays INVALID_ARG. */
    bool dummy = false;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_power_rail_state_get( ( power_rail_t )99, &dummy ),
        "An unknown rail must still be INVALID_ARG" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_power_rail_state_get( POWER_RAIL_DCDC1, NULL ),
        "A NULL out-pointer must still be INVALID_ARG" );
}

/* Vibration motor and green LED are safe to pulse briefly. */
void test_power_actuators_toggle( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_led_enable( true ) );
    vTaskDelay( pdMS_TO_TICKS( 40 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_led_enable( false ) );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_vibration_enable( true ) );
    vTaskDelay( pdMS_TO_TICKS( 40 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_vibration_enable( false ) );
}

/* Rapid repeated reads must stay stable (no I2C desync, no drift to nonsense). */
void test_power_repeated_reads_stable( void )
{
    float min_v = 99.0f, max_v = 0.0f;
    for ( int i = 0; i < 60; i++ )
    {
        float volts = 0.0f;
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_power_batt_volts_get( &volts ) );
        TEST_ASSERT_TRUE( isfinite( volts ) && volts > 2.0f && volts < 5.0f );
        if ( volts < min_v ) min_v = volts;
        if ( volts > max_v ) max_v = volts;
    }
    TEST_ASSERT_TRUE_MESSAGE(
        ( max_v - min_v ) < 0.5f,
        "Battery voltage should not swing wildly across back-to-back reads" );
}
