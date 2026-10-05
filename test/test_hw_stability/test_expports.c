#include <unity.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_EXPPORTS";

/* GPIO drive + reset on the expansion headers must be reliable. */
void test_expports_digital_io( void )
{
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_expports_digital_write( PORT_C_UART_TX_PIN,
                                                           true ) );
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_expports_digital_write( PORT_C_UART_TX_PIN,
                                                           false ) );
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_expports_pin_reset( PORT_C_UART_TX_PIN ) );

    bool level = false;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK, core2foraws_expports_digital_read( PORT_B_ADC_PIN, &level ),
        "Reading a header input pin should succeed (level is don't-care)" );
}

/* The ADC on Port B must convert without faulting; the pin floats so the
 * value is arbitrary but must be within the converter's range. */
void test_expports_adc_read( void )
{
    int raw = -1;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_adc_read( &raw ) );
    TEST_ASSERT_TRUE_MESSAGE( raw >= 0 && raw < 8192,
                              "Raw ADC value should be within converter range" );

    uint32_t mv = 0;
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_adc_mv_read( &mv ) );
    ESP_LOGI( TAG, "adc raw=%d mv=%u", raw, ( unsigned )mv );
    TEST_ASSERT_TRUE_MESSAGE( mv < 4000,
                              "ADC millivolts should be within the input range" );
}

void test_expports_dac_write( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_dac_mv_write( 1650 ) );
    vTaskDelay( pdMS_TO_TICKS( 10 ) );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_dac_mv_write( 0 ) );
}

/* The external Port A I2C bus must open, register/remove a device, and close
 * cleanly (no device is attached, so no transfer is attempted). */
void test_expports_external_i2c_lifecycle( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_i2c_begin() );

    i2c_master_dev_handle_t dev = NULL;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK,
        core2foraws_expports_i2c_device_add( 0x68, 100000, &dev ),
        "Adding a device to the external bus should succeed" );
    TEST_ASSERT_NOT_NULL( dev );
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_i2c_device_remove( dev ) );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_i2c_close() );
}

/* UART on Port C must install, transmit, and drain without blocking forever. */
void test_expports_uart_lifecycle( void )
{
    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_uart_begin( 115200 ) );

    const char *msg = "core2 uart selftest\n";
    size_t wrote = 0;
    TEST_ASSERT_EQUAL( ESP_OK,
                       core2foraws_expports_uart_write( msg, 20, &wrote ) );
    TEST_ASSERT_EQUAL_UINT( 20, wrote );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_expports_uart_send_finished() );
}
