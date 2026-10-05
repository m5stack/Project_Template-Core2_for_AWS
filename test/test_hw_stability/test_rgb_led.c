#include <unity.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

/* Drive both LED bars through the RMT path and confirm each API succeeds. */
void test_rgb_led_set_and_write( void )
{
    /* Keep it dim so the test is not blinding. */
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_brightness_set( 20 ) );

    for ( uint8_t i = 0; i < RGB_LED_NUMS; i++ )
    {
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, core2foraws_rgb_led_single_color_set( i, 0x0000FF ),
            "single_color_set should accept every LED index" );
    }
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );
    vTaskDelay( pdMS_TO_TICKS( 30 ) );

    TEST_ASSERT_EQUAL(
        ESP_OK, core2foraws_rgb_led_side_color_set( RGB_LED_SIDE_LEFT, 0x00FF00 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_side_color_set(
                                   RGB_LED_SIDE_RIGHT, 0xFF0000 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );
    vTaskDelay( pdMS_TO_TICKS( 30 ) );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_clear() );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );
}

/* Repeated deinit/init must not leak the RMT/DMA resources. */
void test_rgb_led_deinit_init_cycles( void )
{
    size_t before = heap_caps_get_free_size( MALLOC_CAP_INTERNAL );

    for ( int i = 0; i < 10; i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_deinit() );
        TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_rgb_led_init(),
                                   "RGB LED must re-initialize cleanly" );
    }

    /* Leave the bars off and re-initialized for later tests. */
    core2foraws_rgb_led_clear();
    core2foraws_rgb_led_write();

    size_t after = heap_caps_get_free_size( MALLOC_CAP_INTERNAL );
    if ( after < before )
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
            2048, before - after,
            "Repeated RGB LED init/deinit should not leak internal DRAM" );
    }
}
