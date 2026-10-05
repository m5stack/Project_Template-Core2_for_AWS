#include <unity.h>

#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static void _dummy_cb( enum core2foraws_button_btns button, press_event_t event )
{
    (void)button;
    (void)event;
}

/* Registering and unregistering callbacks on valid buttons must succeed. */
void test_button_register_valid( void )
{
    const enum core2foraws_button_btns btns[] = { BUTTON_LEFT, BUTTON_MIDDLE,
                                                  BUTTON_RIGHT };
    for ( size_t i = 0; i < sizeof( btns ) / sizeof( btns[ 0 ] ); i++ )
    {
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK,
            core2foraws_button_register_callback(
                btns[ i ], PRESS | RELEASE | LONGPRESS, _dummy_cb ),
            "Registering a callback on a valid button should succeed" );
        TEST_ASSERT_EQUAL_MESSAGE(
            ESP_OK,
            core2foraws_button_unregister_callback(
                btns[ i ], PRESS | RELEASE | LONGPRESS ),
            "Unregistering a callback should succeed" );
    }
}

/* Bad arguments must be rejected, not crash the poll task. */
void test_button_register_invalid_args( void )
{
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_button_register_callback( BUTTON_LEFT, PRESS, NULL ),
        "A NULL callback must be rejected" );

    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_ERR_INVALID_ARG,
        core2foraws_button_register_callback( ( enum core2foraws_button_btns )99,
                                              PRESS, _dummy_cb ),
        "An out-of-range button must be rejected" );
}

/* Re-initialization is documented as idempotent. */
void test_button_init_idempotent( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_button_init() );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_button_init() );
}
