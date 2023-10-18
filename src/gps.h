#include <TinyGPS++.h>

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

const char *time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";

void IRAM_ATTR gpsISR() {
	// This is a callback function that will be activated on UART RX events
	while (GPS_Serial.available() > 0) {
		char x = GPS_Serial.read();
		// Serial.write(x);
		gps.encode(x);
	}
}

void gpsTask(void *parameter) {
	setenv("TZ", time_zone, 1);
	tzset();

	pinMode(OUTPUT_EN, OUTPUT);
	GPS_Serial.onReceive(gpsISR);
	GPS_Serial.begin(9600, SERIAL_8N1, JST_UART_RX, JST_UART_TX);
	GPS_Serial.setRxFIFOFull(64);
	digitalWrite(OUTPUT_EN, HIGH);

	while (gps.date.year() < 2022 || !gps.date.isValid() || !gps.time.isValid()) {
		vTaskDelay(100 / portTICK_PERIOD_MS);
		// Serial.println("gps");
	}

	struct tm t_tm;
	struct timeval val;

	t_tm.tm_hour = gps.time.hour();
	t_tm.tm_min = gps.time.minute();
	t_tm.tm_sec = gps.time.second();
	t_tm.tm_year = gps.date.year() - 1900; // Year, whose value starts from 1900
	t_tm.tm_mon = gps.date.month() - 1;	   // Month (starting from January, 0 for January) - Value range is [0,11]
	t_tm.tm_mday = gps.date.day();

	setenv("TZ", "GMT0", 1);
	tzset();
	val.tv_sec = mktime(&t_tm);
	val.tv_usec = 0;
	settimeofday(&val, NULL); // set system epoch

	setenv("TZ", time_zone, 1);
	tzset();

	vTaskDelete(NULL);
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