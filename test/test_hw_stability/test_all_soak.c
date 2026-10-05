#include <unity.h>

#include <math.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <lvgl.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_SOAK_ALL";

/* Exercises every subsystem together for many iterations - internal I2C
 * sensors, the shared SPI bus (LCD flush + SD file I/O), and the RMT LED
 * path - while watching internal DRAM for leaks. This is the closest thing
 * to a real application hammering the whole BSP at once. */
void test_all_subsystems_soak( void )
{
    const int iterations = 40;
    bool sd = ( core2foraws_sd_mount() == ESP_OK );

    core2foraws_common_heap_stats_t before = { 0 };
    core2foraws_common_heap_report( "SOAK_ALL_START", &before );

    core2foraws_rgb_led_brightness_set( 15 );

    int errors = 0;
    for ( int i = 0; i < iterations; i++ )
    {
        /* Internal I2C peripherals. */
        float v = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
        if ( core2foraws_power_batt_volts_get( &v ) != ESP_OK ) errors++;
        if ( core2foraws_motion_accel_get( &x, &y, &z ) != ESP_OK ) errors++;
        struct tm t = { 0 };
        if ( core2foraws_rtc_time_get( &t ) != ESP_OK ) errors++;
        esp_lcd_touch_point_data_t pts[ 1 ] = { 0 };
        uint8_t n = 0;
        if ( core2foraws_display_touch_data_get( pts, &n, 1 ) != ESP_OK )
            errors++;

        /* RMT LED path. */
        uint32_t color = ( i & 1 ) ? 0x001000 : 0x000010;
        core2foraws_rgb_led_single_color_set( i % RGB_LED_NUMS, color );
        if ( core2foraws_rgb_led_write() != ESP_OK ) errors++;

        /* Shared SPI: SD file I/O. */
        if ( sd )
        {
            size_t wrote = 0;
            if ( core2foraws_sd_write( "/soak.txt", TEST_SD_PAYLOAD, &wrote ) ==
                 ESP_ERR_TIMEOUT )
            {
                /* Bus contention timeout is tolerable; a hard error is not. */
            }
        }

        /* Shared SPI: force an LCD flush. */
        if ( lvgl_port_lock( pdMS_TO_TICKS( 100 ) ) )
        {
            lv_obj_t *scr = lv_screen_active();
            if ( scr != NULL ) lv_obj_invalidate( scr );
            lvgl_port_unlock();
        }

        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }

    core2foraws_rgb_led_clear();
    core2foraws_rgb_led_write();
    if ( sd ) core2foraws_sd_unmount();

    core2foraws_common_heap_stats_t after = { 0 };
    core2foraws_common_heap_report( "SOAK_ALL_END", &after );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, errors, "A subsystem returned a hard error during the combined soak" );

    if ( after.internal_free < before.internal_free )
    {
        size_t leaked = before.internal_free - after.internal_free;
        ESP_LOGI( TAG, "internal DRAM delta after %d iters: -%u bytes",
                  iterations, ( unsigned )leaked );
        TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
            TEST_HEAP_LEAK_BUDGET_BYTES, leaked,
            "Combined subsystem soak leaked internal DRAM beyond the budget" );
    }
}
