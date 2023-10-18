#include <Arduino.h>

#include "gps.h"
#include "gui.h"
#include "sensors.h"
#include "usb.h"

#define SCREEN_ON_TIME 15 * 1

#define GPS_DEEPSLEEP 60 * 59
#define GPS_ON_TIME_AFTER_FIX 60
#define GPS_ON_TIME_MAX 60 * 30

constexpr uint32_t DEEPSLEEP_INTERUPT_BITMASK =
	(1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON) | (1UL << VUSB_MON);
void enterDeepSleep(uint64_t deepSleepTime) {
	ESP_LOGI("", "enterDeepSleep at %is", millis() / 1000);
	esp_sleep_enable_ext1_wakeup(DEEPSLEEP_INTERUPT_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	esp_sleep_enable_timer_wakeup(deepSleepTime * 1000000ULL);
	esp_deep_sleep_start();
}

void IRAM_ATTR wakeToUI() { ESP.restart(); }

void setup() {
	Serial.begin();

	esp_sleep_wakeup_cause_t wakeup_reason;
	wakeup_reason = esp_sleep_get_wakeup_cause();

	switch (wakeup_reason) {
	case ESP_SLEEP_WAKEUP_TIMER:
		setCpuFrequencyMhz(80); // Lower Power Consumption
		attachInterrupt(WAKE_BUTTON, wakeToUI, HIGH);
		attachInterrupt(UP_BUTTON, wakeToUI, HIGH);
		attachInterrupt(DOWN_BUTTON, wakeToUI, HIGH);
		attachInterrupt(VUSB_MON, wakeToUI, HIGH);

		while (gps.fixQuality() < 1 && millis() < (GPS_ON_TIME_MAX * 1000)) {
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}

		vTaskDelay(GPS_ON_TIME_AFTER_FIX * 1000 / portTICK_PERIOD_MS);

		enterDeepSleep(15);

		break;

	default:
		// UI Mode
		pinMode(VUSB_MON, INPUT);
		pinMode(OUTPUT_EN, OUTPUT);
		digitalWrite(OUTPUT_EN, HIGH);

		xTaskCreate(guiTask, "guiTask", 10000, NULL, 2, NULL);
		xTaskCreate(gpsTask, "gpsTask", 10000, NULL, 2, NULL);
		xTaskCreate(usbTask, "usbTask", 10000, NULL, 2, NULL);
		// xTaskCreate(sensorTask, "sensorTask", 10000, NULL, 1, NULL);

		while (millis() < (SCREEN_ON_TIME * 1000) || digitalRead(VUSB_MON)) {
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}

		enterDeepSleep(15);
		break;
	}
}

void loop() { vTaskDelay(100 / portTICK_PERIOD_MS); }