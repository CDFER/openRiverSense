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
	adc.setGain(2);
	adc.setDataRate(0);

	// disable comparator
	adc.setComparatorThresholdHigh(0x8000);
	adc.setComparatorThresholdLow(0x0000);
	adc.setComparatorQueConvert(0);

	pinMode(WIRE_INT, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(WIRE_INT), adcISR, RISING);

	adc.setMode(0);			 // continuous mode
	adc.readADC(adcChannel); // trigger first read
}

void fetchADC() {
	if (adcReady) {
		adcRaw[adcChannel] = adc.toVoltage(adc.getValue());
		adcChannel++;
		if (adcChannel >= 4) adcChannel = 0;
		adc.readADC(adcChannel); // request next channel
		adcReady = false;
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
	pH = 0.1 * (adcRaw[2] - adcRaw[0]) + 0.9 * pH;
	orp = 0.1 * (adcRaw[2] - adcRaw[1]) + 0.9 * orp;
	nitrate = 0.1 * (adcRaw[2] - adcRaw[3]) + 0.9 * nitrate;
	temperature = 0.1 * (temperatureRaw) + 0.9 * temperature;

	Serial.printf("pH = %0.1f, orp = %0.1f, nitrate = %0.1f, temperature = %0.2f\n", pH, orp, nitrate, temperature);
}

void sensorTask(void *parameter) {
	// setupEeprom();
	// setupADC();
	// setupTemperature();
	while (true) {
		// fetchADC();
		// fetchTemperature();
		// convertData();

		pHSensor.fitCurveFromPoints();

		Serial.println(pHSensor.convertFromRaw(-5.0));

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}