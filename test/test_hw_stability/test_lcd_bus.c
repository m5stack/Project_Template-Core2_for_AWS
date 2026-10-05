#include <unity.h>

#include <stdatomic.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lcd_touch.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <lvgl.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_LCD_BUS";

#define LCD_EXPECTED_H_RES 320
#define LCD_EXPECTED_V_RES 240

/* The registered LVGL display must advertise the panel's native geometry. */
void test_display_resolution( void )
{
    lv_display_t *disp = core2foraws_display_ptr;
    TEST_ASSERT_NOT_NULL_MESSAGE( disp, "Display pointer must be valid" );

    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        LCD_EXPECTED_H_RES, lv_display_get_horizontal_resolution( disp ),
        "Horizontal resolution should be 320" );
    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        LCD_EXPECTED_V_RES, lv_display_get_vertical_resolution( disp ),
        "Vertical resolution should be 240" );
}

/* The panel takes big-endian RGB565. LVGL's default format must match so layers
 * render in the byte order the BSP flushes without swapping. */
void test_display_color_format( void )
{
    lv_display_t *disp = core2foraws_display_ptr;
    TEST_ASSERT_NOT_NULL_MESSAGE( disp, "Display pointer must be valid" );

    TEST_ASSERT_EQUAL_MESSAGE(
        LV_COLOR_FORMAT_RGB565_SWAPPED, lv_display_get_color_format( disp ),
        "Display must render big-endian RGB565" );
    TEST_ASSERT_EQUAL_MESSAGE(
        LV_COLOR_FORMAT_RGB565_SWAPPED, LV_COLOR_FORMAT_DEFAULT,
        "sdkconfig must set CONFIG_LV_COLOR_FORMAT_RGB565_SWAPPED" );
}

/* The touch read API must reject malformed arguments before touching the bus. */
void test_display_touch_arg_validation( void )
{
    esp_lcd_touch_point_data_t points[ 1 ] = { 0 };
    uint8_t count = 0;

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_display_touch_data_get( NULL, &count, 1 ),
        "NULL points buffer should be rejected" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_display_touch_data_get( points, NULL, 1 ),
        "NULL point_count should be rejected" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_display_touch_data_get( points, &count, 0 ),
        "Zero capacity should be rejected" );
}

/* The (deprecated) touch-handle accessor returns a live handle when the display
 * is up and validates its output argument. */
void test_display_get_touch_handle_api( void )
{
    esp_lcd_touch_handle_t handle = NULL;

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG, core2foraws_display_get_touch_handle( NULL ),
        "NULL out-pointer should be rejected" );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_display_get_touch_handle( &handle ),
        "Touch handle should be available while the display is initialized" );
    TEST_ASSERT_NOT_NULL_MESSAGE( handle,
                                  "Returned touch handle must be non-NULL" );
}

/* After deinit the LCD APIs must report an uninitialized state rather than
 * touching freed drivers; the display is restored for later tests. */
void test_display_state_after_deinit( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_display_deinit() );
    TEST_ASSERT_NULL_MESSAGE( core2foraws_display_ptr,
                              "Display pointer must be NULL after deinit" );

    esp_lcd_touch_point_data_t points[ 1 ] = { 0 };
    uint8_t count = 0;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_STATE,
        core2foraws_display_touch_data_get( points, &count, 1 ),
        "touch_data_get must report INVALID_STATE after deinit" );

    esp_lcd_touch_handle_t handle = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_STATE, core2foraws_display_get_touch_handle( &handle ),
        "get_touch_handle must report INVALID_STATE after deinit" );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_display_init(),
                               "Display must re-initialize for later tests" );
    TEST_ASSERT_NOT_NULL( core2foraws_display_ptr );
    vTaskDelay( pdMS_TO_TICKS( 50 ) );
}

/* Sustained mix of full-screen and partial-area flushes exercises SPI DMA
 * throughput and confirms the panel stays responsive afterward. */
void test_display_flush_stress( void )
{
    lv_display_t *disp = core2foraws_display_ptr;
    TEST_ASSERT_NOT_NULL( disp );

    const int iterations = 60;
    int completed = 0;
    int64_t start = esp_timer_get_time();

    for ( int i = 0; i < iterations; i++ )
    {
        if ( lvgl_port_lock( pdMS_TO_TICKS( 200 ) ) )
        {
            lv_obj_t *screen = lv_screen_active();
            if ( screen != NULL )
            {
                if ( i % 3 == 0 )
                {
                    lv_obj_invalidate( screen );
                }
                else
                {
                    lv_area_t area = { .x1 = 10, .y1 = 10, .x2 = 150, .y2 = 110 };
                    lv_obj_invalidate_area( screen, &area );
                }
                lv_refr_now( disp );
                completed++;
            }
            lvgl_port_unlock();
        }
    }

    int64_t elapsed_ms = ( esp_timer_get_time() - start ) / 1000;
    ESP_LOGI( TAG, "flush stress: %d/%d flushes in %lld ms", completed,
              iterations, elapsed_ms );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        iterations, completed,
        "Every flush should acquire the LVGL/SPI lock and complete" );

    esp_lcd_touch_point_data_t points[ 1 ] = { 0 };
    uint8_t count = 0;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_display_touch_data_get( points, &count, 1 ),
        "Display/touch must stay healthy after the flush stress" );
}

/* ------------------------------------------------------------------------- */
/* Shared-bus thread safety: two SD writers hammering distinct files at once  */
/* must be serialized by the per-API mutex without corrupting either file.    */
/* ------------------------------------------------------------------------- */

typedef struct
{
    const char *path;
    const char *payload;
    int iterations;
    atomic_int ok;
    atomic_int timeout;
    atomic_int error;
    atomic_int mismatch;
    atomic_bool done;
} sd_writer_ctx_t;

static void _sd_writer_task( void *arg )
{
    sd_writer_ctx_t *ctx = ( sd_writer_ctx_t * )arg;
    char buffer[ 128 ];

    for ( int i = 0; i < ctx->iterations; i++ )
    {
        size_t wrote = 0;
        esp_err_t err = core2foraws_sd_write( ctx->path, ctx->payload, &wrote );
        if ( err == ESP_ERR_TIMEOUT )
        {
            atomic_fetch_add( &ctx->timeout, 1 );
            continue;
        }
        if ( err != ESP_OK )
        {
            atomic_fetch_add( &ctx->error, 1 );
            continue;
        }

        memset( buffer, 0, sizeof( buffer ) );
        err = core2foraws_sd_read( ctx->path, buffer, sizeof( buffer ) );
        if ( err == ESP_ERR_TIMEOUT )
        {
            atomic_fetch_add( &ctx->timeout, 1 );
            continue;
        }
        if ( err != ESP_OK )
        {
            atomic_fetch_add( &ctx->error, 1 );
            continue;
        }

        if ( strcmp( buffer, ctx->payload ) != 0 )
        {
            atomic_fetch_add( &ctx->mismatch, 1 );
        }
        else
        {
            atomic_fetch_add( &ctx->ok, 1 );
        }
    }

    atomic_store( &ctx->done, true );
    vTaskDelete( NULL );
}

void test_bus_concurrent_sd_writers( void )
{
    test_sd_require_mount();

    sd_writer_ctx_t a = { .path = "/hw_a.txt",
                          .payload = "writer-A payload AAAAAAAAAAAAAAAA",
                          .iterations = 60 };
    sd_writer_ctx_t b = { .path = "/hw_b.txt",
                          .payload = "writer-B payload BBBBBBBBBBBBBBBB",
                          .iterations = 60 };
    atomic_store( &a.done, false );
    atomic_store( &b.done, false );

    BaseType_t ra = xTaskCreatePinnedToCore( _sd_writer_task, "sd_a", 4096, &a,
                                             4, NULL, 0 );
    BaseType_t rb = xTaskCreatePinnedToCore( _sd_writer_task, "sd_b", 4096, &b,
                                             4, NULL, 1 );
    TEST_ASSERT_EQUAL_MESSAGE( pdPASS, ra, "Failed to start SD writer A" );
    TEST_ASSERT_EQUAL_MESSAGE( pdPASS, rb, "Failed to start SD writer B" );

    int waited_ms = 0;
    while ( ( !atomic_load( &a.done ) || !atomic_load( &b.done ) ) &&
            waited_ms < 30000 )
    {
        vTaskDelay( pdMS_TO_TICKS( 50 ) );
        waited_ms += 50;
    }

    ESP_LOGI( TAG, "writer A: ok=%d timeout=%d error=%d mismatch=%d",
              atomic_load( &a.ok ), atomic_load( &a.timeout ),
              atomic_load( &a.error ), atomic_load( &a.mismatch ) );
    ESP_LOGI( TAG, "writer B: ok=%d timeout=%d error=%d mismatch=%d",
              atomic_load( &b.ok ), atomic_load( &b.timeout ),
              atomic_load( &b.error ), atomic_load( &b.mismatch ) );

    TEST_ASSERT_TRUE_MESSAGE( atomic_load( &a.done ) && atomic_load( &b.done ),
                              "Concurrent SD writers did not finish - deadlock" );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, atomic_load( &a.mismatch ) + atomic_load( &b.mismatch ),
        "Concurrent SD writers corrupted each other's data" );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, atomic_load( &a.error ) + atomic_load( &b.error ),
        "Concurrent SD writers hit a hard I/O error" );

    core2foraws_sd_unmount();
}
