#include <Arduino.h>

#include "ADS1X15.h"
#include "I2C_eeprom.h"

#include <ExponentialRegression.h>
#include <LinearRegression.h>
LinearRegression pHRegression = LinearRegression();
LinearRegression orpRegression = LinearRegression();
ExponentialRegression tdsRegression = ExponentialRegression();

I2C_eeprom eeprom(0x50, I2C_DEVICESIZE_24LC64);
bool eepromConnected = false;

ADS1015 adc1(0x48);
bool adc1Connected = false;

ADS1015 adc2(0x49);
float tdsRaw = 0.0;
float temperature = 0.0;
bool adc2Connected = false;

class CalibratedSensor {
  public:
	CalibratedSensor(const char *sensorName, uint16_t eepromAddress) {
		calibrationPoints_[0][3] = {};
		_eepromAddress = eepromAddress;
		_sensorName = sensorName;
	}

	void setValue(double valueIn) {
		value = valueIn;
	}

	void setCurrentCalibrationPoint(uint8_t index, double y) {
		setCalibrationPoint(index, raw, y);
	}

	void setCalibrationPoint(uint8_t index, double x, double y) {
		calibrationPoints_[0][index] = x;
		calibrationPoints_[1][index] = y;

		eeprom.updateBlock(_eepromAddress, (uint8_t *)calibrationPoints_, sizeof(calibrationPoints_));
	}

	void importCalibrationFromEeprom() {
		eeprom.readBlock(_eepromAddress, (uint8_t *)calibrationPoints_, sizeof(calibrationPoints_));
		importedDataPoints = true;
	}

	double value = 0.0;
	double raw;
	double calibrationPoints_[2][3];

  private:
	bool importedDataPoints = false;
	uint16_t _eepromAddress;
	const char *_sensorName;
};

CalibratedSensor pH("pH", 0x0000);
CalibratedSensor orp("orp", 0x0100);
CalibratedSensor tds("tds", 0x0200);

void setupADC() {
	if (adc1.begin(WIRE_SDA, WIRE_SCL)) {
		adc1Connected = true;
	}

	if (adc2.begin(WIRE_SDA, WIRE_SCL)) {
		adc2Connected = true;
	}
}

void measureCapacitance() {
	adc2.setGain(8);
	adc2.setDataRate(4);

	int16_t raw = 0;
	unsigned long start = micros();

	pinMode(WIRE_INT, OUTPUT);
	digitalWrite(WIRE_INT, HIGH);

	while (raw < 100 && micros() - start < 256 * 1000) {
		raw = adc2.readADC_Differential_2_3();
	}

	digitalWrite(WIRE_INT, LOW);
	start = micros();

	while (raw > -100 && micros() - start < 256 * 1000) {
		raw = adc2.readADC_Differential_2_3();
	}

	unsigned long end = micros();

	pinMode(WIRE_INT, INPUT);
	tdsRaw = (1.0 / 3.0) * (float)(end - start) + (2.0 / 3.0) * tdsRaw;
	tds.raw = tdsRaw / 1000.0;

	tds.setValue(tdsRegression.calculate(tds.raw));
}

void setupEeprom() {
	if (eeprom.begin(WIRE_SDA, WIRE_SCL)) {
		eepromConnected = true;
		pH.importCalibrationFromEeprom();
		orp.importCalibrationFromEeprom();
		tds.importCalibrationFromEeprom();

		pHRegression.learn(pH.calibrationPoints_[0][0], pH.calibrationPoints_[1][0]);
		pHRegression.learn(pH.calibrationPoints_[0][1], pH.calibrationPoints_[1][1]);
		pHRegression.learn(pH.calibrationPoints_[0][2], pH.calibrationPoints_[1][2]);

		orpRegression.learn(orp.calibrationPoints_[0][0], orp.calibrationPoints_[1][0]);
		orpRegression.learn(orp.calibrationPoints_[0][1], orp.calibrationPoints_[1][1]);

		tdsRegression.learn(tds.calibrationPoints_[0][0], tds.calibrationPoints_[1][0]);
		tdsRegression.learn(tds.calibrationPoints_[0][1], tds.calibrationPoints_[1][1]);
		tdsRegression.learn(tds.calibrationPoints_[0][2], tds.calibrationPoints_[1][2]);
	}
}

void readTemperature() {
	pinMode(ONEWIRE, OUTPUT);
	digitalWrite(ONEWIRE, HIGH);
	vTaskDelay(10 / portTICK_PERIOD_MS);

	adc2.setGain(16);
	float voltage = adc2.toVoltage(adc2.readADC_Differential_0_1());

	digitalWrite(ONEWIRE, LOW);

	// the resistor values for the Wheatstone bridge are:
	const float r1 = 10000;
	const float r2 = 10000;
	const float r3 = 10000;
	const float vRef = 3.3;

	float resistance = (r2 * r3 + r3 * (r1 + r2) * voltage / vRef) / (r1 - (r1 + r2) * voltage / vRef);

	// NTC thermistor values from datasheet
	const float t0 = 273.15;  // 0C in kelvin
	const float bConstant = 3435;
	const float nominalR = 10000;
	const float nominalTemp = t0 + 25;

	temperature = 1 / ((log(resistance / nominalR) / bConstant) + (1 / nominalTemp)) - t0;
}

void readPH() {
	adc1.setGain(16);
	adc1.setDataRate(0);
	pH.raw = adc1.toVoltage(adc1.readADC_Differential_2_3()) * 1000;
	pH.setValue(pHRegression.calculate(pH.raw));
}

void readORP() {
	adc1.setGain(4);
	adc1.setDataRate(0);
	orp.raw = adc1.toVoltage(adc1.readADC_Differential_0_1()) * 1000;
	orp.setValue(orpRegression.calculate(orp.raw));
}

void sensorTask(void *parameter) {
	vTaskDelay(5000 / portTICK_PERIOD_MS);
	setupEeprom();
	setupADC();
	vTaskSuspend(NULL);

	while (true) {
		TickType_t sensorWakeTime = xTaskGetTickCount();
		if (adc1Connected && adc2Connected) {
			readTemperature();
			readPH();
			readORP();
			measureCapacitance();
		}

		xTaskDelayUntil(&sensorWakeTime, pdMS_TO_TICKS(200));
	}
}