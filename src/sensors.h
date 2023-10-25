#include <Arduino.h>

#include "ADS1X15.h"
#include "I2C_eeprom.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <curveFitting.h>

I2C_eeprom eeprom(0x50, I2C_DEVICESIZE_24LC64);

ADS1015 adc(0x48);
volatile bool adcReady = false;
uint8_t adcChannel = 0;
float adcRaw[4] = {0, 0, 0, 0};
void IRAM_ATTR adcISR() { adcReady = true; }

OneWire oneWireBus(ONEWIRE);
DallasTemperature temperatureSensor(&oneWireBus);
DeviceAddress tempDeviceAddress;
uint8_t resolution = 12;
unsigned long lastTempRequest = 0;
int delayInMillis = 0;
float temperatureRaw = 0;

float pH = 0.0;			 // 0 - 14
float orp = 0.0;		 // -400 - 400mV
float nitrate = 0.0;	 // 0 - 500mg/L
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
	adc.begin(WIRE_SDA, WIRE_SCL);
	adc.setGain(4);
	adc.setDataRate(0);

	// set the thresholds to that they don't trigger the interrupt pin
	adc.setComparatorThresholdLow(0x0000);
	adc.setComparatorThresholdHigh(0xFFFF);
	adc.setComparatorQueConvert(0);
	adc.setComparatorLatch(0);

	pinMode(WIRE_INT, INPUT);
	attachInterrupt(digitalPinToInterrupt(WIRE_INT), adcISR, FALLING);

	adc.readADC(adcChannel); // trigger first read
}

void fetchADC() {
	if (adcReady) {
		adcRaw[adcChannel] = (0.1 * adc.toVoltage(adc.getValue()) * 1000) + (0.9 * adcRaw[adcChannel]);
		adcChannel = (adcChannel + 1) % 4;
		adcReady = false;
		adc.readADC(adcChannel); // request next channel
	}
}

void setupEeprom() {
	eeprom.begin(WIRE_SDA, WIRE_SCL);
	if (!eeprom.isConnected()) {
		Serial.println("eeprom not connected");
	}
}

void setupTemperature() {
	temperatureSensor.begin();
	temperatureSensor.setWaitForConversion(false);
	temperatureSensor.getAddress(tempDeviceAddress, 0);
	temperatureSensor.setResolution(tempDeviceAddress, resolution);
	temperatureSensor.requestTemperatures();
	delayInMillis = 750 / (1 << (12 - resolution));
	lastTempRequest = millis();
}

void fetchTemperature() {
	if (millis() - lastTempRequest >= delayInMillis) {
		temperatureRaw = temperatureSensor.getTempCByIndex(0);
		temperatureSensor.requestTemperatures();
		lastTempRequest = millis();
	}
}

void convertData() {
	pH = adcRaw[2] - adcRaw[1];
	orp = adcRaw[2] - adcRaw[0];
	nitrate = adcRaw[2] - adcRaw[3];
	temperature = temperatureRaw;

	Serial.printf("pH = %0.1f,	orp = %0.1f,	nitrate = %0.1f,	temperature = %0.2f\n", pH, orp, nitrate, temperature);
}

void sensorTask(void *parameter) {
	// setupEeprom();
	setupADC();
	setupTemperature();
	while (true) {
		for (size_t i = 0; i < 100; i++) {
			fetchADC();
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}

		fetchTemperature();
		convertData();

		// pHSensor.fitCurveFromPoints();

		// Serial.println(pHSensor.convertFromRaw(-5.0));

		// vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}