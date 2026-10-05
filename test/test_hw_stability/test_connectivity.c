/*
 * Tier 4 - Connectivity resilience.
 *
 * Radio bring-up is the single largest consumer of internal DRAM on this board,
 * and internal DRAM is what the display's DMA draw buffers compete for. These
 * tests measure that cost directly and prove the display still works with the
 * radio stack resident, which is the configuration the device actually ships in.
 *
 * Association with a real access point is deliberately out of scope: it needs
 * credentials and an AP, so it belongs in a rig-backed tier rather than a suite
 * that must pass on any desk.
 */

#include <unity.h>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_CONNECT";

#ifdef CONFIG_SOFTWARE_WIFI_SUPPORT

/* Internal DRAM the radio stack may keep after a full deinit. Some ESP-IDF
 * allocations are one-shot and intentionally never returned; anything beyond
 * this is a leak across connect/disconnect cycles. */
#define WIFI_TEARDOWN_SLACK_BYTES ( 24 * 1024 )

/* Bringing the Wi-Fi stack up and back down must return what it borrowed. A
 * device that reconnects for weeks does this hundreds of times. */
void test_connectivity_wifi_init_deinit_reclaims( void )
{
    core2foraws_common_heap_stats_t before = { 0 };
    core2foraws_common_heap_report( "WIFI_BEFORE", &before );

    esp_err_t init_err = core2foraws_wifi_init();
    if ( init_err != ESP_OK )
    {
        ESP_LOGW( TAG, "wifi_init returned %s", esp_err_to_name( init_err ) );
        TEST_IGNORE_MESSAGE( "Wi-Fi stack unavailable on this build" );
    }

    core2foraws_common_heap_stats_t up = { 0 };
    core2foraws_common_heap_report( "WIFI_UP", &up );

    esp_err_t deinit_err = core2foraws_wifi_deinit();
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    core2foraws_common_heap_stats_t after = { 0 };
    core2foraws_common_heap_report( "WIFI_AFTER", &after );

    const size_t cost = ( before.internal_free > up.internal_free )
                            ? before.internal_free - up.internal_free
                            : 0;
    const size_t retained = ( before.internal_free > after.internal_free )
                                ? before.internal_free - after.internal_free
                                : 0;

    ESP_LOGI( TAG, "wifi internal DRAM cost=%u retained_after_deinit=%u deinit=%s",
              ( unsigned )cost, ( unsigned )retained,
              esp_err_to_name( deinit_err ) );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, deinit_err, "wifi_deinit must succeed" );
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
        WIFI_TEARDOWN_SLACK_BYTES, retained,
        "Wi-Fi teardown did not return its internal DRAM" );
}

/* The configuration that matters in the field: radio stack resident *and* the
 * display allocating its DMA buffers. This is exactly where a device that
 * booted fine on the bench fails to bring up its UI. */
void test_connectivity_display_survives_radio( void )
{
    esp_err_t init_err = core2foraws_wifi_init();
    if ( init_err != ESP_OK )
    {
        TEST_IGNORE_MESSAGE( "Wi-Fi stack unavailable on this build" );
    }

    core2foraws_common_heap_stats_t radio_up = { 0 };
    core2foraws_common_heap_report( "RADIO_UP", &radio_up );

    /* Force a full display teardown and re-allocation while the radio owns its
     * share of internal DRAM. */
    esp_err_t deinit = core2foraws_display_deinit();
    esp_err_t reinit = core2foraws_display_init();

    esp_err_t touch_err = ESP_FAIL;
    if ( reinit == ESP_OK )
    {
        esp_lcd_touch_point_data_t points[ 1 ] = { 0 };
        uint8_t count = 0;
        touch_err = core2foraws_display_touch_data_get( points, &count, 1 );
    }

    ( void )core2foraws_wifi_deinit();
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    ESP_LOGI( TAG, "with radio up: dma_free=%u dma_largest=%u deinit=%s reinit=%s",
              ( unsigned )radio_up.dma_free,
              ( unsigned )radio_up.dma_largest_block,
              esp_err_to_name( deinit ), esp_err_to_name( reinit ) );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, deinit, "Display teardown must succeed" );
    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        TEST_LCD_DRAW_BUF_BYTES, radio_up.dma_largest_block,
        "Radio bring-up left too little contiguous DMA DRAM for a draw buffer" );
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, reinit,
        "Display must initialize while the radio stack is resident" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, touch_err,
                               "Touch must work while the radio is resident" );

    /* Restore a known-good display for the remainder of the suite. */
    TEST_ASSERT_NOT_NULL( core2foraws_display_ptr );
}

/* Repeated init must be idempotent rather than double-registering handlers or
 * leaking a netif each time an application retries bring-up. */
void test_connectivity_wifi_init_idempotent( void )
{
    esp_err_t first = core2foraws_wifi_init();
    if ( first != ESP_OK )
    {
        TEST_IGNORE_MESSAGE( "Wi-Fi stack unavailable on this build" );
    }

    core2foraws_common_heap_stats_t once = { 0 };
    core2foraws_common_heap_report( "WIFI_ONCE", &once );

    esp_err_t second = core2foraws_wifi_init();

    core2foraws_common_heap_stats_t twice = { 0 };
    core2foraws_common_heap_report( "WIFI_TWICE", &twice );

    const size_t extra = ( once.internal_free > twice.internal_free )
                             ? once.internal_free - twice.internal_free
                             : 0;

    ( void )core2foraws_wifi_deinit();
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    ESP_LOGI( TAG, "second wifi_init=%s extra_dram=%u",
              esp_err_to_name( second ), ( unsigned )extra );

    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, second,
                               "Repeated wifi_init must report success" );
    TEST_ASSERT_LESS_OR_EQUAL_UINT_MESSAGE(
        4096, extra, "Repeated wifi_init allocated a second copy of the stack" );
}

/* Connect/disconnect issued before the stack is up must report state instead of
 * dereferencing an uninitialized driver. */
void test_connectivity_ops_before_init( void )
{
    ( void )core2foraws_wifi_deinit();

    /* Valid arguments so a failure can only come from the uninitialized state. */
    esp_err_t connect_err = core2foraws_wifi_connect( "core2-test", NULL, 0 );
    esp_err_t disconnect_err = core2foraws_wifi_disconnect();

    ESP_LOGI( TAG, "before init: connect=%s disconnect=%s",
              esp_err_to_name( connect_err ), esp_err_to_name( disconnect_err ) );

    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        ESP_OK, connect_err,
        "connect() before init must fail rather than report success" );
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        ESP_OK, disconnect_err,
        "disconnect() before init must fail rather than report success" );
}

/* Malformed credentials and buffers must be rejected up front, before the
 * lifecycle lock or the radio is touched. */
void test_connectivity_arg_validation( void )
{
    char long_ssid[ MAX_SSID_LEN + 2 ];
    memset( long_ssid, 'a', sizeof( long_ssid ) - 1 );
    long_ssid[ sizeof( long_ssid ) - 1 ] = '\0';

    char long_pass[ MAX_PASSPHRASE_LEN + 2 ];
    memset( long_pass, 'a', sizeof( long_pass ) - 1 );
    long_pass[ sizeof( long_pass ) - 1 ] = '\0';

    wifi_ap_record_t record;
    uint16_t count = 1;
    uint16_t zero = 0;

    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_connect( NULL, NULL, 0 ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_connect( "", NULL, 0 ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_connect( long_ssid, NULL, 0 ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG,
                       core2foraws_wifi_connect( "core2-test", long_pass, 0 ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_scan( NULL, &count ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_scan( &record, NULL ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_scan( &record, &zero ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_state_get( NULL ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG, core2foraws_wifi_saved_ssid_get( NULL ) );

#ifdef CONFIG_CORE2FORAWS_WIFI_PROVISIONING
    char payload[ CORE2FORAWS_WIFI_PROV_PAYLOAD_LEN ];
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_ARG,
                       core2foraws_wifi_provisioning_payload_get( NULL, sizeof( payload ) ) );
    TEST_ASSERT_EQUAL( ESP_ERR_INVALID_SIZE,
                       core2foraws_wifi_provisioning_payload_get( payload, sizeof( payload ) - 1 ) );
#endif
}

/* Applications follow the link through the state API and event bits instead of
 * the driver, so both must agree across the init/deinit lifecycle. */
void test_connectivity_state_reporting( void )
{
    char ssid[ MAX_SSID_LEN + 1 ] = { 0 };
    core2foraws_wifi_state_t state = CORE2FORAWS_WIFI_STATE_CONNECTED;

    ( void )core2foraws_wifi_deinit();
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_wifi_state_get( &state ) );
    TEST_ASSERT_EQUAL_MESSAGE( CORE2FORAWS_WIFI_STATE_IDLE, state,
                               "State must be IDLE before init" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_ERR_INVALID_STATE,
                               core2foraws_wifi_saved_ssid_get( ssid ),
                               "saved_ssid_get before init must report state" );

    if ( core2foraws_wifi_init() != ESP_OK )
    {
        TEST_IGNORE_MESSAGE( "Wi-Fi stack unavailable on this build" );
    }

    state = CORE2FORAWS_WIFI_STATE_CONNECTED;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_wifi_state_get( &state ) );
    const EventBits_t bits = xEventGroupGetBits( wifi_event_group );
    const esp_err_t saved_err = core2foraws_wifi_saved_ssid_get( ssid );

    ( void )core2foraws_wifi_deinit();
    vTaskDelay( pdMS_TO_TICKS( 200 ) );

    ESP_LOGI( TAG, "after init: state=%d bits=0x%02x saved=%s", ( int )state,
              ( unsigned )bits, esp_err_to_name( saved_err ) );

    TEST_ASSERT_EQUAL_MESSAGE( CORE2FORAWS_WIFI_STATE_IDLE, state,
                               "init must not start the radio or connect" );
    TEST_ASSERT_EQUAL_MESSAGE(
        WIFI_DISCONNECTED_BIT,
        bits & ( WIFI_CONNECTED_BIT | WIFI_CONNECTING_BIT | WIFI_DISCONNECTED_BIT ),
        "Exactly the DISCONNECTED link bit must be set after init" );
    TEST_ASSERT_TRUE_MESSAGE( saved_err == ESP_OK || saved_err == ESP_ERR_NOT_FOUND,
                              "saved_ssid_get must succeed or report no saved network" );
    if ( saved_err == ESP_OK )
    {
        TEST_ASSERT_GREATER_THAN_UINT( 0, strlen( ssid ) );
    }
}

#ifdef CONFIG_CORE2FORAWS_WIFI_PROVISIONING
/* The provisioning payload is what the companion app scans, so an empty or
 * malformed string is a silent setup failure for the end user. The payload only
 * exists while a provisioning session is live, so this also pins the contract
 * that asking too early reports state rather than handing back a stub string. */
void test_connectivity_provisioning_string( void )
{
    esp_err_t init_err = core2foraws_wifi_init();
    if ( init_err != ESP_OK )
    {
        TEST_IGNORE_MESSAGE( "Wi-Fi stack unavailable on this build" );
    }

    char payload[ CORE2FORAWS_WIFI_PROV_PAYLOAD_LEN ] = { 0 };

    const esp_err_t before_session =
        core2foraws_wifi_provisioning_payload_get( payload, sizeof( payload ) );
    const core2foraws_wifi_prov_state_t state_before =
        core2foraws_wifi_provisioning_state_get();

    const esp_err_t start_err = core2foraws_wifi_provisioning_start();
    esp_err_t during_session = ESP_FAIL;
    core2foraws_wifi_prov_state_t state_during = CORE2FORAWS_WIFI_PROV_OFF;
    EventBits_t bits_during = 0;
    if ( start_err == ESP_OK )
    {
        during_session =
            core2foraws_wifi_provisioning_payload_get( payload, sizeof( payload ) );
        state_during = core2foraws_wifi_provisioning_state_get();
        bits_during = xEventGroupGetBits( wifi_event_group );
    }

    ESP_LOGI( TAG, "prov: before=%s start=%s during=%s payload=%s",
              esp_err_to_name( before_session ), esp_err_to_name( start_err ),
              esp_err_to_name( during_session ), payload );

    const esp_err_t stop_err = core2foraws_wifi_provisioning_stop();
    const core2foraws_wifi_prov_state_t state_after =
        core2foraws_wifi_provisioning_state_get();
    const EventBits_t bits_after = xEventGroupGetBits( wifi_event_group );
    ( void )core2foraws_wifi_deinit();
    vTaskDelay( pdMS_TO_TICKS( 300 ) );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_STATE, before_session,
        "Requesting the payload with no session must report state" );
    TEST_ASSERT_EQUAL( CORE2FORAWS_WIFI_PROV_OFF, state_before );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, start_err,
                               "Provisioning start must succeed" );
    TEST_ASSERT_EQUAL_MESSAGE( CORE2FORAWS_WIFI_PROV_WAITING, state_during,
                               "An open session with no phone must be WAITING" );
    TEST_ASSERT_TRUE_MESSAGE( bits_during & WIFI_PROVISIONING_BIT,
                              "PROVISIONING bit must be set during a session" );
    TEST_ASSERT_EQUAL( ESP_OK, stop_err );
    TEST_ASSERT_EQUAL( CORE2FORAWS_WIFI_PROV_OFF, state_after );
    TEST_ASSERT_FALSE_MESSAGE( bits_after & WIFI_PROVISIONING_BIT,
                               "PROVISIONING bit must clear when the session stops" );
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, during_session,
                               "Payload must be available during a session" );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        0, strlen( payload ), "Provisioning string must not be empty" );
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr( payload, "\"ver\"" ),
        "Provisioning payload must carry the scheme version field" );
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr( payload, "\"name\"" ),
        "Provisioning payload must carry the device service name" );
    TEST_ASSERT_NOT_NULL_MESSAGE(
        strstr( payload, "\"transport\"" ),
        "Provisioning payload must declare its transport" );
}
#endif /* CONFIG_CORE2FORAWS_WIFI_PROVISIONING */

#endif /* CONFIG_SOFTWARE_WIFI_SUPPORT */
