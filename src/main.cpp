#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"

#include "driver/rtc_io.h"
#include "gps.h"
#include "sensors.h"
#include "usb.h"

// Todo List
//- Add assist now functionality

TaskHandle_t sensorTaskHandle = NULL;

#include "gui.h"

#define SCREEN_ON_TIME 60 * 1				//1min
#define GPS_ON_TIME_MIN 60 * 1				//1min
#define GPS_ON_TIME_MAX_OFF_HOURS 60 * 3	//3mins
#define GPS_ON_TIME_MAX_WORK_HOURS 60 * 12	//12mins
#define GPS_DEEPSLEEP_SECONDS 8 * 60 * 60	//8hrs
#define MINIMUM_BATTERY_WAKEUP 3700			//3.7V
#define MINIMUM_BATTERY 3300				//3.3V

RTC_DATA_ATTR uint16_t batteryMilliVolts = 0;
RTC_DATA_ATTR float batteryPercentage = 0.0;
RTC_DATA_ATTR bool charging = false;
RTC_DATA_ATTR float averageGPSTimeToLocationFixSeconds = 0.0;

int16_t screenOnCountdown = SCREEN_ON_TIME;
float gpsHDOPThreshold = 1.5;
int16_t screenTime = 0;
int16_t lightSleepTime = 0;
bool workingHours = true;

enum DeviceStates : uint8_t { STARTUP,
							  UI_MODE,
							  LIGHT_SLEEP,
							  ENTER_DEEPSLEEP };
DeviceStates deviceState = STARTUP;
TaskHandle_t guiTaskHandle = NULL;

constexpr uint32_t DEEPSLEEP_INTERUPT_BITMASK =
	(1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON) | (1UL << VUSB_MON);

void enterDeepSleep(uint64_t deepSleepTimeSeconds = GPS_DEEPSLEEP_SECONDS) {
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */

	int32_t DEEPSLEEP_INTERUPT_BITMASK = (1UL << WAKE_BUTTON) | (1UL << UP_BUTTON) | (1UL << DOWN_BUTTON);

	if (!charging) {
		DEEPSLEEP_INTERUPT_BITMASK = DEEPSLEEP_INTERUPT_BITMASK | (1UL << VUSB_MON);
	}

	esp_sleep_enable_ext1_wakeup(DEEPSLEEP_INTERUPT_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
	// esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
	if (batteryMilliVolts > MINIMUM_BATTERY_WAKEUP) {
		if (!workingHours) {
			time_t currentEpoch;
			time(&currentEpoch);
			struct tm *timeInfo = localtime(&currentEpoch);

			// Calculate the seconds until 09:00
			uint64_t secondsUntilTarget = (9 * 3600 - (timeInfo->tm_hour * 3600 + timeInfo->tm_min * 60 + timeInfo->tm_sec)) % (24 * 3600);

			if (secondsUntilTarget < 0) {
				secondsUntilTarget += 24 * 3600;
			}

			printf("Seconds until 09:00: %d\n", secondsUntilTarget);
		}

		esp_sleep_enable_timer_wakeup(deepSleepTimeSeconds * uS_TO_S_FACTOR);
	}
	esp_deep_sleep_start();
}

void calculateBatteryPercentage() {
	const uint16_t batteryCurve[3][13] = { { 0, 3300, 3350, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200, 9999 },
										   { 0, 0, 0, 13, 21, 39, 53, 64, 78, 92, 100, 100, 100 },	// discharge
										   { 0, 0, 0, 0, 1, 13, 22, 39, 58, 70, 85, 100, 100 } };	// charge

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
				batteryPercentage = batteryPercentage * 0.9 + float(rawPercentage) * 0.1;
			}
			snprintf(batteryText, sizeof(batteryText), "%3.0f%%", batteryPercentage);

			break;
		}
	}
}

void updateBatteryVoltage() {
	digitalWrite(VBAT_SENSE_EN, HIGH);

	uint32_t sum = 0;
	for (int i = 0; i < 10; i++) {
		vTaskDelay(1 / portTICK_PERIOD_MS);
		sum += analogReadMilliVolts(VBAT_SENSE) * VBAT_SENSE_SCALE;
	}

	digitalWrite(VBAT_SENSE_EN, LOW);
	batteryMilliVolts = sum / 10;

	charging = digitalRead(VUSB_MON);

	if (deviceState == UI_MODE) {
		calculateBatteryPercentage();
	}

	ESP_LOGV("Battery", "%imV", batteryMilliVolts);
}

void batteryTask(void *parameter) {
	while (true) {
		vTaskDelay(10000 / portTICK_PERIOD_MS);
		updateBatteryVoltage();
	}
}

void setup() {
	pinMode(VUSB_MON, INPUT);
	pinMode(VBAT_SENSE_EN, OUTPUT);
	pinMode(VBAT_SENSE, INPUT);

	updateBatteryVoltage();

	if (batteryMilliVolts > MINIMUM_BATTERY) {
		deviceState == STARTUP;
		Serial.begin();
	} else {
		enterDeepSleep();
	}
}

void loop() {
	TickType_t deviceStateWakeTime = xTaskGetTickCount();
	switch (deviceState) {
		case STARTUP:
			ESP_LOGI("State Machine", "STARTUP");
			pinMode(OUTPUT_EN, OUTPUT);
			digitalWrite(OUTPUT_EN, HIGH);
			xTaskCreate(gpsTask, "gpsTask", 10000, NULL, 1, NULL);
			xTaskCreate(batteryTask, "batteryTask", 2000, NULL, 2, NULL);
			xTaskCreate(guiTask, "guiTask", 10000, NULL, 1, &guiTaskHandle);
			xTaskCreate(sensorTask, "sensorTask", 10000, NULL, 3, &sensorTaskHandle);
			xTaskCreate(usbTask, "usbTask", 10000, NULL, 2, NULL);

			if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
				deviceState = LIGHT_SLEEP;
				gpsHDOPThreshold = 5.0;
				vTaskSuspend(guiTaskHandle);
				ESP_LOGI("State Machine", "LIGHT_SLEEP");
			} else {
				deviceState = UI_MODE;
				gpsHDOPThreshold = 1.0;
				buttonPressed = true;
				ESP_LOGI("State Machine", "UI_MODE");
			}
			break;

		case UI_MODE:
			if (buttonPressed) {
				screenOnCountdown = SCREEN_ON_TIME;
				buttonPressed = false;
				vTaskResume(guiTaskHandle);
				xTaskDelayUntil(&deviceStateWakeTime, pdMS_TO_TICKS(1000));
			} else if (screenOnCountdown <= 0) {
				deviceState = LIGHT_SLEEP;
				updateScreenBrightness(false);
				vTaskSuspend(guiTaskHandle);
			} else {
				screenOnCountdown -= 1;
				xTaskDelayUntil(&deviceStateWakeTime, pdMS_TO_TICKS(1000));
			}
			screenTime += 1;
			break;

		case LIGHT_SLEEP:
			if (buttonPressed) {
				deviceState = UI_MODE;

			} else if (gps.hdop.isValid() && gps.hdop.hdop() < gpsHDOPThreshold && (millis() / 1000) > GPS_ON_TIME_MIN) {
				deviceState = ENTER_DEEPSLEEP;
			} else if (workingHours && (millis() / 1000) > GPS_ON_TIME_MAX_WORK_HOURS) {
				deviceState = ENTER_DEEPSLEEP;
			} else if (!workingHours && (millis() / 1000) > GPS_ON_TIME_MAX_OFF_HOURS) {
				deviceState = ENTER_DEEPSLEEP;
			} else {
				xTaskDelayUntil(&deviceStateWakeTime, pdMS_TO_TICKS(1000));
			}
			lightSleepTime += 1;
			break;

		case ENTER_DEEPSLEEP:
			saveAnalyticsToFile(batteryMilliVolts, screenTime, lightSleepTime);
			digitalWrite(OUTPUT_EN, LOW);
			ESP_LOGI("State Machine", "DEEPSLEEP");
			enterDeepSleep(GPS_DEEPSLEEP_SECONDS);
			break;
	}
	vTaskDelay(100 / portTICK_PERIOD_MS);
}