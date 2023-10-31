#include <Arduino.h>

#include "driver/rtc_io.h"
#include "gps.h"
#include "passwords.h"
#include "sensors.h"
#include "usb.h"

#include "gui.h"

#define SCREEN_ON_TIME 60 * 1
#define GPS_ON_TIME_MAX 60 * 5
#define GPS_DEEPSLEEP 60 * 60

#define WATCHDOG_TICK 1

RTC_DATA_ATTR uint16_t batteryMilliVolts = 0;
RTC_DATA_ATTR float batteryPercentage = 0.0;
RTC_DATA_ATTR bool charging = false;

int16_t watchDogCountdown = SCREEN_ON_TIME;

constexpr uint32_t DEEPSLEEP_INTERUPT_BITMASK =
	(1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON) | (1UL << VUSB_MON);

void enterDeepSleep(uint64_t deepSleepTime) {
	esp_sleep_enable_ext1_wakeup(DEEPSLEEP_INTERUPT_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	esp_sleep_enable_timer_wakeup(deepSleepTime * 1000000ULL);
	esp_deep_sleep_start();
}

void calculateBatteryPercentage() {
	const uint16_t batteryCurve[3][12] = {{0, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4150, 9999},
										  {0, 0, 13, 22, 39, 53, 64, 78, 92, 100, 100, 100}, // discharge
										  {0, 0, 0, 13, 22, 39, 53, 64, 79, 94, 100, 100}};	 // charge

	// Determine the size of the lookup table
	uint8_t tableSize = sizeof(batteryCurve[0]) / sizeof(batteryCurve[0][0]);

	// Iterate through the lookup table to find the two lookup values we are between
	for (uint8_t index = 0; index < tableSize - 1; index++) {
		// Check if the battery voltage is within the current range
		if (batteryMilliVolts <= batteryCurve[0][index + 1] && batteryMilliVolts > batteryCurve[0][index]) {
			// Get the x and y values for interpolation
			uint16_t x0 = batteryCurve[0][index];
			uint16_t x1 = batteryCurve[0][index + 1];
			uint8_t y0 = batteryCurve[charging + 1][index];
			uint8_t y1 = batteryCurve[charging + 1][index + 1];

			// Perform linear interpolation to calculate the battery percentage
			uint8_t rawPercentage = (y0 + ((y1 - y0) * (batteryMilliVolts - x0)) / (x1 - x0));

			if (batteryPercentage == 0.0) {
				batteryPercentage = (float)(rawPercentage);
			} else {
				batteryPercentage = batteryPercentage * 0.9 + (float)(rawPercentage)*0.1;
			}
			snprintf(batteryText, sizeof(batteryText), "%3.0f%%", batteryPercentage);

			break;
		}
	}
	return;
}

void watchDogTask(void *parameter) {
	pinMode(VUSB_MON, INPUT);
	pinMode(VBAT_SENSE_EN, OUTPUT);
	pinMode(VBAT_SENSE, INPUT);

	while (true) {
		TickType_t xLastWakeTime = xTaskGetTickCount();

		digitalWrite(VBAT_SENSE_EN, HIGH);
		vTaskDelay(100 / portTICK_PERIOD_MS);
		batteryMilliVolts = analogReadMilliVolts(VBAT_SENSE) * VBAT_SENSE_SCALE;
		digitalWrite(VBAT_SENSE_EN, LOW);
		charging = digitalRead(VUSB_MON);

		calculateBatteryPercentage();

		if (charging || resetWatchdog) {
			watchDogCountdown = SCREEN_ON_TIME;
			resetWatchdog = false;
		} else {
			watchDogCountdown -= WATCHDOG_TICK;
		}
		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(WATCHDOG_TICK * 1000));
	}
}

enum DeviceStates : uint8_t { STARTUP, UI_MODE, GPS_SYNC_MODE, ENTER_DEEPSLEEP };
DeviceStates deviceState = STARTUP;

void setup() { Serial.begin(); }

void loop() {
	switch (deviceState) {
	case STARTUP:
		ESP_LOGI("State Machine", "STARTUP");
		pinMode(OUTPUT_EN, OUTPUT);
		digitalWrite(OUTPUT_EN, HIGH);
		xTaskCreate(gpsTask, "gpsTask", 10000, NULL, 1, NULL);
		xTaskCreate(watchDogTask, "watchDogTask", 2000, NULL, 1, NULL);
		xTaskCreate(usbTask, "usbTask", 10000, NULL, 1, NULL);
		setupButtons();

		if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
			deviceState = GPS_SYNC_MODE;
		} else {
			deviceState = UI_MODE;
		}
		break;

	case UI_MODE:
		ESP_LOGI("State Machine", "UI_MODE");
		xTaskCreate(sensorTask, "sensorTask", 10000, NULL, 1, NULL);
		xTaskCreate(guiTask, "guiTask", 10000, NULL, 2, NULL);

		while (watchDogCountdown > 0) {
			vTaskDelay(WATCHDOG_TICK * 1000 / portTICK_PERIOD_MS);
		}

		analogWrite(BACKLIGHT, 0);
		deviceState = ENTER_DEEPSLEEP;
		break;

	case GPS_SYNC_MODE:
		ESP_LOGI("State Machine", "GPS_SYNC_MODE");
		watchDogCountdown = GPS_ON_TIME_MAX;

		while ((gps.hdop.hdop() > 1.0 || gps.sentencesWithFix() == 0) && watchDogCountdown > 0 && buttonState == NOT_PRESSED) {
			vTaskDelay(50 / portTICK_PERIOD_MS);
		}

		if (buttonState > NOT_PRESSED) {
			buttonState = NOT_PRESSED;
			deviceState = UI_MODE;

		} else {

			ESP_LOGI("Adafruit IO", "Connecting...");
			io.connect();

			// wait for a connection
			for (size_t i = 0; i < 10 && io.status() < AIO_CONNECTED; i++) {
				vTaskDelay(500 / portTICK_PERIOD_MS);
			}

			ESP_LOGI("Adafruit IO", "%s", io.statusText());

			if (io.status() == AIO_CONNECTED) {
				ESP_LOGI("Adafruit IO", "sending to feed");

				AdafruitIO_Feed *deepsleep = io.feed("batteryvoltage");
				deepsleep->save(batteryMilliVolts);

				AdafruitIO_Feed *location = io.feed("location");
				location->save(gps.timeToFirstFix(), gps.location.lat(), gps.location.lng(), gps.altitude.meters());

				AdafruitIO_Feed *gpssynctime = io.feed("gpssynctime");
				gpssynctime->save(millis());

				io.run();
			}

			deviceState = ENTER_DEEPSLEEP;
		}

		break;

	case ENTER_DEEPSLEEP:
		digitalWrite(OUTPUT_EN, LOW);
		ESP_LOGI("State Machine", "DEEPSLEEP");
		enterDeepSleep(GPS_DEEPSLEEP);
		break;
	}
}