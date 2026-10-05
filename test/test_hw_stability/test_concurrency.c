#include <unity.h>

#include <stdatomic.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <lvgl.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_CONCURRENCY";

/* Iteration budgets sized to force sustained contention on the shared SPI bus
 * without making the on-device run excessively long. */
#define SD_WORKER_ITERATIONS      120
#define DISPLAY_WORKER_ITERATIONS 400
#define WORKER_JOIN_TIMEOUT_MS    30000

typedef struct
{
    atomic_bool done;
    atomic_int sd_ok;
    atomic_int sd_timeout;
    atomic_int sd_error;
    atomic_int sd_mismatch;
    atomic_int display_flushes;
} concurrency_ctx_t;

/* Continuously drives full-screen LVGL flushes so the display owns the SPI bus
 * in bursts while the SD worker competes for it. */
static void _display_worker( void *arg )
{
    concurrency_ctx_t *ctx = ( concurrency_ctx_t * )arg;
    lv_color_t colors[ 2 ] = { lv_color_hex( 0x102030 ),
                               lv_color_hex( 0x304050 ) };

    for ( int i = 0; i < DISPLAY_WORKER_ITERATIONS; i++ )
    {
        if ( lvgl_port_lock( pdMS_TO_TICKS( 200 ) ) )
        {
            lv_obj_t *screen = lv_screen_active();
            if ( screen != NULL )
            {
                lv_obj_set_style_bg_color( screen, colors[ i & 1 ], LV_PART_MAIN );
                lv_obj_invalidate( screen );
                atomic_fetch_add( &ctx->display_flushes, 1 );
            }
            lvgl_port_unlock();
        }
        vTaskDelay( pdMS_TO_TICKS( 5 ) );
    }

    atomic_store( &ctx->done, true );
    vTaskDelete( NULL );
}

/* Hammers SD writes and read-backs so file I/O interleaves with display DMA;
 * classifies each result to prove the bus lock serializes rather than corrupts
 * or deadlocks. */
static void _sd_worker( void *arg )
{
    concurrency_ctx_t *ctx = ( concurrency_ctx_t * )arg;
    char buffer[ 128 ];

    for ( int i = 0; i < SD_WORKER_ITERATIONS; i++ )
    {
        size_t wrote = 0;
        esp_err_t err =
            core2foraws_sd_write( TEST_SD_FILE_PATH, TEST_SD_PAYLOAD, &wrote );
        if ( err == ESP_ERR_TIMEOUT )
        {
            atomic_fetch_add( &ctx->sd_timeout, 1 );
            continue;
        }
        if ( err != ESP_OK )
        {
            atomic_fetch_add( &ctx->sd_error, 1 );
            continue;
        }

        memset( buffer, 0, sizeof( buffer ) );
        err = core2foraws_sd_read( TEST_SD_FILE_PATH, buffer, sizeof( buffer ) );
        if ( err == ESP_ERR_TIMEOUT )
        {
            atomic_fetch_add( &ctx->sd_timeout, 1 );
            continue;
        }
        if ( err != ESP_OK )
        {
            atomic_fetch_add( &ctx->sd_error, 1 );
            continue;
        }

        if ( strcmp( buffer, TEST_SD_PAYLOAD ) != 0 )
        {
            atomic_fetch_add( &ctx->sd_mismatch, 1 );
        }
        else
        {
            atomic_fetch_add( &ctx->sd_ok, 1 );
        }
    }

    while ( !atomic_load( &ctx->done ) )
    {
        vTaskDelay( pdMS_TO_TICKS( 10 ) );
    }
    vTaskDelete( NULL );
}

/* Runs the LCD and SD workers on both cores at once and asserts the shared-SPI
 * lock kept every transfer intact with no deadlock. */
void test_display_sd_concurrent( void )
{
    TEST_ASSERT_NOT_NULL_MESSAGE(
        core2foraws_display_ptr,
        "Display must be initialized before the concurrency test" );
    test_sd_require_mount();

    concurrency_ctx_t ctx = { 0 };
    atomic_store( &ctx.done, false );

    BaseType_t d = xTaskCreatePinnedToCore( _display_worker, "disp_worker", 4096,
                                            &ctx, 4, NULL, 1 );
    BaseType_t s = xTaskCreatePinnedToCore( _sd_worker, "sd_worker", 4096, &ctx,
                                            4, NULL, 0 );
    TEST_ASSERT_EQUAL_MESSAGE( pdPASS, d, "Failed to start display worker" );
    TEST_ASSERT_EQUAL_MESSAGE( pdPASS, s, "Failed to start SD worker" );

    int waited_ms = 0;
    while ( !atomic_load( &ctx.done ) && waited_ms < WORKER_JOIN_TIMEOUT_MS )
    {
        vTaskDelay( pdMS_TO_TICKS( 50 ) );
        waited_ms += 50;
    }
    /* Let the SD worker observe done and self-delete. */
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    ESP_LOGI( TAG,
              "concurrency results: sd_ok=%d timeout=%d error=%d mismatch=%d "
              "flushes=%d",
              atomic_load( &ctx.sd_ok ), atomic_load( &ctx.sd_timeout ),
              atomic_load( &ctx.sd_error ), atomic_load( &ctx.sd_mismatch ),
              atomic_load( &ctx.display_flushes ) );

    TEST_ASSERT_TRUE_MESSAGE(
        atomic_load( &ctx.done ),
        "Display worker did not finish - shared SPI bus likely deadlocked" );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, atomic_load( &ctx.sd_mismatch ),
        "SD data was corrupted under concurrent display flushes" );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, atomic_load( &ctx.sd_error ),
        "SD I/O returned a hard error under concurrent display flushes" );
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(
        0, atomic_load( &ctx.sd_ok ),
        "No SD operations completed successfully under contention" );
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(
        0, atomic_load( &ctx.display_flushes ),
        "No display flushes were issued under contention" );

    core2foraws_sd_unmount();
}

/* Long-run soak: repeated display re-init plus SD mount/write/unmount cycles,
 * bounded by a heap-leak budget on free internal DRAM. */
void test_display_sd_soak_no_leak( void )
{
    const int cycles = 25;

    core2foraws_common_heap_stats_t before = { 0 };
    core2foraws_common_heap_report( "SOAK_START", &before );

    for ( int i = 0; i < cycles; i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_init() );

        if ( core2foraws_sd_mount() == ESP_OK )
        {
            size_t wrote = 0;
            core2foraws_sd_write( TEST_SD_FILE_PATH, TEST_SD_PAYLOAD, &wrote );
            core2foraws_sd_unmount();
        }

        vTaskDelay( pdMS_TO_TICKS( 20 ) );
    }

    core2foraws_common_heap_stats_t after = { 0 };
    core2foraws_common_heap_report( "SOAK_END", &after );

    TEST_ASSERT_NOT_NULL_MESSAGE(
        core2foraws_display_ptr,
        "Display must remain initialized after the soak loop" );

    if ( after.internal_free < before.internal_free )
    {
        size_t leaked = before.internal_free - after.internal_free;
        ESP_LOGI( TAG, "internal DRAM delta after %d cycles: -%u bytes", cycles,
                  ( unsigned )leaked );
        TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
            TEST_HEAP_LEAK_BUDGET_BYTES, leaked,
            "Free internal DRAM dropped beyond the leak budget over the soak" );
    }
}
