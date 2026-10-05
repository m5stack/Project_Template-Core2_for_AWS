/*
 * BSP tuning regression checks: datasheet waits, bounded bus waits, combined
 * IMU sample, whole-frame RGB transfers, PSRAM SD staging and boot overlap.
 */

#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_TUNING";

#define TUNING_SD_PAYLOAD_BYTES ( 32 * 1024 )
#define TUNING_SD_PATH          "/tuning.txt"

/* Every datasheet minimum wait must survive vTaskDelay returning a tick early. */
void test_tuning_delay_macro_meets_minimum( void )
{
    const uint32_t waits_ms[] = { 1, 10, 15, 100 };
    for( size_t i = 0; i < sizeof( waits_ms ) / sizeof( waits_ms[ 0 ] ); i++ )
    {
        int64_t shortest_us = INT64_MAX;
        for( int round = 0; round < 10; round++ )
        {
            const int64_t start = esp_timer_get_time();
            vTaskDelay( CORE2FORAWS_DELAY_MS_TO_TICKS( waits_ms[ i ] ) );
            const int64_t elapsed = esp_timer_get_time() - start;
            if( elapsed < shortest_us ) shortest_us = elapsed;
        }
        ESP_LOGI( TAG, "delay %lu ms: shortest %lld us", ( unsigned long ) waits_ms[ i ],
                  shortest_us );
        TEST_ASSERT_GREATER_OR_EQUAL_INT( ( int ) waits_ms[ i ] * 1000, ( int ) shortest_us );
    }
}

void test_tuning_boot_init_duration( void )
{
    const int init_ms = ( int )( g_test_bsp_init_us / 1000 );
    ESP_LOGI( TAG, "core2foraws_init took %d ms", init_ms );
    /* LCD/touch reset (100 ms) and boot (300 ms) are the floor. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT( 400, init_ms );
}

void test_tuning_motion_burst_sample( void )
{
    for( int i = 0; i < 20; i++ )
    {
        float ax, ay, az, gx, gy, gz;
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_motion_accel_gyro_get(
                                       &ax, &ay, &az, &gx, &gy, &gz ) );
        const float mag = sqrtf( ax * ax + ay * ay + az * az );
        TEST_ASSERT_TRUE( isfinite( mag ) && isfinite( gx ) && isfinite( gy ) &&
                          isfinite( gz ) );
        TEST_ASSERT_TRUE_MESSAGE( mag > 0.5f && mag < 1.5f,
                                  "Burst accel magnitude should be about 1 g" );
        TEST_ASSERT_TRUE_MESSAGE( fabsf( gx ) < 300.0f && fabsf( gy ) < 300.0f &&
                                      fabsf( gz ) < 300.0f,
                                  "Stationary burst gyro rates should be small" );
        if( i == 0 )
        {
            ESP_LOGI( TAG, "burst: |a|=%.3f g gyro=%.1f %.1f %.1f dps", mag, gx, gy, gz );
        }
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG,
                       core2foraws_motion_accel_gyro_get( NULL, NULL, NULL,
                                                          NULL, NULL, NULL ) );
}

static SemaphoreHandle_t _hog_started;

static void _i2c_hog_task( void *arg )
{
    const uint32_t hold_ms = ( uint32_t )( uintptr_t ) arg;
    /* Unity asserts must stay on the test task; report failure by not
       signalling, which the test observes as a timeout. */
    if( core2foraws_i2c_lock( CORE2FORAWS_I2C_INTERNAL ) == ESP_OK )
    {
        xSemaphoreGive( _hog_started );
        vTaskDelay( pdMS_TO_TICKS( hold_ms ) );
        core2foraws_i2c_unlock( CORE2FORAWS_I2C_INTERNAL );
        xSemaphoreGive( _hog_started );
    }
    vTaskDelete( NULL );
}

/* A busy internal bus must cost a latency-sensitive caller (the LVGL touch
 * read) only its chosen timeout. */
void test_tuning_bounded_i2c_wait( void )
{
    _hog_started = xSemaphoreCreateCounting( 2, 0 );
    TEST_ASSERT_NOT_NULL( _hog_started );
    TEST_ASSERT_EQUAL( pdPASS, xTaskCreate( _i2c_hog_task, "i2cHog", 3072,
                                            ( void * )( uintptr_t ) 500, 5, NULL ) );
    TEST_ASSERT_EQUAL( pdTRUE, xSemaphoreTake( _hog_started, pdMS_TO_TICKS( 1000 ) ) );

    int64_t start = esp_timer_get_time();
    esp_err_t err = core2foraws_i2c_lock_timeout( CORE2FORAWS_I2C_INTERNAL, 20 );
    int64_t lock_us = esp_timer_get_time() - start;
    TEST_ASSERT_EQUAL( ESP_ERR_TIMEOUT, err );

    TEST_ASSERT_EQUAL( pdTRUE, xSemaphoreTake( _hog_started, pdMS_TO_TICKS( 2000 ) ) );
    vSemaphoreDelete( _hog_started );

    ESP_LOGI( TAG, "busy bus: lock_timeout(20) gave up after %lld us", lock_us );
    TEST_ASSERT_GREATER_OR_EQUAL_INT( 20 * 1000, ( int ) lock_us );
    TEST_ASSERT_LESS_THAN_INT( 40 * 1000, ( int ) lock_us );
}

/* Writes are asynchronous and the encoder supplies the latch, so a write
 * returns without a CPU busy-wait and back-to-back frames stay valid. */
void test_tuning_rgb_back_to_back_frames( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_init() );
    int64_t worst_us = 0;
    for( int i = 0; i < 100; i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_side_color_set(
                                       i & 1 ? RGB_LED_SIDE_LEFT : RGB_LED_SIDE_RIGHT,
                                       ( uint32_t ) i * 0x020202 ) );
        const int64_t start = esp_timer_get_time();
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );
        const int64_t elapsed = esp_timer_get_time() - start;
        if( elapsed > worst_us ) worst_us = elapsed;
    }
    ESP_LOGI( TAG, "rgb write worst %lld us over 100 back-to-back frames", worst_us );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_clear() );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );
    /* A frame is 240 bits at 1.2 us plus the 80 us latch, about 370 us. */
    TEST_ASSERT_LESS_THAN_INT( 2000, ( int ) worst_us );
}

static int64_t _sd_write_read_us( char *buffer, const char *label )
{
    memset( buffer, 'A' + ( label[ 0 ] % 26 ), TUNING_SD_PAYLOAD_BYTES - 1 );
    buffer[ TUNING_SD_PAYLOAD_BYTES - 1 ] = '\0';

    size_t wrote = 0;
    const int64_t start = esp_timer_get_time();
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_write( TUNING_SD_PATH, buffer, &wrote ) );
    const int64_t write_us = esp_timer_get_time() - start;
    TEST_ASSERT_EQUAL( TUNING_SD_PAYLOAD_BYTES - 1, wrote );

    const char expected = buffer[ 0 ];
    memset( buffer, 0, TUNING_SD_PAYLOAD_BYTES );
    const int64_t read_start = esp_timer_get_time();
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_sd_read( TUNING_SD_PATH, buffer,
                                                    TUNING_SD_PAYLOAD_BYTES ) );
    const int64_t read_us = esp_timer_get_time() - read_start;
    TEST_ASSERT_EQUAL( TUNING_SD_PAYLOAD_BYTES - 1, strlen( buffer ) );
    TEST_ASSERT_EQUAL_CHAR( expected, buffer[ 0 ] );
    TEST_ASSERT_EQUAL_CHAR( expected, buffer[ TUNING_SD_PAYLOAD_BYTES - 2 ] );

    ESP_LOGI( TAG, "%s buffer: write %lld KB/s, read %lld KB/s", label,
              ( int64_t ) TUNING_SD_PAYLOAD_BYTES * 1000 / write_us,
              ( int64_t ) TUNING_SD_PAYLOAD_BYTES * 1000 / read_us );
    return write_us;
}

void test_tuning_sd_psram_buffer_throughput( void )
{
    test_sd_require_mount();

    char *internal = heap_caps_malloc( TUNING_SD_PAYLOAD_BYTES,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA );
    char *psram = heap_caps_malloc( TUNING_SD_PAYLOAD_BYTES, MALLOC_CAP_SPIRAM );
    if( internal == NULL || psram == NULL )
    {
        free( internal );
        free( psram );
        core2foraws_sd_unmount();
        TEST_IGNORE_MESSAGE( "Could not allocate 32 KB internal and PSRAM test buffers" );
    }

    /* Warm up the FAT allocation so both timed passes overwrite in place. */
    ( void ) _sd_write_read_us( internal, "warm-up" );
    const int64_t internal_us = _sd_write_read_us( internal, "internal" );
    const int64_t psram_us = _sd_write_read_us( psram, "PSRAM" );

    free( internal );
    free( psram );
    core2foraws_sd_unmount();

    /* Guards the documented claim that PSRAM buffers are not a slow path. */
    TEST_ASSERT_LESS_THAN_INT_MESSAGE( ( int )( internal_us * 2 ), ( int ) psram_us,
        "PSRAM-sourced SD writes should be close to internal-buffer speed" );
}
