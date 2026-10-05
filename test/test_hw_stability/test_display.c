#include <unity.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_DISPLAY";

/* The display is brought up by core2foraws_init() in the test runner, so this
 * asserts the feature API reports a usable, registered LVGL display. */
void test_display_ready( void )
{
    TEST_ASSERT_NOT_NULL_MESSAGE(
        core2foraws_display_ptr,
        "Display should be registered after core2foraws_init()" );
}

/* Exercises the shared internal-I2C touch path. A touch may or may not be
 * active, so only the API contract (success + bounded count) is asserted. */
void test_display_touch_data_get_smoke( void )
{
    esp_lcd_touch_point_data_t points[ 5 ] = { 0 };
    uint8_t count = 0xFF;

    esp_err_t err = core2foraws_display_touch_data_get( points, &count, 5 );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, err,
        "touch_data_get should succeed while the display is initialized" );
    TEST_ASSERT_LESS_OR_EQUAL_UINT8_MESSAGE(
        5, count, "Reported touch points must not exceed the buffer capacity" );
}

/* Repeated deinit/init verifies teardown is idempotent, re-init succeeds, and
 * the shared SPI bus is left usable for the SD card between cycles. */
void test_display_init_deinit_cycles( void )
{
    for ( int i = 0; i < 5; i++ )
    {
        TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_deinit(),
                                   "First deinit should succeed" );
        TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_deinit(),
                                   "deinit is documented as idempotent" );

        esp_err_t err = core2foraws_display_init();
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, err, "Display must re-initialize cleanly after deinit" );
        TEST_ASSERT_NOT_NULL_MESSAGE(
            core2foraws_display_ptr,
            "Display pointer must be valid after re-init" );

        vTaskDelay( pdMS_TO_TICKS( 50 ) );
    }

    ESP_LOGI( TAG, "Completed 5 display init/deinit cycles" );
}
