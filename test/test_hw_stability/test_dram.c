#include <unity.h>

#include <sdkconfig.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_DRAM";

#define TEST_DRAM_MAX_RESERVATIONS 48
#define TEST_TASK_STACK_HEADROOM_BYTES 1024

/* Regression bars for the since-boot heap low-water marks, not functional
 * minima: a draw buffer needs only 12,800 contiguous bytes. The organic low
 * water measured with the radio stack brought up is 85,887 (internal) and
 * 80,479 (DMA), so these sit roughly 25% below to absorb run-to-run drift.
 * Lowering one to make a run green is how a real memory regression escapes;
 * re-measure and justify instead. */
#define TEST_INTERNAL_MINIMUM_FREE_BYTES ( 64 * 1024 )
#define TEST_DMA_MINIMUM_FREE_BYTES      ( 56 * 1024 )
#define TEST_DMA_LARGEST_BLOCK_BYTES     ( 96 * 1024 )

/* Reports every heap pool the BSP allocates from and confirms the DRAM
 * strategy that keeps the display safe: Wi-Fi/LWIP buffers can spill to PSRAM
 * (CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP), leaving contiguous internal
 * DMA DRAM for the LCD draw buffers. BLE controller memory, by contrast, is
 * internal-only, so this is the pool under pressure once radios are up. */
void test_dram_feature_budget_report( void )
{
    core2foraws_common_heap_stats_t stats = { 0 };
    core2foraws_common_heap_report( "DRAM_BUDGET", &stats );

    ESP_LOGI( TAG,
              "LCD draw buffers need 2 x %u = %u bytes of *contiguous* DMA DRAM",
              ( unsigned )TEST_LCD_DRAW_BUF_BYTES,
              ( unsigned )( TEST_LCD_DRAW_BUF_BYTES * 2 ) );
    ESP_LOGI( TAG,
              "internal_free=%u internal_min=%u dma_free=%u dma_min=%u "
              "dma_largest=%u spiram_free=%u spiram_min=%u",
              ( unsigned )stats.internal_free,
              ( unsigned )stats.internal_minimum_free,
              ( unsigned )stats.dma_free,
              ( unsigned )stats.dma_minimum_free,
              ( unsigned )stats.dma_largest_block,
              ( unsigned )stats.spiram_free,
              ( unsigned )stats.spiram_minimum_free );

    /* PSRAM must exist, otherwise Wi-Fi/LWIP fall back to internal DRAM and
     * compete with the display's DMA buffers. */
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        512 * 1024, stats.spiram_free,
        "PSRAM must be available to keep Wi-Fi/LWIP out of internal DRAM" );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_INTERNAL_MINIMUM_FREE_BYTES, stats.internal_minimum_free,
        "Internal DRAM low-water mark fell below the validated budget" );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_DMA_MINIMUM_FREE_BYTES, stats.dma_minimum_free,
        "DMA DRAM low-water mark fell below the validated budget" );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_DMA_LARGEST_BLOCK_BYTES, stats.dma_largest_block,
        "Largest DMA block fell below the validated contiguous budget" );
}

/* Emulates the internal-DMA-DRAM footprint of BLE + Wi-Fi (which the automated
 * suite does not bring up) by reserving DMA-capable DRAM, then proves the
 * display:
 *   1. still allocates its buffers under tight-but-sufficient pressure,
 *   2. fails with ESP_ERR_NO_MEM (never crashes) when DMA DRAM is exhausted,
 *   3. recovers once the pressure is released.
 * All reservations are freed before any assertion so a failure cannot leak. */
void test_dram_display_realloc_under_pressure( void )
{
    const size_t needed = TEST_LCD_DRAW_BUF_BYTES;

    /* Free the live draw buffers so measurements reflect true availability. */
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );

    size_t largest0 = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
    size_t avail0 = heap_caps_get_free_size( MALLOC_CAP_DMA );
    bool baseline_ok = ( largest0 >= needed ) && ( avail0 >= needed * 2 );

    /* ── Phase A: tight but sufficient. Reserve one big contiguous block,
     * leaving only ~2 buffers + 16 KB of headroom for the display. ── */
    void *tight = NULL;
    if ( largest0 > needed * 2 + 16384 )
    {
        tight = heap_caps_malloc( largest0 - ( needed * 2 + 16384 ),
                                  MALLOC_CAP_DMA );
    }
    esp_err_t tight_init = core2foraws_display_init();
    esp_err_t tight_touch = ESP_FAIL;
    if ( tight_init == ESP_OK )
    {
        esp_lcd_touch_point_data_t points[ 1 ] = { 0 };
        uint8_t count = 0;
        tight_touch = core2foraws_display_touch_data_get( points, &count, 1 );
        core2foraws_display_deinit();
    }
    if ( tight != NULL )
    {
        heap_caps_free( tight );
    }

    /* ── Phase B: exhaust contiguous DMA DRAM so no draw buffer can fit. ── */
    void *reservations[ TEST_DRAM_MAX_RESERVATIONS ];
    int reserved = 0;
    while ( reserved < TEST_DRAM_MAX_RESERVATIONS &&
            heap_caps_get_largest_free_block( MALLOC_CAP_DMA ) >= needed )
    {
        size_t block = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
        /* Leave a sub-buffer hole in this region so its largest free block
         * drops below one draw buffer. */
        size_t take = ( block > needed ) ? block - ( needed - 4096 ) : block;
        void *p = heap_caps_malloc( take, MALLOC_CAP_DMA );
        if ( p == NULL )
        {
            p = heap_caps_malloc( block, MALLOC_CAP_DMA );
        }
        if ( p == NULL )
        {
            break;
        }
        reservations[ reserved++ ] = p;
    }

    size_t starved_largest = heap_caps_get_largest_free_block( MALLOC_CAP_DMA );
    esp_err_t starved_init = core2foraws_display_init();
    if ( starved_init == ESP_OK )
    {
        /* Unexpectedly fit; undo so the restore path is clean. */
        core2foraws_display_deinit();
    }

    for ( int i = 0; i < reserved; i++ )
    {
        heap_caps_free( reservations[ i ] );
    }

    /* ── Restore the display for the remaining suite. ── */
    esp_err_t restore_init = core2foraws_display_init();

    ESP_LOGI( TAG,
              "needed/buf=%u largest0=%u avail0=%u starved_largest=%u "
              "reserved=%d",
              ( unsigned )needed, ( unsigned )largest0, ( unsigned )avail0,
              ( unsigned )starved_largest, reserved );
    ESP_LOGI( TAG,
              "tight_init=%s tight_touch=%s starved_init=%s restore=%s",
              esp_err_to_name( tight_init ), esp_err_to_name( tight_touch ),
              esp_err_to_name( starved_init ), esp_err_to_name( restore_init ) );

    TEST_ASSERT_TRUE_MESSAGE(
        baseline_ok,
        "Not enough contiguous DMA DRAM for the LCD draw buffers at baseline" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, tight_init,
        "Display should allocate under tight-but-sufficient DMA pressure" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, tight_touch,
        "Display/touch should work after a tight allocation" );
    TEST_ASSERT_TRUE_MESSAGE(
        starved_largest < needed,
        "Test setup failed to create real DMA pressure" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_NO_MEM, starved_init,
        "Display must fail with ESP_ERR_NO_MEM (not crash) when DMA DRAM is "
        "exhausted" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, restore_init,
        "Display must recover once DMA pressure is released" );
    TEST_ASSERT_NOT_NULL( core2foraws_display_ptr );
}

/* Measure persistent BSP tasks after the suite has exercised display flushes,
 * touch polling, callbacks, shared buses, and subsystem soak loops. */
void test_task_stack_watermarks( void )
{
    TaskHandle_t button_task = xTaskGetHandle( "buttonPress" );
    TaskHandle_t lvgl_task = xTaskGetHandle( "taskLVGL" );
    size_t button_free = 0;
    size_t lvgl_free = 0;

    TEST_ASSERT_NOT_NULL_MESSAGE( button_task,
                                  "buttonPress task must be running" );
    TEST_ASSERT_NOT_NULL_MESSAGE( lvgl_task, "taskLVGL must be running" );
    TEST_ASSERT_EQUAL(
        ESP_OK, core2foraws_common_task_stack_watermark(
                    "STACK_BUDGET", button_task, &button_free ) );
    TEST_ASSERT_EQUAL(
        ESP_OK, core2foraws_common_task_stack_watermark(
                    "STACK_BUDGET", lvgl_task, &lvgl_free ) );

    ESP_LOGI( TAG, "button_free=%u lvgl_free=%u",
              ( unsigned )button_free, ( unsigned )lvgl_free );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_TASK_STACK_HEADROOM_BYTES, button_free,
        "buttonPress needs at least 1 KiB of measured stack headroom" );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_TASK_STACK_HEADROOM_BYTES, lvgl_free,
        "LVGL task needs at least 1 KiB of measured stack headroom" );
}
