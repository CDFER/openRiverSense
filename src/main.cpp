#include <Arduino.h>

void setup() {
	Serial.begin(112500);
}

void loop() {
	Serial.println("Hello World");
	delay(1000);
}