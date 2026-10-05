#include <unity.h>

#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_RTC";

static struct tm _make_time( int year, int mon, int mday, int hour, int min,
                             int sec )
{
    struct tm t = { 0 };
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    return t;
}

/* Writing then reading the clock must return the same wall time. UTC avoids
 * the timezone/DST math so this is an exact, deterministic round trip. */
void test_rtc_set_get_roundtrip( void )
{
    struct tm set = _make_time( 2025, 6, 15, 10, 30, 0 );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_utc_time_set( set ) );

    struct tm got = { 0 };
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_utc_time_get( &got ) );

    ESP_LOGI( TAG, "set UTC 2025-06-15 10:30:00, got %04d-%02d-%02d %02d:%02d:%02d",
              got.tm_year + 1900, got.tm_mon + 1, got.tm_mday, got.tm_hour,
              got.tm_min, got.tm_sec );

    TEST_ASSERT_EQUAL_INT( 2025, got.tm_year + 1900 );
    TEST_ASSERT_EQUAL_INT( 6, got.tm_mon + 1 );
    TEST_ASSERT_EQUAL_INT( 15, got.tm_mday );
    TEST_ASSERT_EQUAL_INT( 10, got.tm_hour );
    TEST_ASSERT_EQUAL_INT( 30, got.tm_min );
    TEST_ASSERT_INT_WITHIN_MESSAGE( 3, 0, got.tm_sec,
                                    "Seconds should be close to the set value" );
}

/* The clock must actually tick forward over a real delay. */
void test_rtc_monotonic( void )
{
    struct tm set = _make_time( 2025, 1, 1, 0, 0, 0 );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_time_set( set ) );

    struct tm t1 = { 0 };
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_time_get( &t1 ) );
    vTaskDelay( pdMS_TO_TICKS( 2100 ) );
    struct tm t2 = { 0 };
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_time_get( &t2 ) );

    time_t s1 = mktime( &t1 );
    time_t s2 = mktime( &t2 );
    ESP_LOGI( TAG, "elapsed %ld s", ( long )( s2 - s1 ) );
    TEST_ASSERT_TRUE_MESSAGE( s2 > s1,
                              "RTC must advance across a 2 s wall-clock delay" );
    TEST_ASSERT_INT_WITHIN_MESSAGE( 2, 2, ( int )( s2 - s1 ),
                                    "Elapsed RTC time should track real time" );
}

/* Countdown timer set/get/stop must behave. */
void test_rtc_timer_countdown( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_timer_set( 10 ) );

    uint32_t remaining = 0;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_timer_get( &remaining ) );
    ESP_LOGI( TAG, "timer remaining = %u s", ( unsigned )remaining );
    TEST_ASSERT_TRUE_MESSAGE( remaining > 0 && remaining <= 10,
                              "Timer should report a value within the set span" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_timer_stop() );
}

/* Alarm set/get/disable and status flags must not fault. */
void test_rtc_alarm_lifecycle( void )
{
    struct tm alarm = _make_time( 2025, 6, 15, 11, 0, 0 );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_alarm_set( alarm ) );

    struct tm got = { 0 };
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_alarm_get( &got ) );

    bool triggered = true;
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_rtc_alarm_status( &triggered, false ) );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_rtc_alarm_disable() );
}
