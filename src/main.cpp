#include <Arduino.h>
// Analog Input
#include <driver/adc.h>
#include <esp_adc_cal.h>
// #include <DallasTemperature.h>
// #include <OneWire.h>
// #include <SD.h>
#include <TinyGPS++.h>
// #include <WiFi.h>
#include <lvgl.h>
#include <tft_eSPI.h>

#include "USB.h"

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#define WIFI_WIFI_SSID "YourWIFI"
#define WIFI_PW "PASSWORD"

#endif

#define ARDUINO_USB_MODE

TFT_eSPI screen;

// USBMSC MSC;
USBCDC USBSerial;

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);
TFT_eSprite ui = TFT_eSprite(&screen);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[TFT_WIDTH * TFT_HEIGHT / 10];

#define READAV 1000
double pHmV = 0;
uint32_t samples[READAV];
double raw = 0;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
	uint32_t w = (area->x2 - area->x1 + 1);
	uint32_t h = (area->y2 - area->y1 + 1);

	screen.startWrite();
	screen.setAddrWindow(area->x1, area->y1, w, h);
	screen.pushColors((uint16_t *)&color_p->full, w * h, true);
	screen.endWrite();

	lv_disp_flush_ready(disp_drv);
}

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

void displayInfo() {
	ui.setCursor(10, 50);
	ui.fillSprite(TFT_BLACK);
	uint32_t adcTime = millis();
	// ui.printf("%.5f %.5f\n", gps.location.lat(), gps.location.lng());
	// ui.printf("%i Satellites\n", gps.satellites.value());
	// ui.printf("%s\n", getCurrentDateTime("%Y-%m-%d,%H:%M:%S"));
	// ui.printf("%.1fhdop\n", gps.hdop.hdop());
	// for (int i = 0; i < READAV; i++) {`
	// 	samples[i] = analogReadMilliVolts(JST_IO_1_1);
	// }
	// raw = 0;
	// for (int i = 0; i < READAV; i++) {
	// 	raw += double(samples[i]);
	// }
	// raw /= READAV;

	pHmV = (double(multiSampleADCmV(JST_IO_1_1, 8192))) * 0.1 + pHmV * 0.9;
	ui.printf("%.1fmV\n", pHmV);
	ui.printf(" %ims\n", millis() - adcTime);
	ui.pushSprite(0, 0);
}

float voltageToTemperature(int voltage_mV) {
	float R2 = 10000.0;	 // Resistance at reference temperature (10k ohms)
	float R1 = 10000.0;	 // Resistance at ambient temperature (10k ohms)
	float T0 = 298.15;	 // Reference temperature in Kelvin (25°C)
	float B = 4000.0;	 // Beta value of the thermistor

	float R_thermistor = R2 * voltage_mV / (3300.0 - voltage_mV);  // Assuming a 3.3V reference voltage

	float inv_T = 1.0 / T0 + (1.0 / B) * log(R_thermistor / R1);

	float temperature_C = 1.0 / inv_T - 273.15;	 // Convert back to Celsius

	return temperature_C;
}

lv_obj_t *bar1;
static lv_obj_t *label;
lv_obj_t *bar2;
static lv_obj_t *label2;
lv_obj_t *bar3;
static lv_obj_t *label3;
static lv_obj_t *uiClock;
static lv_obj_t *battery;

void setup() {
	Serial.begin(115200);
	USBSerial.setDebugOutput(true);
	USBSerial.begin();
	USB.begin();

	pinMode(SPI_EN, OUTPUT);
	digitalWrite(SPI_EN, HIGH);

	pinMode(TFT_CS, OUTPUT);
	digitalWrite(TFT_CS, HIGH);

	screen.begin();
	screen.setRotation(1);
	screen.fillScreen(TFT_BLACK);

	pinMode(BACKLIGHT, OUTPUT);
	digitalWrite(BACKLIGHT, HIGH);

	pinMode(OUTPUT_EN, OUTPUT);
	digitalWrite(OUTPUT_EN, HIGH);
	GPS_Serial.setRxBufferSize(2048);
	GPS_Serial.begin(9600, SERIAL_8N1, JST_UART_RX, JST_UART_TX);

	pinMode(JST_IO_1_1, INPUT);
	pinMode(JST_IO_1_2, INPUT);

	lv_init();
	lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_HEIGHT * TFT_WIDTH / 10);

	/*Initialize the display*/
	static lv_disp_drv_t disp_drv;
	lv_disp_drv_init(&disp_drv);
	/*Change the following line to your display resolution*/
	disp_drv.hor_res = TFT_HEIGHT;
	disp_drv.ver_res = TFT_WIDTH;
	disp_drv.flush_cb = my_disp_flush;
	disp_drv.draw_buf = &draw_buf;
	lv_disp_drv_register(&disp_drv);

	uiClock = lv_label_create(lv_scr_act());
	lv_label_set_text(uiClock, "00:00");
	lv_obj_align_to(uiClock, lv_scr_act(), LV_ALIGN_TOP_LEFT, 25, 0);

	battery = lv_label_create(lv_scr_act());
	lv_label_set_text(battery, "100%");
	lv_obj_align_to(battery, lv_scr_act(), LV_ALIGN_TOP_RIGHT, -25, 0);

	/*Create a container with ROW flex direction*/
	lv_obj_t *cont_row = lv_obj_create(lv_scr_act());
	lv_obj_set_size(cont_row, TFT_HEIGHT, TFT_WIDTH);
	lv_obj_align(cont_row, LV_ALIGN_TOP_MID, 0, 20);
	lv_obj_set_flex_flow(cont_row, LV_FLEX_FLOW_ROW_WRAP);

	bar1 = lv_bar_create(cont_row);
	lv_obj_set_size(bar1, 180, 20);
	lv_bar_set_range(bar1, 1200, 2500);
	lv_obj_center(bar1);

	label = lv_label_create(cont_row);
	lv_label_set_text(label, "0");
	lv_obj_align_to(label, bar1, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/

	bar2 = lv_bar_create(cont_row);
	lv_obj_set_size(bar2, 180, 20);
	lv_bar_set_range(bar2, 1200, 2500);
	lv_obj_center(bar2);

	label2 = lv_label_create(cont_row);
	lv_label_set_text(label2, "0");
	lv_obj_align_to(label2, bar2, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/

	bar3 = lv_bar_create(cont_row);
	lv_obj_set_size(bar3, 180, 20);
	lv_bar_set_range(bar3, -10, 30);
	lv_obj_center(bar3);

	label3 = lv_label_create(cont_row);
	lv_label_set_text(label3, "0");
	lv_obj_align_to(label3, bar2, LV_ALIGN_OUT_TOP_MID, 0, -15); /*Align top of the slider*/
}

void loop() {
	// Empty loop, not needed
	// vTaskSuspend(NULL);
	// uint32_t hotfixTime = millis();

		while (GPS_Serial.available() > 0) {
			gps.encode(GPS_Serial.read());
		}
		lv_label_set_text_fmt(uiClock, "%i:%i", gps.time.hour(), gps.time.minute());
		// USBSerial.printf("%.6f %.6f\n", gps.location.lat(), gps.location.lng());
		// if (gps.time.isUpdated()) {
		// 	struct tm t_tm;
		// 	struct timeval val;

		// 	t_tm.tm_hour = gps.time.hour();
		// 	t_tm.tm_min = gps.time.minute();
		// 	t_tm.tm_sec = gps.time.second();
		// 	t_tm.tm_year = gps.date.year() - 1900;	// Year, whose value starts from 1900
		// 	t_tm.tm_mon = gps.date.month() - 1;		// Month (starting from January, 0 for January) - Value range is [0,11]
		// 	t_tm.tm_mday = gps.date.day();

		// 	val.tv_sec = mktime(&t_tm) - _timezone;	 // make epoch from UTC/GMT even when system timezone already set
		// 	val.tv_usec = gps.time.centisecond() * 10000;

		// 	settimeofday(&val, NULL);  // set system epoch
		// 	setenv("TZ", time_zone, 1);
		// 	tzset();

		// 	Serial.printf("%s,%u\n", getCurrentDateTime("%Y-%m-%d,%H:%M:%S"), batteryMilliVolts);
		// }
		//vTaskDelay(10);
		//displayInfo();


	lv_bar_set_value(bar1, multiSampleADCmV(JST_IO_1_1, 1000), LV_ANIM_ON);
	lv_label_set_text_fmt(label, "%imV", lv_bar_get_value(bar1));

	lv_bar_set_value(bar2, multiSampleADCmV(JST_IO_1_2, 10000), LV_ANIM_ON);
	lv_label_set_text_fmt(label2, "%imV", lv_bar_get_value(bar2));

	float tempC = voltageToTemperature(multiSampleADCmV(JST_IO_2_1, 1000));

	lv_bar_set_value(bar3, tempC, LV_ANIM_ON);
	lv_label_set_text_fmt(label3, "%0.1f C", tempC);

	lv_timer_handler(); /* let the GUI do its work */
	delay(5);

	// struct tm t_tm;
	// struct timeval val;

	// t_tm.tm_hour = gps.time.hour();
	// t_tm.tm_min = gps.time.minute();
	// t_tm.tm_sec = gps.time.second();
	// t_tm.tm_year = gps.date.year() - 1900;	// Year, whose value starts from 1900
	// t_tm.tm_mon = gps.date.month() - 1;		// Month (starting from January, 0 for January) - Value range is [0,11]
	// t_tm.tm_mday = gps.date.day();

	// val.tv_sec = mktime(&t_tm) - _timezone;	 // make epoch from UTC/GMT even when system timezone already set
	// val.tv_usec = gps.time.centisecond() * 10000;

	// settimeofday(&val, NULL);  // set system epoch
	// setenv("TZ", time_zone, 1);
	// tzset();

	// vTaskDelay(10 / portTICK_PERIOD_MS);
	// digitalWrite(OUTPUT_EN, LOW);
	// vTaskDelay(5000 / portTICK_PERIOD_MS);
	//  esp_sleep_config_gpio_isolate();
	//  esp_sleep_enable_timer_wakeup(1000000*5);
	//  esp_deep_sleep_start();
}