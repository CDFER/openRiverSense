#include <tft_eSPI.h>

#include "Roboto-Bold-24.h"
#include "gpsOff.h"
#include "gpsOn.h"

TFT_eSPI screen;
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

	// mainGui.createSprite(160, 240);

	menuDraw.createSprite(240, 240);
	menuDraw.loadFont(Roboto_Bold_24);
	menuDraw.setTextColor(TFT_WHITE);
	topGui.setTextDatum(TL_DATUM);

	pinMode(BACKLIGHT, OUTPUT);
	digitalWrite(BACKLIGHT, HIGH);
}

void drawScreen() {
	menuDraw.fillRoundRect(0, 0, 240, 240, 40, TFT_DARKGREY);
	menuDraw.drawWideLine(45, 20, 195, 20, 5, TFT_WHITE);
	menuDraw.drawString("pH Calibration >", 20, 40);
	menuDraw.drawString("pH Calibration >", 20, 80);
	menuDraw.drawString("pH Calibration >", 20, 120);
	// menuDraw.pushSprite(0, 40);
	menuDraw.pushSprite(0, 240);

}

void drawTopGui(const char *localTime, bool locationFound, const char *batteryPercentage) {
	topGui.fillSprite(TFT_BLACK);
	topGui.setTextDatum(CL_DATUM);
	topGui.drawString(localTime, 0, 10);
	topGui.setTextDatum(CR_DATUM);
	topGui.drawString(batteryPercentage, 200, 10);
	if (locationFound) {
		topGui.pushImage(110, 0, 20, 20, gpsOn);
	} else {
		topGui.pushImage(110, 0, 20, 20, gpsOff);
	}

	topGui.pushSprite(20, 10);
}