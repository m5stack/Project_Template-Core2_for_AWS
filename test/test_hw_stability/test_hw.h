#ifndef _TEST_HW_H_
#define _TEST_HW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <esp_err.h>
#include <sdkconfig.h>

/* Mirrors the BSP's draw-buffer geometry so every test measures the budget the
 * display actually allocates. Kept here rather than per-file so a Kconfig
 * change cannot leave one test asserting against a stale size. */
#ifdef CONFIG_CORE2FORAWS_LCD_DRAW_BUF_LINES
#define TEST_LCD_DRAW_BUF_LINES CONFIG_CORE2FORAWS_LCD_DRAW_BUF_LINES
#else
#define TEST_LCD_DRAW_BUF_LINES 20
#endif

/* 320 px wide, 2 bytes/px (RGB565); one of two double-buffered buffers. */
#define TEST_LCD_DRAW_BUF_BYTES ( 320 * TEST_LCD_DRAW_BUF_LINES * 2 )

/* Shared fixtures for the LCD + SD shared-SPI stability suite. */
#define TEST_SD_FILE_PATH "/hw_test.txt"
#define TEST_SD_PAYLOAD   "Core2 for AWS IoT Kit shared-SPI stability probe 0123456789"

/* Free internal DRAM the soak loop is allowed to lose before it is treated as
 * a leak. Covers one-shot lazy allocations and heap fragmentation slack. */
#define TEST_HEAP_LEAK_BUDGET_BYTES ( 4 * 1024 )

/* DMA-capable DRAM that must remain free with every subsystem active at once.
 * Measured headroom is far above this; the floor exists to catch a regression
 * that quietly consumes the reserve the display depends on. */
#define TEST_DMA_PEAK_FLOOR_BYTES ( 64 * 1024 )

/**
 * @brief Mount the SD card, or skip the calling test if no card is present.
 *
 * Calls TEST_IGNORE (which aborts the current test) when mounting fails so the
 * suite stays green on units with no card inserted.
 *
 * @return true once the card is mounted.
 */
bool test_sd_require_mount( void );

/* Duration of the runner's core2foraws_init() call, in microseconds. */
extern int64_t g_test_bsp_init_us;

/* BSP tuning regression checks. */
void test_crypto_survives_unsolicited_wake( void );
void test_tuning_delay_macro_meets_minimum( void );
void test_tuning_boot_init_duration( void );
void test_tuning_motion_burst_sample( void );
void test_tuning_bounded_i2c_wait( void );
void test_tuning_rgb_back_to_back_frames( void );
void test_tuning_sd_psram_buffer_throughput( void );

/* Display feature API + lifecycle. */
void test_display_ready( void );
void test_display_touch_data_get_smoke( void );
void test_display_init_deinit_cycles( void );

/* Comprehensive LCD panel + shared-SPI bus coverage. */
void test_display_resolution( void );
void test_display_color_format( void );
void test_display_touch_arg_validation( void );
void test_display_get_touch_handle_api( void );
void test_display_state_after_deinit( void );
void test_display_flush_stress( void );
void test_bus_concurrent_sd_writers( void );

/* SD feature API + lifecycle. */
void test_sd_mount_write_read_unmount( void );
void test_sd_unmount_requires_mount( void );

/* Shared-SPI concurrency + long-run stability. */
void test_display_sd_concurrent( void );
void test_display_sd_soak_no_leak( void );

/* DMA-capable internal DRAM budget for the LCD draw buffers vs Wi-Fi/BLE. */
void test_dram_feature_budget_report( void );
void test_dram_display_realloc_under_pressure( void );
void test_task_stack_watermarks( void );

/* Tier 2 - boot health and timing budgets. */
void test_timing_previous_reset_was_clean( void );
void test_timing_boot_to_ready_budget( void );
void test_timing_i2c_read_latency( void );
void test_timing_display_frame_budget( void );
void test_timing_scheduler_responsiveness( void );

/* Tier 3 - fault injection and error paths. */
void test_fault_null_output_pointers( void );
void test_fault_out_of_range_arguments( void );
void test_fault_storage_before_mount( void );
void test_fault_missing_file_read( void );
void test_fault_i2c_absent_device_recovery( void );
void test_fault_repeated_teardown( void );

/* Tier 3 - storage durability and integrity. */
void test_storage_nvs_commit_durability( void );
void test_storage_nvs_missing_key( void );
void test_storage_sd_survives_remount( void );
void test_storage_sd_large_payload_integrity( void );
void test_storage_sd_repeated_rewrite( void );

/* Tier 4 - connectivity resilience. */
void test_connectivity_wifi_init_deinit_reclaims( void );
void test_connectivity_display_survives_radio( void );
void test_connectivity_wifi_init_idempotent( void );
void test_connectivity_ops_before_init( void );
void test_connectivity_arg_validation( void );
void test_connectivity_state_reporting( void );
void test_connectivity_provisioning_string( void );

/* Tier 5 - memory behaviour over time. */
void test_memory_fragmentation_under_churn( void );
void test_memory_worst_case_concurrent_footprint( void );
void test_memory_graceful_under_exhaustion( void );

/* Power management unit (AXP192, internal I2C). */
void test_power_readings( void );
void test_power_backlight_sweep( void );
void test_power_rails_read( void );
void test_power_actuators_toggle( void );
void test_power_repeated_reads_stable( void );

/* Motion / IMU (MPU6886, internal I2C). */
void test_motion_accel_plausible( void );
void test_motion_gyro_plausible( void );
void test_motion_temperature_plausible( void );
void test_motion_range_settings( void );
void test_motion_repeated_reads_stable( void );

/* Real-time clock (BM8563, internal I2C). */
void test_rtc_set_get_roundtrip( void );
void test_rtc_monotonic( void );
void test_rtc_timer_countdown( void );
void test_rtc_alarm_lifecycle( void );

/* Side RGB LED bars (SK6812 via RMT). */
void test_rgb_led_set_and_write( void );
void test_rgb_led_deinit_init_cycles( void );

/* Virtual touch buttons (FT6336 via internal I2C). */
void test_button_register_valid( void );
void test_button_register_invalid_args( void );
void test_button_init_idempotent( void );

/* Secure element (ATECC608, internal I2C). */
void test_crypto_serial_get( void );
void test_crypto_device_info_read_only( void );
void test_crypto_config_and_lock_state_read_only( void );
void test_crypto_pubkey_get( void );
void test_crypto_external_verify_read_only( void );

/* Expansion ports (GPIO / ADC / DAC / I2C / UART). */
void test_expports_digital_io( void );
void test_expports_adc_read( void );
void test_expports_dac_write( void );
void test_expports_external_i2c_lifecycle( void );
void test_expports_uart_lifecycle( void );

/* Audio (I2S speaker + mic, mutually exclusive on GPIO0). */
void test_audio_speaker_lifecycle( void );
void test_audio_mic_lifecycle( void );
void test_audio_speaker_mic_mutual_exclusion( void );

/* Cross-module interactions. */
void test_internal_i2c_concurrency( void );
void test_all_subsystems_soak( void );

#endif /* _TEST_HW_H_ */
