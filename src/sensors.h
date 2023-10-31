#include <Arduino.h>

#include "ADS1X15.h"
#include "I2C_eeprom.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <curveFitting.h>

I2C_eeprom eeprom(0x50, I2C_DEVICESIZE_24LC64);

ADS1015 adc(0x48);
uint8_t adcChannel = 0;
float adcRaw[4] = {0, 0, 0, 0};
float tdsRaw = 0.0;

OneWire oneWireBus(ONEWIRE);
DallasTemperature owTemp(&oneWireBus);
DeviceAddress deviceAddress;
#define TEMP_RESOLUTION 12
#define TEMP_CONVERSION_MS 750 / (1 << (12 - TEMP_RESOLUTION))
unsigned long lastTempRequest = 0;
float temperature = 0.0;

#define POINTS 3
#define ORDER 2

class CalibratedSensor {
public:
	CalibratedSensor(const char *sensorName, uint16_t eepromAddress) {
		_eepromAddress = eepromAddress;
		_sensorName = sensorName;
	}

	double convertFromRaw(double raw) {
		if (!importedDataPoints) {
			importCalibrationFromEeprom();
		}
		_raw = raw;
		value = coefficients[0] * pow(_raw, 2) - coefficients[1] * _raw + coefficients[2];
		return value;
	}

	void fitCurveFromPoints() {
		double x[POINTS];
		double y[POINTS];

		for (int i = 0; i < POINTS; i++) {
			x[i] = calibrationPoints[0][i]; // x values
			y[i] = calibrationPoints[1][i]; // y values
		}

		fitCurve(ORDER, POINTS, x, y, ORDER + 1, coefficients);

		Serial.println();
		Serial.println(_sensorName);
		Serial.printf("x:	%0.2f	%0.2f	%0.2f\n", x[0], x[1], x[2]);
		Serial.printf("y:	%0.2f	%0.2f	%0.2f\n", y[0], y[1], y[2]);
		Serial.printf("%0.6fx^2 + %0.6fx + %0.6f\n", coefficients[0], coefficients[1], coefficients[2]);
	}

	void setCalibrationPoint(uint8_t index, double y) {
		calibrationPoints[0][index] = _raw;
		calibrationPoints[1][index] = y;

		eeprom.updateBlock(_eepromAddress, (uint8_t *)calibrationPoints, sizeof(calibrationPoints));
		fitCurveFromPoints();
	}

	void importCalibrationFromEeprom() {
		eeprom.readBlock(_eepromAddress, (uint8_t *)calibrationPoints, sizeof(calibrationPoints));
		fitCurveFromPoints();
		importedDataPoints = true;
	}

	double value = 0.0;

private:
	double _raw;
	double calibrationPoints[2][POINTS];
	double coefficients[ORDER + 1];
	bool importedDataPoints = false;
	uint16_t _eepromAddress;
	const char *_sensorName;
};

CalibratedSensor pH("pH", 0x0000);
CalibratedSensor orp("orp", 0x0100);
CalibratedSensor tds("tds", 0x0200);

void setupADC() {
	pinMode(WIRE_INT, OUTPUT); // capacitance drive pin
	adc.begin(WIRE_SDA, WIRE_SCL);
	adc.setWireClock(400000);
}

void fetchADC() {
	adc.setGain(4);			 // ±1.024 V LSB = 0.5 mV
	adc.setDataRate(0);		 // 128 SPS
	adc.setMode(1);			 // Single-shot mode
	adc.readADC(adcChannel); // Trigger first read

	for (size_t i = 0; i < 60; i++) {
		if (adc.isReady()) {
			adcRaw[adcChannel] = (0.1 * adc.toVoltage(adc.getValue()) * 1000) + (0.9 * adcRaw[adcChannel]);
			adcChannel = (adcChannel + 1) % 3; // cycle from channel 0-2
			adc.readADC(adcChannel);		   // request next channel
		}
		vTaskDelay((1000 / 128) / portTICK_PERIOD_MS);
	}
}

void measureCapacitance() {
	adc.setGain(8);		// ±0.512 V LSB = 0.25mV
	adc.setDataRate(7); // 3300 SPS
	adc.setMode(0);		// Continuous mode
	adc.readADC(3);		// Trigger first read

	int16_t raw = 0;
	digitalWrite(WIRE_INT, HIGH);

	while (raw < 2000) {
		raw = adc.getValue();
	}

	digitalWrite(WIRE_INT, LOW);
	uint32_t start = millis();

	while (raw > 1800) {
		raw = adc.getValue();
		// vTaskDelay(10 / portTICK_PERIOD_MS);
	}

	uint32_t end = millis();
	tdsRaw = 0.5 * (float)(end - start) + 0.5 * tdsRaw;
}

void setupEeprom() {
	eeprom.begin(WIRE_SDA, WIRE_SCL);
	if (!eeprom.isConnected()) {
		Serial.println("eeprom not connected");
	}
}

void setupTemperature() {
	owTemp.begin();
	owTemp.setWaitForConversion(false);
	owTemp.setResolution(TEMP_RESOLUTION);
	owTemp.getAddress(deviceAddress, 0);
	owTemp.requestTemperatures();
	lastTempRequest = millis();
}

void fetchTemperature() {
	if (millis() - lastTempRequest >= TEMP_CONVERSION_MS) {
		temperature = 0.5 * owTemp.getTempC(deviceAddress) + 0.5 * temperature;
		owTemp.requestTemperatures();
		lastTempRequest = millis();
	}
}

void convertData() {
	pH.convertFromRaw(adcRaw[1] - adcRaw[2]);
	orp.convertFromRaw(adcRaw[0] - adcRaw[2]);
	tds.convertFromRaw(tdsRaw);
}

void sensorTask(void *parameter) {
	vTaskDelay(5000 / portTICK_PERIOD_MS);
	setupEeprom();
	setupADC();
	setupTemperature();

	while (true) {

		fetchADC();
		// measureCapacitance();
		fetchTemperature();
		convertData();

		// Serial.printf("%i	%0.1f	%0.1f	%0.1f	%0.2f\n", millis(), pH, orp, tds, temperature);

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}