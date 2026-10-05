#include <unity.h>

#include <math.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_MOTION";

static float _accel_magnitude( void )
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_motion_accel_get( &x, &y, &z ) );
    TEST_ASSERT_TRUE( isfinite( x ) && isfinite( y ) && isfinite( z ) );
    return sqrtf( x * x + y * y + z * z );
}

/* At rest the accelerometer must read ~1 g total (gravity). */
void test_motion_accel_plausible( void )
{
    float mag = _accel_magnitude();
    ESP_LOGI( TAG, "accel magnitude = %.3f g", mag );
    TEST_ASSERT_TRUE_MESSAGE(
        mag > 0.5f && mag < 1.5f,
        "At rest the accelerometer vector magnitude should be about 1 g" );
}

/* At rest the gyro should be near zero (allow generous bias/noise). */
void test_motion_gyro_plausible( void )
{
    float r = 0.0f, p = 0.0f, yw = 0.0f;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_motion_gyro_get( &r, &p, &yw ) );
    TEST_ASSERT_TRUE( isfinite( r ) && isfinite( p ) && isfinite( yw ) );
    ESP_LOGI( TAG, "gyro = %.1f %.1f %.1f dps", r, p, yw );
    TEST_ASSERT_TRUE_MESSAGE(
        fabsf( r ) < 300.0f && fabsf( p ) < 300.0f && fabsf( yw ) < 300.0f,
        "Stationary gyro rates should be small" );
}

void test_motion_temperature_plausible( void )
{
    float temp = 0.0f;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_motion_temperature_get( &temp ) );
    ESP_LOGI( TAG, "die temp = %.1f C", temp );
    TEST_ASSERT_TRUE_MESSAGE(
        isfinite( temp ) && temp > 0.0f && temp < 70.0f,
        "IMU die temperature should be a plausible indoor/operating value" );
}

/* Every full-scale range must apply and still yield a valid reading. Per the
 * driver contract the first samples after a range change are unsettled, so we
 * discard a few before asserting gravity still reads ~1 g. */
void test_motion_range_settings( void )
{
    const motion_accel_range_t a_ranges[] = {
        MOTION_ACCEL_RANGE_2G, MOTION_ACCEL_RANGE_4G, MOTION_ACCEL_RANGE_8G,
        MOTION_ACCEL_RANGE_16G };
    for ( size_t i = 0; i < sizeof( a_ranges ) / sizeof( a_ranges[ 0 ] ); i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK,
                           core2foraws_motion_accel_range_set( a_ranges[ i ] ) );

        /* Discard the unsettled samples that follow a range change. */
        for ( int d = 0; d < 5; d++ )
        {
            float x, y, z;
            core2foraws_motion_accel_get( &x, &y, &z );
            vTaskDelay( pdMS_TO_TICKS( 10 ) );
        }

        float mag = _accel_magnitude();
        ESP_LOGI( TAG, "range idx %u -> magnitude %.3f g", ( unsigned )i, mag );
        TEST_ASSERT_TRUE_MESSAGE( mag > 0.5f && mag < 1.5f,
                                  "Gravity should read ~1 g in every range" );
    }

    const motion_gyro_range_t g_ranges[] = {
        MOTION_GYRO_RANGE_250DPS, MOTION_GYRO_RANGE_500DPS,
        MOTION_GYRO_RANGE_1000DPS, MOTION_GYRO_RANGE_2000DPS };
    for ( size_t i = 0; i < sizeof( g_ranges ) / sizeof( g_ranges[ 0 ] ); i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK,
                           core2foraws_motion_gyro_range_set( g_ranges[ i ] ) );
    }

    /* Restore the driver defaults. */
    core2foraws_motion_accel_range_set( MOTION_ACCEL_RANGE_8G );
    core2foraws_motion_gyro_range_set( MOTION_GYRO_RANGE_2000DPS );
}

void test_motion_repeated_reads_stable( void )
{
    for ( int i = 0; i < 100; i++ )
    {
        float mag = _accel_magnitude();
        TEST_ASSERT_TRUE_MESSAGE(
            mag > 0.3f && mag < 2.0f,
            "Accelerometer should stay plausible across rapid reads" );
    }
}
