#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "SD.h"
#include "cdcusb.h"
#include "flashdisk.h"
#include "gps.h"
#include "gui.h"

CDCusb USBSerial;
FlashUSB dev;
char *l1 = "ffat";

#define ARDUINO_USB_MODE

uint32_t multiSampleADCmV(uint8_t pin, uint16_t samples) {
	esp_adc_cal_characteristics_t adc1_chars;
	uint32_t bufferAverage = 0;

	adc1_channel_t channel = adc1_channel_t(digitalPinToAnalogChannel(pin));
	if (channel < 0) {
		log_e("Pin %u is not ADC pin!", pin);
		return 0;
	}

	// Setup Analog Input ==================================================
	adc1_config_channel_atten(channel, ADC_ATTEN_DB_11);

	esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_13, 0, &adc1_chars);
	adc1_config_width(ADC_WIDTH_BIT_13);

	for (int i = 0; i < samples; i++) {
		bufferAverage += adc1_get_raw(channel);
	}

	bufferAverage = uint32_t(double(bufferAverage) / double(samples));

	return esp_adc_cal_raw_to_voltage(bufferAverage, &adc1_chars);
}

double voltageToTemperature(int voltage_mV) {
	double R2 = 10000.0;  // Resistance at reference temperature (10k ohms)
	double R1 = 10000.0;  // Resistance at ambient temperature (10k ohms)
	double T0 = 298.15;	  // Reference temperature in Kelvin (25°C)
	double B = 4000.0;	  // Beta value of the thermistor

	double R_thermistor = R2 * voltage_mV / (3300.0 - voltage_mV);	// Assuming a 3.3V reference voltage

	double inv_T = 1.0 / T0 + (1.0 / B) * log(R_thermistor / R1);

	double temperature_C = 1.0 / inv_T - 273.15;  // Convert back to Celsius

	return temperature_C;
}

double voltageTopH(int voltage_mV) {
	double pH = -0.0187 * double(voltage_mV) + 37.7;

	return pH;
}

double voltageToORP(int voltage_mV) {
	double orp = double(voltage_mV) - 1652.0;

	return orp;
}

/**
 * @brief Get the current date and time as a formatted string.
 *
 * @param format The desired format of the date and time string.
 * @return The current date and time as a formatted string.
 */
const char *getCurrentDateTime(const char *format) {
	static char dateTime[32];
	time_t currentEpoch;
	time(&currentEpoch);
	struct tm *timeInfo = localtime(&currentEpoch);
	strftime(dateTime, sizeof(dateTime), format, timeInfo);
	return dateTime;
}

/**
 * @brief Calculate battery percentage based on voltage using a lookup table.
 *
 * Lookup table for battery voltage in millivolts and corresponding percentage (based on PANASONIC_NCR_18650_B).
 * battery voltage 3300 = 3.3V, state of charge 22 = 22%.
 */
const uint16_t batteryDischargeCurve[2][12] = {
	{0, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200, 9999},
	{0, 0, 13, 22, 39, 53, 62, 74, 84, 94, 100, 100}};

/**
 * @brief Calculate battery percentage based on voltage.
 *
 * @param batteryMilliVolts The battery voltage in millivolts.
 * @return The battery percentage as a null-terminated char array.
 */
const char *calculateBatteryPercentage(uint16_t batteryMilliVolts) {
	static char batteryPercentage[5];  // Static array to hold the battery percentage
	batteryPercentage[4] = '\0';	   // Null-terminate the array

	// Determine the size of the lookup table
	uint8_t tableSize = sizeof(batteryDischargeCurve[0]) / sizeof(batteryDischargeCurve[0][0]);

	// Initialize the percentage variable
	uint8_t percentage = 0;

	// Iterate through the lookup table to find the two lookup values we are between
	for (uint8_t index = 0; index < tableSize - 1; index++) {
		// Check if the battery voltage is within the current range
		if (batteryMilliVolts <= batteryDischargeCurve[0][index + 1]) {
			// Get the x and y values for interpolation
			uint16_t x0 = batteryDischargeCurve[0][index];
			uint16_t x1 = batteryDischargeCurve[0][index + 1];
			uint8_t y0 = batteryDischargeCurve[1][index];
			uint8_t y1 = batteryDischargeCurve[1][index + 1];

			// Perform linear interpolation to calculate the battery percentage
			percentage = static_cast<uint8_t>(y0 + ((y1 - y0) * (batteryMilliVolts - x0)) / (x1 - x0));
			break;
		}
	}

	// Convert the percentage to a char array
	snprintf(batteryPercentage, sizeof(batteryPercentage), "%u%%", percentage);

	return batteryPercentage;
}

void setup() {
	// const char *time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";
	// setenv("TZ", time_zone, 1);
	// tzset();

	Serial.begin(115200);
	if (dev.init("/fat", "ffat")) {
		if (dev.begin()) {
			Serial.println("MSC lun 1 begin");
		} else
			log_e("LUN 1 failed");
	}

	// USBSerial.manufacturer("espressif");
	// USBSerial.serial("1234-567890");
	// USBSerial.product("Test device");
	// USBSerial.revision(100);
	USBSerial.deviceID(0xdead, 0xbeef);
	// USBSerial.registerDeviceCallbacks(new MyUSBSallnbacks());

	if (!USBSerial.begin())
		Serial.println("Failed to start CDC USB device");

	pinMode(WAKE_BUTTON, INPUT);
	pinMode(UP_BUTTON, INPUT);
	pinMode(DOWN_BUTTON, INPUT);

	initScreen();
	initGPS();
}

void loop() {
	double tempC = voltageToTemperature(multiSampleADCmV(JST_IO_2_1, 1000));
	double pH = voltageTopH(multiSampleADCmV(JST_IO_1_2, 10000));
	double orp = voltageToORP(multiSampleADCmV(JST_IO_1_2, 10000));

	drawTopGui(getCurrentDateTime("%H:%M"), gps.location.isValid(), calculateBatteryPercentage(4000));


}