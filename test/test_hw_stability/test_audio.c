#include <unity.h>

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "core2foraws.h"
#include "test_hw.h"

static const char *TAG = "TEST_AUDIO";

/* Speaker: power the amp, push a short buffer of silence, power down. */
void test_audio_speaker_lifecycle( void )
{
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_audio_speaker_enable( true ),
                               "Speaker enable should power the amp and I2S" );

    uint8_t silence[ 1024 ] = { 0 };
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK,
        core2foraws_audio_speaker_write( silence, sizeof( silence ) ),
        "Writing a silent buffer should succeed" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( false ) );
}

/* Microphone: enable, capture a few samples, disable. */
void test_audio_mic_lifecycle( void )
{
    TEST_ASSERT_EQUAL_MESSAGE( ESP_OK, core2foraws_audio_mic_enable( true ),
                               "Mic enable should init the I2S RX path" );

    int8_t samples[ 512 ] = { 0 };
    size_t was_read = 0;
    TEST_ASSERT_EQUAL_MESSAGE(
        ESP_OK,
        core2foraws_audio_mic_read( samples, sizeof( samples ), &was_read ),
        "Mic read should succeed" );
    ESP_LOGI( TAG, "mic read %u bytes", ( unsigned )was_read );
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE( 0, was_read,
                                           "Mic should return captured samples" );

    TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_mic_enable( false ) );
}

/* Speaker and mic share GPIO0. Rapidly alternating them must reconfigure the
 * I2S bus cleanly every time without wedging or leaking. */
void test_audio_speaker_mic_mutual_exclusion( void )
{
    uint8_t silence[ 512 ] = { 0 };
    int8_t samples[ 256 ] = { 0 };
    size_t was_read = 0;

    for ( int i = 0; i < 4; i++ )
    {
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( true ) );
        TEST_ASSERT_EQUAL(
            ESP_OK, core2foraws_audio_speaker_write( silence, sizeof( silence ) ) );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_speaker_enable( false ) );

        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_mic_enable( true ) );
        TEST_ASSERT_EQUAL(
            ESP_OK,
            core2foraws_audio_mic_read( samples, sizeof( samples ), &was_read ) );
        TEST_ASSERT_EQUAL( ESP_OK, core2foraws_audio_mic_enable( false ) );
    }
}
