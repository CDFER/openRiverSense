#include <Arduino.h>

#include "gps.h"
#include "gui.h"
#include "usb.h"
#include "sensors.h"



constexpr uint32_t DEEPSLEEP_INTERUPT_BITMASK = (1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON);
void enterDeepSleep() {
	esp_sleep_enable_ext1_wakeup(DEEPSLEEP_INTERUPT_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
	//esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	//esp_sleep_enable_timer_wakeup(30*1000*1000);
	esp_deep_sleep_start();
}

void setup() {
	Serial.begin(115200);

	// Create tasks
	xTaskCreate(guiTask, "guiTask", 10000, NULL, 2, NULL);
	xTaskCreate(gpsTask, "gpsTask", 10000, NULL, 2, NULL);
	xTaskCreate(usbTask, "usbTask", 10000, NULL, 2, NULL);

	// enterDeepSleep();
}

void loop() {
	// double tempC = voltageToTemperature(multiSampleADCmV(JST_IO_2_1, 1000));
	// double pH = voltageTopH(multiSampleADCmV(JST_IO_1_2, 10000));
	// double orp = voltageToORP(multiSampleADCmV(JST_IO_1_2, 10000));

	vTaskDelay(10 / portTICK_PERIOD_MS);
}