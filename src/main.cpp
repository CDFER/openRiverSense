#include <Arduino.h>
#include <TinyGPS++.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>
#include <tft_eSPI.h>

#include "Roboto-Bold-24.h"
#include "SD.h"
#include "cdcusb.h"
#include "flashdisk.h"
#include "gpsOn.h"

CDCusb USBSerial;
FlashUSB dev;
char *l1 = "ffat";

#define ARDUINO_USB_MODE

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

TFT_eSPI screen;

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

TFT_eSprite topGui = TFT_eSprite(&screen);
TFT_eSprite mainGui = TFT_eSprite(&screen);
TFT_eSprite menuDraw = TFT_eSprite(&screen);

void initScreen() {
	pinMode(SPI_EN, OUTPUT);
	digitalWrite(SPI_EN, HIGH);

	screen.init();
	screen.setRotation(2);
	screen.fillScreen(TFT_BLACK);

	topGui.createSprite(200, 20);
	topGui.loadFont(Roboto_Bold_24);
	topGui.setTextColor(TFT_WHITE);

	//mainGui.createSprite(160, 240);

	menuDraw.createSprite(240, 240);
	menuDraw.loadFont(Roboto_Bold_24);
	menuDraw.setTextColor(TFT_WHITE);
	topGui.setTextDatum(TL_DATUM);

	pinMode(BACKLIGHT, OUTPUT);
	digitalWrite(BACKLIGHT, HIGH);
}

void drawScreen() {
	topGui.setTextDatum(CL_DATUM);
	topGui.drawString("10:45", 0, 10);
	topGui.setTextDatum(CR_DATUM);
	topGui.drawString("100%", 200, 10);
	topGui.pushImage(110, 0, 20, 20, gpsOn);
	//topGui.pushImage(110, 0, 20, 20, gpsOff);
	topGui.pushSprite(20, 10);

	menuDraw.fillRoundRect(0, 0, 240, 240, 40, TFT_DARKGREY);
	menuDraw.drawWideLine(45, 20, 195, 20, 5, TFT_WHITE);
	menuDraw.drawString("pH Calibration >", 20, 40);
	menuDraw.drawString("pH Calibration >", 20, 80);
	menuDraw.drawString("pH Calibration >", 20, 120);
	//menuDraw.pushSprite(0, 40);
	menuDraw.pushSprite(0, 240);

	// mesurementCountGui.drawString("14", TFT_WIDTH / 4, TFT_WIDTH / 4);
	// mesurementCountGui.drawLine(TFT_WIDTH / 4, TFT_WIDTH / 4 -21, TFT_WIDTH / 4, TFT_WIDTH / 4 + 21, TFT_WHITE);
	// mesurementCountGui.pushSprite(TFT_WIDTH / 4, TFT_WIDTH / 4);
}

void setup() {
	const char *time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";
	setenv("TZ", time_zone, 1);
	tzset();

	Serial.begin(115200);
	if (dev.init("/fat", "ffat")) {
		if (dev.begin()) {
			Serial.println("MSC lun 1 begin");
		} else
			log_e("LUN 1 failed");
	}

	USBSerial.manufacturer("espressif");
	USBSerial.serial("1234-567890");
	USBSerial.product("Test device");
	USBSerial.revision(100);
	USBSerial.deviceID(0xdead, 0xbeef);
	// USBSerial.registerDeviceCallbacks(new MyUSBSallnbacks());

	if (!USBSerial.begin())
		Serial.println("Failed to start CDC USB device");

	pinMode(OUTPUT_EN, OUTPUT);
	// GPS_Serial.setRxBufferSize(2048);
	GPS_Serial.begin(9600, SERIAL_8N1, JST_UART_RX, JST_UART_TX);

	pinMode(WAKE_BUTTON, INPUT);
	pinMode(UP_BUTTON, INPUT);
	pinMode(DOWN_BUTTON, INPUT);

	digitalWrite(OUTPUT_EN, HIGH);

	initScreen();
}

void loop() {
	while (GPS_Serial.available() > 0) {
		gps.encode(GPS_Serial.read());
		// USBSerial.write(GPS_Serial.read());
	}
	// USBSerial.printf("%.6f %.6f %i ", gps.location.lat(), gps.location.lng(), gps.satellites.value());
	// if (gps.time.isUpdated()) {
	// 	struct tm t_tm;
	// 	struct timeval val;

	// 	t_tm.tm_hour = gps.time.hour();
	// 	t_tm.tm_min = gps.time.minute();
	// 	t_tm.tm_sec = gps.time.second();
	// 	t_tm.tm_year = gps.date.year() - 1900;	// Year, whose value starts from 1900
	// 	t_tm.tm_mon = gps.date.month() - 1;		// Month (starting from January, 0 for January) - Value range is [0,11]
	// 	t_tm.tm_mday = gps.date.day();

	// 	val.tv_sec = mktime(&t_tm);	 // make epoch from UTC/GMT even when system timezone already set
	// 	val.tv_usec = 0;

	// 	settimeofday(&val, NULL);  // set system epoch
	// }

	// USBSerial.printf("%s\n", getCurrentDateTime("%Y-%m-%d,%H:%M:%S"));

	double tempC = voltageToTemperature(multiSampleADCmV(JST_IO_2_1, 1000));
	double pH = voltageTopH(multiSampleADCmV(JST_IO_1_2, 10000));
	double orp = voltageToORP(multiSampleADCmV(JST_IO_1_2, 10000));

	drawScreen();
}