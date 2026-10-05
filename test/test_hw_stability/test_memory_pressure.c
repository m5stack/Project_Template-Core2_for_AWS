/*
 * Tier 5 - Memory behaviour over time.
 *
 * Leak tests answer "did we give it back". These answer the two questions that
 * actually strand a long-running device: does the heap stay *usable* after
 * hundreds of lifecycle transitions (fragmentation), and does the BSP degrade
 * gracefully instead of crashing when an allocation cannot be served.
 */

#include <unity.h>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_MEMPRESS";

/* Contiguity loss tolerated across the lifecycle churn below. Fragmentation is
 * gradual, so the bar is deliberately tight: anything larger indicates the
 * allocator is not returning coalescable blocks. */
#define FRAG_TOLERANCE_BYTES ( 8 * 1024 )

#define EXHAUST_MAX_BLOCKS   128
#define EXHAUST_BLOCK_BYTES  ( 4 * 1024 )
/* Never consume the last of internal DRAM: the scheduler, logging and ISRs all
 * allocate from it, and starving them would crash the runner rather than test
 * the BSP. */
#define EXHAUST_RESERVE_BYTES ( 48 * 1024 )

/* Churn every subsystem that owns a large allocation, then confirm the largest
 * contiguous DMA block is still big enough for the display. Total free size can
 * look healthy while contiguity has quietly collapsed. */
void test_memory_fragmentation_under_churn( void )
{
    const size_t before = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
    const int cycles = 15;

    for ( int i = 0; i < cycles; i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );
        TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_init(),
                                   "Display must re-init on every churn cycle" );

        if ( core2foraws_sd_mount() == ESP_OK )
        {
            size_t wrote = 0;
            ( void )core2foraws_sd_write( "/frag.txt", TEST_SD_PAYLOAD, &wrote );
            ( void )core2foraws_sd_unmount();
        }

        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( true ) );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( false ) );

        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_deinit() );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_init() );
    }

    const size_t after = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
    const size_t needed = TEST_LCD_DRAW_BUF_BYTES;

    ESP_LOGI( TAG, "largest DMA block before=%u after=%u (%d cycles, need %u)",
              ( unsigned )before, ( unsigned )after, cycles, ( unsigned )needed );

    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        needed, after,
        "Fragmentation left too little contiguous DMA DRAM for the display" );

    if ( before > after )
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
            FRAG_TOLERANCE_BYTES, before - after,
            "Contiguous DMA DRAM degraded across lifecycle churn" );
    }
}

/* The peak-usage moment for a real application: display up, card mounted,
 * amplifier live, secure element working, LEDs driving. Anything that only fits
 * when subsystems are used one at a time will fail here. */
void test_memory_worst_case_concurrent_footprint( void )
{
    core2foraws_common_heap_stats_t peak = { 0 };

    const bool sd = ( core2foraws_sd_mount() == ESP_OK );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( true ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_brightness_set( 20 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_side_color_set( RGB_LED_SIDE_LEFT,
                                                                   0x101010 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rgb_led_write() );

    char serial[ CRYPTO_SERIAL_STR_SIZE ] = { 0 };
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_crypto_serial_get( serial ),
        "Secure element must work while every other subsystem is live" );

    if ( sd )
    {
        size_t wrote = 0;
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK, core2foraws_sd_write( "/peak.txt", TEST_SD_PAYLOAD, &wrote ),
            "SD writes must work at peak memory usage" );
    }

    if ( lvgl_port_lock( pdMS_TO_TICKS( 200 ) ) )
    {
        lv_obj_t *screen = lv_screen_active();
        if ( screen != NULL )
        {
            lv_obj_invalidate( screen );
            lv_refr_now( core2foraws_display_ptr );
        }
        lvgl_port_unlock();
    }

    core2foraws_common_heap_report( "PEAK_CONCURRENT", &peak );

    ( void )core2foraws_audio_speaker_enable( false );
    ( void )core2foraws_rgb_led_clear();
    ( void )core2foraws_rgb_led_write();
    if ( sd ) ( void )core2foraws_sd_unmount();

    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_DMA_PEAK_FLOOR_BYTES, peak.dma_free,
        "Not enough DMA DRAM headroom with every subsystem active at once" );
}

/* Squeeze internal DRAM until a draw-buffer allocation cannot be served, then
 * confirm the BSP reports the shortage, keeps the small-allocation paths
 * working, and fully recovers once the pressure is released. */
void test_memory_graceful_under_exhaustion( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );

    void *blocks[ EXHAUST_MAX_BLOCKS ];
    int held = 0;

    while ( held < EXHAUST_MAX_BLOCKS &&
            heap_caps_get_free_size( MALLOC_CAP_INTERNAL ) >
                ( EXHAUST_RESERVE_BYTES + EXHAUST_BLOCK_BYTES ) )
    {
        void *p = heap_caps_malloc( EXHAUST_BLOCK_BYTES, MALLOC_CAP_INTERNAL );
        if ( p == NULL )
        {
            break;
        }
        blocks[ held++ ] = p;
    }

    const size_t squeezed = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
    const esp_err_t starved_init = core2foraws_display_init();

    /* Small, non-DMA work must still succeed while the big pool is empty. */
    float volts = 0.0f;
    const esp_err_t pmu_err = core2foraws_power_batt_volts_get( &volts );

    if ( starved_init == ESP_OK )
    {
        ( void )core2foraws_display_deinit();
    }
    for ( int i = 0; i < held; i++ )
    {
        heap_caps_free( blocks[ i ] );
    }

    const esp_err_t restored = core2foraws_display_init();

    ESP_LOGI( TAG, "held=%d blocks squeezed_largest=%u starved_init=%s restore=%s",
              held, ( unsigned )squeezed, esp_err_to_name( starved_init ),
              esp_err_to_name( restored ) );

    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(
        0, held, "Test setup failed to create memory pressure" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, pmu_err,
        "Small I2C reads must keep working while the heap is under pressure" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, restored,
        "Display must recover once memory pressure is released" );
    TEST_ASSERT_NOT_NULL( core2foraws_display_ptr );
}
