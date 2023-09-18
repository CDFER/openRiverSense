#include <Arduino.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

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
	double R2 = 10000.0;  // Resistance at reference temperature (10k ohrootMenu)
	double R1 = 10000.0;  // Resistance at ambient temperature (10k ohrootMenu)
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