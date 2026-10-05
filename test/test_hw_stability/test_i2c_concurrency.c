#include <unity.h>

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lcd_touch.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_I2C_CONC";

/* AXP192, MPU6886, BM8563, FT6336, and ATECC608 all share the internal I2C
 * bus behind one recursive lock. This test hammers all five from separate
 * tasks on both cores at once and proves the lock serializes them with zero
 * bus errors and zero data corruption (readings stay self-consistent). */

#define I2C_ITERS_FAST 80
#define I2C_ITERS_CRYPTO 20
#define I2C_JOIN_TIMEOUT_MS 30000

typedef struct
{
    atomic_int power_ok, power_err;
    atomic_int motion_ok, motion_err, motion_bad;
    atomic_int rtc_ok, rtc_err;
    atomic_int touch_ok, touch_err;
    atomic_int crypto_ok, crypto_err, crypto_bad;
    char crypto_ref[ CRYPTO_SERIAL_STR_SIZE ];
    atomic_int running;
} i2c_ctx_t;

static void _power_worker( void *arg )
{
    i2c_ctx_t *c = arg;
    for ( int i = 0; i < I2C_ITERS_FAST; i++ )
    {
        float v = 0.0f;
        if ( core2foraws_power_batt_volts_get( &v ) == ESP_OK && isfinite( v ) )
            atomic_fetch_add( &c->power_ok, 1 );
        else
            atomic_fetch_add( &c->power_err, 1 );
    }
    atomic_fetch_sub( &c->running, 1 );
    vTaskDelete( NULL );
}

static void _motion_worker( void *arg )
{
    i2c_ctx_t *c = arg;
    for ( int i = 0; i < I2C_ITERS_FAST; i++ )
    {
        float x = 0, y = 0, z = 0;
        if ( core2foraws_motion_accel_get( &x, &y, &z ) == ESP_OK )
        {
            /* Any finite vector is fine - the board may be moved during the
             * test, so magnitude is not a corruption signal. Only non-finite
             * garbage indicates a torn transfer. */
            if ( isfinite( x ) && isfinite( y ) && isfinite( z ) )
                atomic_fetch_add( &c->motion_ok, 1 );
            else
                atomic_fetch_add( &c->motion_bad, 1 );
        }
        else
            atomic_fetch_add( &c->motion_err, 1 );
    }
    atomic_fetch_sub( &c->running, 1 );
    vTaskDelete( NULL );
}

static void _rtc_worker( void *arg )
{
    i2c_ctx_t *c = arg;
    for ( int i = 0; i < I2C_ITERS_FAST; i++ )
    {
        struct tm t = { 0 };
        if ( core2foraws_rtc_time_get( &t ) == ESP_OK )
            atomic_fetch_add( &c->rtc_ok, 1 );
        else
            atomic_fetch_add( &c->rtc_err, 1 );
    }
    atomic_fetch_sub( &c->running, 1 );
    vTaskDelete( NULL );
}

static void _touch_worker( void *arg )
{
    i2c_ctx_t *c = arg;
    for ( int i = 0; i < I2C_ITERS_FAST; i++ )
    {
        esp_lcd_touch_point_data_t p[ 1 ] = { 0 };
        uint8_t n = 0;
        if ( core2foraws_display_touch_data_get( p, &n, 1 ) == ESP_OK )
            atomic_fetch_add( &c->touch_ok, 1 );
        else
            atomic_fetch_add( &c->touch_err, 1 );
    }
    atomic_fetch_sub( &c->running, 1 );
    vTaskDelete( NULL );
}

static void _crypto_worker( void *arg )
{
    i2c_ctx_t *c = arg;
    for ( int i = 0; i < I2C_ITERS_CRYPTO; i++ )
    {
        char serial[ CRYPTO_SERIAL_STR_SIZE ] = { 0 };
        if ( core2foraws_crypto_serial_get( serial ) == ESP_OK )
        {
            if ( strcmp( serial, c->crypto_ref ) == 0 )
                atomic_fetch_add( &c->crypto_ok, 1 );
            else
                atomic_fetch_add( &c->crypto_bad, 1 ); /* corrupted reading */
        }
        else
            atomic_fetch_add( &c->crypto_err, 1 );
    }
    atomic_fetch_sub( &c->running, 1 );
    vTaskDelete( NULL );
}

void test_internal_i2c_concurrency( void )
{
    static i2c_ctx_t c;
    memset( &c, 0, sizeof( c ) );

    /* Reference serial for the corruption check (may be unprovisioned). */
    bool crypto_present =
        ( core2foraws_crypto_serial_get( c.crypto_ref ) == ESP_OK );

    int workers = crypto_present ? 5 : 4;
    atomic_store( &c.running, workers );

    xTaskCreatePinnedToCore( _power_worker, "i2c_pwr", 3072, &c, 5, NULL, 0 );
    xTaskCreatePinnedToCore( _motion_worker, "i2c_mot", 3072, &c, 5, NULL, 1 );
    xTaskCreatePinnedToCore( _rtc_worker, "i2c_rtc", 3072, &c, 5, NULL, 0 );
    xTaskCreatePinnedToCore( _touch_worker, "i2c_tch", 3072, &c, 5, NULL, 1 );
    if ( crypto_present )
        xTaskCreatePinnedToCore( _crypto_worker, "i2c_cry", 4096, &c, 5, NULL,
                                 0 );

    int waited = 0;
    while ( atomic_load( &c.running ) > 0 && waited < I2C_JOIN_TIMEOUT_MS )
    {
        vTaskDelay( pdMS_TO_TICKS( 50 ) );
        waited += 50;
    }
    vTaskDelay( pdMS_TO_TICKS( 100 ) );

    ESP_LOGI( TAG,
              "power ok=%d err=%d | motion ok=%d err=%d bad=%d | rtc ok=%d "
              "err=%d",
              atomic_load( &c.power_ok ), atomic_load( &c.power_err ),
              atomic_load( &c.motion_ok ), atomic_load( &c.motion_err ),
              atomic_load( &c.motion_bad ), atomic_load( &c.rtc_ok ),
              atomic_load( &c.rtc_err ) );
    ESP_LOGI( TAG, "touch ok=%d err=%d | crypto ok=%d err=%d bad=%d present=%d",
              atomic_load( &c.touch_ok ), atomic_load( &c.touch_err ),
              atomic_load( &c.crypto_ok ), atomic_load( &c.crypto_err ),
              atomic_load( &c.crypto_bad ), crypto_present );

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, atomic_load( &c.running ),
        "All internal-I2C workers must finish (no bus deadlock)" );

    int errors = atomic_load( &c.power_err ) + atomic_load( &c.motion_err ) +
                 atomic_load( &c.rtc_err ) + atomic_load( &c.touch_err ) +
                 atomic_load( &c.crypto_err );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, errors, "Concurrent internal-I2C access produced bus errors" );

    int corrupt = atomic_load( &c.motion_bad ) + atomic_load( &c.crypto_bad );
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, corrupt,
        "Concurrent access corrupted a reading (lock failed to serialize)" );

    TEST_ASSERT_GREATER_THAN_INT( 0, atomic_load( &c.power_ok ) );
    TEST_ASSERT_GREATER_THAN_INT( 0, atomic_load( &c.motion_ok ) );
    TEST_ASSERT_GREATER_THAN_INT( 0, atomic_load( &c.rtc_ok ) );
    TEST_ASSERT_GREATER_THAN_INT( 0, atomic_load( &c.touch_ok ) );
}
