#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_freertos_hooks.h>
#include <esp_system.h>
#include <esp_spi_flash.h>
#include <esp_log.h>

#include "core2foraws.h"

static const char *TAG = "MAIN";

void app_main( void )
{
	ESP_LOGI( TAG, "Starting application" );
	
	core2foraws_init();

	/*
	 
	Start your code here

	*/
}