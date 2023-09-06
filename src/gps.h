#include <TinyGPS++.h>

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

bool timeSet = false;

void readGPS(void) {
	// This is a callback function that will be activated on UART RX events
	while (GPS_Serial.available() > 0) {
		gps.encode(GPS_Serial.read());
	}

    if (timeSet == false && gps.time.isValid() && gps.date.year() > 2022) {
		struct tm t_tm;
		struct timeval val;

		t_tm.tm_hour = gps.time.hour();
		t_tm.tm_min = gps.time.minute();
		t_tm.tm_sec = gps.time.second();
		t_tm.tm_year = gps.date.year() - 1900;	// Year, whose value starts from 1900
		t_tm.tm_mon = gps.date.month() - 1;		// Month (starting from January, 0 for January) - Value range is [0,11]
		t_tm.tm_mday = gps.date.day();

		val.tv_sec = mktime(&t_tm) - _timezone;	 // make epoch from UTC/GMT even when system timezone already set
		val.tv_usec = 0;

		settimeofday(&val, NULL);  // set system epoch

		const char *time_zone = "NZST-12NZDT,M9.5.0,M4.1.0/3";
		setenv("TZ", time_zone, 1);
		tzset();

		timeSet = true;
	}
}

void initGPS(){
	pinMode(OUTPUT_EN, OUTPUT);
	GPS_Serial.setRxFIFOFull(120);
	GPS_Serial.onReceive(readGPS);
	GPS_Serial.begin(9600, SERIAL_8N1, JST_UART_RX, JST_UART_TX);
    digitalWrite(OUTPUT_EN,HIGH);
}