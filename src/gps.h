#include <TinyGPS++.h>

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

const char *time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";

extern RTC_DATA_ATTR float averageGPSTimeToLocationFixSeconds;
extern bool workingHours;

void IRAM_ATTR gpsISR() {
	while (GPS_Serial.available() > 0) {
		char buf = GPS_Serial.read();
		gps.encode(buf);
		//Serial.print(buf);
	}
}

void gpsTask(void *parameter) {
	setenv("TZ", time_zone, 1);
	tzset();

	GPS_Serial.onReceive(gpsISR);
	GPS_Serial.begin(9600, SERIAL_8N1, JST_UART_RX, JST_UART_TX);
	vTaskDelay(250 / portTICK_PERIOD_MS);
	GPS_Serial.write("$PCAS01,5*19\r\n");  // set to 115200 baud
	GPS_Serial.flush();
	GPS_Serial.updateBaudRate(115200);
	vTaskDelay(250 / portTICK_PERIOD_MS);
	GPS_Serial.write("$PCAS02,500*1A\r\n");	 // set to 2 HZ
	GPS_Serial.flush();

	while (gps.date.year() < 2022 || !gps.date.isValid() || !gps.time.isValid()) {
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	struct tm t_tm;
	struct timeval val;

	t_tm.tm_hour = gps.time.hour();
	t_tm.tm_min = gps.time.minute();
	t_tm.tm_sec = gps.time.second();
	t_tm.tm_year = gps.date.year() - 1900;	// Year, whose value starts from 1900
	t_tm.tm_mon = gps.date.month() - 1;		// Month (starting from January, 0 for January) - Value range is [0,11]
	t_tm.tm_mday = gps.date.day();

	setenv("TZ", "GMT0", 1);
	tzset();
	val.tv_sec = mktime(&t_tm);
	val.tv_usec = 0;
	settimeofday(&val, NULL);  // set system epoch

	setenv("TZ", time_zone, 1);
	tzset();

	time_t currentEpoch;
	time(&currentEpoch);
	struct tm *timeInfo = localtime(&currentEpoch);
	static char dateTime[32];
	strftime(dateTime, sizeof(dateTime), "%d-%m-%Y %H:%M", timeInfo);

	if (timeInfo->tm_hour >= 9 && timeInfo->tm_hour >= 5) {
		workingHours = true;
		ESP_LOGI("GPS", "Set ESP Time To %s, Working Hours ON", dateTime);
	} else {
		workingHours = false;
		ESP_LOGI("GPS", "Set ESP Time To %s, Working Hours OFF", dateTime);
	}


	while (gps.timeToFirstFix() == 0) {
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	float ttffSeconds = ((float)gps.timeToFirstFix()) / 1000.0;
	averageGPSTimeToLocationFixSeconds = (averageGPSTimeToLocationFixSeconds * 0.9) + (ttffSeconds * 0.1);
	ESP_LOGI("GPS", "Got Location Fix in %0.3fs", ttffSeconds);

	vTaskDelete(NULL);
}