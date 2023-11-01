#include <Arduino.h>

#include "ADS1X15.h"
#include "I2C_eeprom.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <curveFitting.h>

bool sensorProbeOn = false;

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

class CalibratedSensor {
public:
	CalibratedSensor(const char *sensorName, uint16_t eepromAddress, int points, int order) {
		points_ = points;
		order_ = order;
		calibrationPoints_[0][points_] = {};
		coefficients_[order_ + 1] = {};
		_eepromAddress = eepromAddress;
		_sensorName = sensorName;
	}

	double convertFromRaw(double raw) {
		if (!importedDataPoints) {
			importCalibrationFromEeprom();
		}
		_raw = raw;
		value = 0.0;

		// Calculate the value using the polynomial equation
		for (int i = 0; i <= order_; i++) {
			value += coefficients_[i] * pow(_raw, order_ - i);
		}

		return value;
	}

	void fitCurveFromPoints() {
		Serial.println();
		Serial.println(_sensorName);

		double x[points_];
		double y[points_];

		for (int i = 0; i < points_; i++) {
			x[i] = calibrationPoints_[0][i]; // x values
			y[i] = calibrationPoints_[1][i]; // y values

			Serial.printf("(%0.2f,%0.2f)", x[i], y[i]);
		}

		fitCurve(order_, points_, x, y, order_ + 1, coefficients_);

		Serial.println();

		for (int i = 0; i < order_; i++) {
			Serial.printf("%0.6fx^%d + ", coefficients_[i], order_ - i);
		}

		Serial.printf("%0.6f", coefficients_[order_]);

		Serial.println();
	}

	void setCurrentCalibrationPoint(uint8_t index, double y) { setCalibrationPoint(index, _raw, y); }

	void setCalibrationPoint(uint8_t index, double x, double y) {
		calibrationPoints_[0][index] = x;
		calibrationPoints_[1][index] = y;

		eeprom.updateBlock(_eepromAddress, (uint8_t *)calibrationPoints_, sizeof(calibrationPoints_));
		fitCurveFromPoints();
	}

	void importCalibrationFromEeprom() {
		eeprom.readBlock(_eepromAddress, (uint8_t *)calibrationPoints_, sizeof(calibrationPoints_));
		fitCurveFromPoints();
		importedDataPoints = true;
	}

	double value = 0.0;

private:
	double _raw;
	double calibrationPoints_[2][3];
	double coefficients_[3];
	bool importedDataPoints = false;
	uint16_t _eepromAddress;
	const char *_sensorName;
	uint8_t points_; // number of data points for calibration
	uint8_t order_;	 // polynomial order
};

CalibratedSensor pH("pH", 0x0000, 3, 2);
CalibratedSensor orp("orp", 0x0100, 2, 1);
CalibratedSensor tds("tds", 0x0200, 3, 2);

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
		vTaskDelay((1000 / 128) + 1 / portTICK_PERIOD_MS);
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
	if (eeprom.isConnected()) {
		pH.importCalibrationFromEeprom();
		orp.importCalibrationFromEeprom();
		tds.importCalibrationFromEeprom();

	} else {
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
		temperature = owTemp.getTempC(deviceAddress);
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
		if (sensorProbeOn) {
			fetchADC();
			measureCapacitance();
			fetchTemperature();
			convertData();
			Serial.printf("%i	%0.2f	%0.0f	%0.0f	%0.1f\n", millis(), pH.value, orp.value, tds.value, temperature);
		} else {
			vTaskDelay(100 / portTICK_PERIOD_MS);
		}
	}
}