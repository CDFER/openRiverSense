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

float pH = 0.0;			 // 0 - 14
float orp = 0.0;		 // -400 - 400mV
float tds = 0.0;		 //
float temperature = 0.0; // 0-50c

#define POINTS 3
#define ORDER 2

class CalibratedSensor {
public:
	CalibratedSensor() {}

	double convertFromRaw(double raw) {
		avRaw = 0.1 * raw + 0.9 * avRaw;
		value = calibrationCoeffs[0] * pow(avRaw, 2) + calibrationCoeffs[1] * avRaw + calibrationCoeffs[2];
		return value;
	}

	void fitCurveFromPoints() {
		double calibrationPoints[2][POINTS] = {
			{-200.0, 0.0, 200.0}, // raw input mV (X AXIS)
			{4.00, 6.88, 9.23},	  // output (Y AXIS)
		};

		// double x[POINTS] = {-5, 0, 5, 10};
		// double y[POINTS] = {0, 5, 10, 15};

		// double x[POINTS] = {0, 5, 10};
		// double y[POINTS] = {0, 5, 10};

		double x[POINTS] = {0, 200, 400};
		double y[POINTS] = {4.00, 6.88, 9.23};

		int err = fitCurve(ORDER, POINTS, x, y, ORDER + 1, calibrationCoeffs);
		Serial.printf("err = %i A:%0.3f,B:%0.3f,C:%0.3f\n", err, calibrationCoeffs[0], calibrationCoeffs[1],
					  calibrationCoeffs[2]);
	}

	double value = 0.0;

private:
	double calibrationCoeffs[ORDER + 1];

	double avRaw = 0.0;
};

double pHCalibrationPoints[2][POINTS] = {
	{-200.0, 0.0, 200.0}, // raw input mV (X AXIS)
	{4.00, 6.88, 9.23},	  // output (Y AXIS)
};

CalibratedSensor pHSensor;

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
	pH = adcRaw[1] - adcRaw[2];
	orp = adcRaw[0] - adcRaw[2];
	tds = tdsRaw;
}

void sensorTask(void *parameter) {
	vTaskDelay(10 / portTICK_PERIOD_MS);
	// setupEeprom();
	setupADC();
	setupTemperature();

	while (true) {

		fetchADC();
		measureCapacitance();
		fetchTemperature();
		convertData();

		//Serial.printf("%i	%0.1f	%0.1f	%0.1f	%0.2f\n", millis(), pH, orp, tds, temperature);

		// pHSensor.fitCurveFromPoints();

		// Serial.println(pHSensor.convertFromRaw(-5.0));

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}