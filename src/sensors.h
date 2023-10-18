#include "ADS1X15.h"
#include "I2C_eeprom.h"

#include <Arduino.h>
#include <curveFitting.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

ADS1015 ADS(0x48);
I2C_eeprom ee(0x50, I2C_DEVICESIZE_24LC64);

void sensorTask(void *parameter) {
	while (true) {

		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}