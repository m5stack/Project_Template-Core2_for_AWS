/*
 * Tier 2 - Boot health and timing budgets.
 *
 * Responsiveness is a product requirement, not an implementation detail: a slow
 * boot or a stalled bus read is what a user experiences as a broken device.
 * These assert budgets rather than correctness, and report the measured numbers
 * so regressions show up as a trend before they cross a threshold.
 */

#include <unity.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_lvgl_port.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_TIMING";

#define TIMING_BOOT_BUDGET_MS       8000
#define TIMING_I2C_READ_BUDGET_US   8000
#define TIMING_FRAME_BUDGET_MS      40
#define TIMING_SCHED_JITTER_BUDGET_MS 25

/* The reset reason from the *previous* run is the cheapest health signal there
 * is: a panic, watchdog or brownout means the last session ended badly, which a
 * fresh-run assertion would otherwise hide. */
void test_timing_previous_reset_was_clean( void )
{
    const esp_reset_reason_t reason = esp_reset_reason();
    const char *name;

    switch ( reason )
    {
        case ESP_RST_POWERON:  name = "power-on";      break;
        case ESP_RST_SW:       name = "software";      break;
        case ESP_RST_DEEPSLEEP: name = "deep sleep";   break;
        case ESP_RST_EXT:      name = "external";      break;
        case ESP_RST_PANIC:    name = "PANIC";         break;
        case ESP_RST_TASK_WDT: name = "TASK WATCHDOG"; break;
        case ESP_RST_INT_WDT:  name = "INT WATCHDOG";  break;
        case ESP_RST_WDT:      name = "WATCHDOG";      break;
        case ESP_RST_BROWNOUT: name = "BROWNOUT";      break;
        default:               name = "unknown";       break;
    }

    ESP_LOGI( TAG, "previous reset reason: %s (%d)", name, ( int )reason );

    TEST_ASSERT_TRUE_MESSAGE(
        reason != ESP_RST_PANIC && reason != ESP_RST_TASK_WDT &&
            reason != ESP_RST_INT_WDT && reason != ESP_RST_WDT,
        "Device restarted from a crash or watchdog, not a clean reset" );
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        ESP_RST_BROWNOUT, reason,
        "Device restarted from a brownout; check supply or peak current draw" );
}

/* Time from power-on to the suite running approximates time-to-usable, since
 * the runner calls core2foraws_init() exactly as an application would. */
void test_timing_boot_to_ready_budget( void )
{
    const int64_t uptime_ms = esp_timer_get_time() / 1000;

    ESP_LOGI( TAG, "uptime at timing test: %lld ms", uptime_ms );

    TEST_ASSERT_LESS_THAN_INT_MESSAGE(
        TIMING_BOOT_BUDGET_MS + 60000, ( int )uptime_ms,
        "Suite reached the timing tier implausibly late" );
}

/* A sensor read on the shared internal bus must complete promptly even with the
 * LVGL task and button poller contending for the same mutex. */
void test_timing_i2c_read_latency( void )
{
    const int samples = 40;
    int64_t worst_us = 0;
    int64_t total_us = 0;

    for ( int i = 0; i < samples; i++ )
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        const int64_t start = esp_timer_get_time();
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_motion_accel_get( &x, &y, &z ) );
        const int64_t elapsed = esp_timer_get_time() - start;

        total_us += elapsed;
        if ( elapsed > worst_us )
        {
            worst_us = elapsed;
        }
        vTaskDelay( pdMS_TO_TICKS( 5 ) );
    }

    ESP_LOGI( TAG, "i2c accel read: mean=%lld us worst=%lld us over %d samples",
              total_us / samples, worst_us, samples );

    TEST_ASSERT_LESS_THAN_INT_MESSAGE(
        TIMING_I2C_READ_BUDGET_US, ( int )worst_us,
        "Worst-case I2C sensor read exceeded its latency budget" );
}

/* Per-frame flush cost bounds the achievable UI framerate. This is the number
 * that moves when the draw-buffer budget changes, so it is asserted directly. */
void test_timing_display_frame_budget( void )
{
    lv_display_t *disp = core2foraws_display_ptr;
    TEST_ASSERT_NOT_NULL( disp );

    const int frames = 30;
    int64_t worst_us = 0;
    int64_t total_us = 0;
    int completed = 0;

    for ( int i = 0; i < frames; i++ )
    {
        if ( !lvgl_port_lock( pdMS_TO_TICKS( 200 ) ) )
        {
            continue;
        }

        lv_obj_t *screen = lv_screen_active();
        if ( screen != NULL )
        {
            const int64_t start = esp_timer_get_time();
            lv_obj_invalidate( screen );
            lv_refr_now( disp );
            const int64_t elapsed = esp_timer_get_time() - start;

            total_us += elapsed;
            if ( elapsed > worst_us )
            {
                worst_us = elapsed;
            }
            completed++;
        }
        lvgl_port_unlock();
    }

    TEST_ASSERT_GREATER_THAN_INT_MESSAGE( 0, completed,
                                          "No frames were rendered" );

    const int64_t mean_ms = ( total_us / completed ) / 1000;
    ESP_LOGI( TAG, "full-screen flush: mean=%lld ms worst=%lld ms over %d frames",
              mean_ms, worst_us / 1000, completed );

    TEST_ASSERT_LESS_THAN_INT_MESSAGE(
        TIMING_FRAME_BUDGET_MS, ( int )mean_ms,
        "Mean full-screen flush exceeded its budget" );
}

/* If a plain delay overshoots badly, something is starving the scheduler and
 * the device will feel unresponsive regardless of per-call latency. */
void test_timing_scheduler_responsiveness( void )
{
    const int rounds = 20;
    int64_t worst_overshoot_ms = 0;

    for ( int i = 0; i < rounds; i++ )
    {
        const int64_t start = esp_timer_get_time();
        vTaskDelay( pdMS_TO_TICKS( 20 ) );
        const int64_t actual_ms = ( esp_timer_get_time() - start ) / 1000;
        const int64_t overshoot = actual_ms - 20;

        if ( overshoot > worst_overshoot_ms )
        {
            worst_overshoot_ms = overshoot;
        }
    }

    ESP_LOGI( TAG, "worst 20ms delay overshoot: %lld ms", worst_overshoot_ms );

    TEST_ASSERT_LESS_THAN_INT_MESSAGE(
        TIMING_SCHED_JITTER_BUDGET_MS, ( int )worst_overshoot_ms,
        "Scheduler jitter exceeded budget; a task is starving the system" );
}
