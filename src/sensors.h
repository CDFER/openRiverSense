#include <Arduino.h>

#include "ADS1X15.h"
#include "I2C_eeprom.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <curveFitting.h>

ADS1015 ADS(0x48);
I2C_eeprom ee(0x50, I2C_DEVICESIZE_24LC64);

OneWire oneWireBus;
DallasTemperature dallasTemperatureBus;

void sensorTask(void *parameter) {
	while (true) {

		vTaskDelay(10 / portTICK_PERIOD_MS);
	}
}