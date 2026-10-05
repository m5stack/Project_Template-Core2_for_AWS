#include <unity.h>

#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_HW_MAIN";

int64_t g_test_bsp_init_us;

/* Logs each test name before it runs so a hanging test is visible on raw
 * serial (Unity only prints a result after a test returns). */
#define RUN_TEST_MARKED( fn )                                                  \
    do                                                                         \
    {                                                                          \
        ESP_LOGW( TAG, ">>> START %s", #fn );                                  \
        RUN_TEST( fn );                                                        \
        ESP_LOGW( TAG, "<<< END   %s", #fn );                                  \
    } while ( 0 )

/* Tiers run cheapest-and-most-diagnostic first so an early contract break is
 * obvious before the destructive and long-running tiers muddy the log. */
#define TIER( n, name ) ESP_LOGW( TAG, "===== TIER %d: %s =====", n, name )

void setUp( void ) {}

void tearDown( void ) {}

void app_main( void )
{
    /* Bring up the board exactly as an application would. */
    const int64_t init_start_us = esp_timer_get_time();
    esp_err_t err = core2foraws_init();
    g_test_bsp_init_us = esp_timer_get_time() - init_start_us;
    ESP_LOGI( TAG, "core2foraws_init returned %s in %lld ms", esp_err_to_name( err ),
              g_test_bsp_init_us / 1000 );

    /* Let the LVGL port task and hardware drivers settle before testing. */
    vTaskDelay( pdMS_TO_TICKS( 500 ) );

    UNITY_BEGIN();

    TIER( 1, "module contracts" );


    /* Display feature API + lifecycle. */
    RUN_TEST_MARKED( test_display_ready );
    RUN_TEST_MARKED( test_display_resolution );
    RUN_TEST_MARKED( test_display_color_format );
    RUN_TEST_MARKED( test_display_touch_data_get_smoke );
    RUN_TEST_MARKED( test_display_touch_arg_validation );
    RUN_TEST_MARKED( test_display_get_touch_handle_api );
    RUN_TEST_MARKED( test_display_init_deinit_cycles );
    RUN_TEST_MARKED( test_display_state_after_deinit );
    RUN_TEST_MARKED( test_display_flush_stress );

    /* Power management unit (AXP192). */
    RUN_TEST_MARKED( test_power_readings );
    RUN_TEST_MARKED( test_power_backlight_sweep );
    RUN_TEST_MARKED( test_power_rails_read );
    RUN_TEST_MARKED( test_power_actuators_toggle );
    RUN_TEST_MARKED( test_power_repeated_reads_stable );

    /* Motion / IMU (MPU6886). */
    RUN_TEST_MARKED( test_motion_accel_plausible );
    RUN_TEST_MARKED( test_motion_gyro_plausible );
    RUN_TEST_MARKED( test_motion_temperature_plausible );
    RUN_TEST_MARKED( test_motion_range_settings );
    RUN_TEST_MARKED( test_motion_repeated_reads_stable );

    /* Real-time clock (BM8563). */
    RUN_TEST_MARKED( test_rtc_set_get_roundtrip );
    RUN_TEST_MARKED( test_rtc_monotonic );
    RUN_TEST_MARKED( test_rtc_timer_countdown );
    RUN_TEST_MARKED( test_rtc_alarm_lifecycle );

    /* Side RGB LED bars (SK6812 / RMT). */
    RUN_TEST_MARKED( test_rgb_led_set_and_write );
    RUN_TEST_MARKED( test_rgb_led_deinit_init_cycles );

    /* Virtual touch buttons (FT6336). */
    RUN_TEST_MARKED( test_button_register_valid );
    RUN_TEST_MARKED( test_button_register_invalid_args );
    RUN_TEST_MARKED( test_button_init_idempotent );

    /* Secure element (ATECC608). */
    RUN_TEST_MARKED( test_crypto_serial_get );
    RUN_TEST_MARKED( test_crypto_device_info_read_only );
    RUN_TEST_MARKED( test_crypto_config_and_lock_state_read_only );
    RUN_TEST_MARKED( test_crypto_pubkey_get );
    RUN_TEST_MARKED( test_crypto_external_verify_read_only );
    RUN_TEST_MARKED( test_crypto_survives_unsolicited_wake );

    /* Expansion ports (GPIO / ADC / DAC / I2C / UART). */
    RUN_TEST_MARKED( test_expports_digital_io );
    RUN_TEST_MARKED( test_expports_adc_read );
    RUN_TEST_MARKED( test_expports_dac_write );
    RUN_TEST_MARKED( test_expports_external_i2c_lifecycle );
    RUN_TEST_MARKED( test_expports_uart_lifecycle );

    /* Audio (I2S speaker + mic). */
    RUN_TEST_MARKED( test_audio_speaker_lifecycle );
    RUN_TEST_MARKED( test_audio_mic_lifecycle );
    RUN_TEST_MARKED( test_audio_speaker_mic_mutual_exclusion );

    /* SD feature API + lifecycle. */
    RUN_TEST_MARKED( test_sd_unmount_requires_mount );
    RUN_TEST_MARKED( test_sd_mount_write_read_unmount );

    TIER( 2, "boot health and timing budgets" );

    RUN_TEST_MARKED( test_timing_previous_reset_was_clean );
    RUN_TEST_MARKED( test_timing_boot_to_ready_budget );
    RUN_TEST_MARKED( test_timing_i2c_read_latency );
    RUN_TEST_MARKED( test_timing_display_frame_budget );
    RUN_TEST_MARKED( test_timing_scheduler_responsiveness );

    RUN_TEST_MARKED( test_tuning_delay_macro_meets_minimum );
    RUN_TEST_MARKED( test_tuning_boot_init_duration );
    RUN_TEST_MARKED( test_tuning_motion_burst_sample );
    RUN_TEST_MARKED( test_tuning_bounded_i2c_wait );
    RUN_TEST_MARKED( test_tuning_rgb_back_to_back_frames );
    RUN_TEST_MARKED( test_tuning_sd_psram_buffer_throughput );

    TIER( 3, "fault injection and durability" );

    RUN_TEST_MARKED( test_fault_null_output_pointers );
    RUN_TEST_MARKED( test_fault_out_of_range_arguments );
    RUN_TEST_MARKED( test_fault_storage_before_mount );
    RUN_TEST_MARKED( test_fault_missing_file_read );
    RUN_TEST_MARKED( test_fault_i2c_absent_device_recovery );
    RUN_TEST_MARKED( test_fault_repeated_teardown );

    RUN_TEST_MARKED( test_storage_nvs_commit_durability );
    RUN_TEST_MARKED( test_storage_nvs_missing_key );
    RUN_TEST_MARKED( test_storage_sd_survives_remount );
    RUN_TEST_MARKED( test_storage_sd_large_payload_integrity );
    RUN_TEST_MARKED( test_storage_sd_repeated_rewrite );

    TIER( 4, "integration and connectivity" );

    /* Cross-module interactions on the shared buses. */
    RUN_TEST_MARKED( test_bus_concurrent_sd_writers );
    RUN_TEST_MARKED( test_display_sd_concurrent );
    RUN_TEST_MARKED( test_internal_i2c_concurrency );

#ifdef CONFIG_SOFTWARE_WIFI_SUPPORT
    RUN_TEST_MARKED( test_connectivity_ops_before_init );
    RUN_TEST_MARKED( test_connectivity_arg_validation );
    RUN_TEST_MARKED( test_connectivity_state_reporting );
    RUN_TEST_MARKED( test_connectivity_wifi_init_deinit_reclaims );
    RUN_TEST_MARKED( test_connectivity_wifi_init_idempotent );
#ifdef CONFIG_CORE2FORAWS_WIFI_PROVISIONING
    RUN_TEST_MARKED( test_connectivity_provisioning_string );
#endif
    RUN_TEST_MARKED( test_connectivity_display_survives_radio );
#endif

    TIER( 5, "endurance and resource budgets" );

    /* Organic workloads first. The budget assertions below read since-boot heap
     * low-water marks, which cannot be reset once a synthetic-pressure test has
     * deliberately driven them to the floor. */
    RUN_TEST_MARKED( test_display_sd_soak_no_leak );
    RUN_TEST_MARKED( test_all_subsystems_soak );
    RUN_TEST_MARKED( test_memory_fragmentation_under_churn );
    RUN_TEST_MARKED( test_memory_worst_case_concurrent_footprint );

    /* Budgets, measured against the organic peak reached above. */
    RUN_TEST_MARKED( test_dram_feature_budget_report );
    RUN_TEST_MARKED( test_task_stack_watermarks );

    /* Synthetic exhaustion last: these prove graceful degradation by driving
     * the heap to its floor, which invalidates the watermarks asserted above. */
    RUN_TEST_MARKED( test_memory_graceful_under_exhaustion );
    RUN_TEST_MARKED( test_dram_display_realloc_under_pressure );

    UNITY_END();

    /* Stop the LVGL render task so it does not spin and trip the task
     * watchdog after the suite returns; then idle quietly. */
    core2foraws_display_deinit();
    while ( 1 )
    {
        vTaskDelay( portMAX_DELAY );
    }
}
