#include <Arduino.h>
#include <FFat.h>
#include <FS.h>

#include "SD.h"
#include "gps.h"
#include "gui.h"
#include "sensors.h"
#include "usb.h"

#define SCREEN_ON_TIME 30*40

#define GPS_DEEPSLEEP 60 * 59
#define GPS_ON_TIME_AFTER_FIX 60
#define GPS_ON_TIME_MAX 60 * 30

constexpr uint32_t DEEPSLEEP_INTERUPT_BITMASK = (1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON);
void enterDeepSleep(uint64_t deepSleepTime) {
	ESP_LOGI("", "enterDeepSleep at %is", millis() / 1000);
	esp_sleep_enable_ext1_wakeup(DEEPSLEEP_INTERUPT_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	esp_sleep_enable_timer_wakeup(deepSleepTime * 1000000ULL);
	esp_deep_sleep_start();
}

void setup() {
	Serial.begin(115200);

	esp_sleep_wakeup_cause_t wakeup_reason;

	wakeup_reason = esp_sleep_get_wakeup_cause();

	switch (wakeup_reason) {
	case ESP_SLEEP_WAKEUP_TIMER:
		//Low Power Mode
		ESP_LOGV("Low Power Mode", "");
		setCpuFrequencyMhz(80);	 // Set CPU frequency to boost when needed
		gpsLowPowerSync(GPS_ON_TIME_AFTER_FIX, GPS_ON_TIME_MAX);
		enterDeepSleep(GPS_DEEPSLEEP);

		// break;

	default:
		// UI Mode
		ESP_LOGV("UI Mode", "");
		xTaskCreate(guiTask, "guiTask", 10000, NULL, 2, NULL);
		xTaskCreate(gpsTask, "gpsTask", 10000, NULL, 2, NULL);
		xTaskCreate(usbTask, "usbTask", 10000, NULL, 2, NULL);

		while (millis() < (SCREEN_ON_TIME * 1000)) {
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}

		enterDeepSleep(10);
		break;
	}
}

void loop() {

	vTaskDelay(10 / portTICK_PERIOD_MS);
}