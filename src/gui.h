#include <Arduino.h>
#include <MenuSystem.h>
#include <tft_eSPI.h>

#include "MaterialSymbols24.h"
#include "Roboto-Bold-24.h"
#include "Roboto-Bold-48.h"
#include "gpsOff.h"
#include "gpsOn.h"

RTC_DATA_ATTR static char batteryText[6] = "100%";

TFT_eSPI screen;
TFT_eSprite topGui = TFT_eSprite(&screen);
TFT_eSprite menuBuffer = TFT_eSprite(&screen); //~40kb in size

#define MENU_WIDTH 240
#define MENU_HEIGHT 200
#define MENU_ITEM_HEIGHT 48
#define BACKLIGHT_BRIGHTNESS 32

enum ButtonStates : uint8_t { NOT_PRESSED = 0, UP_PRESSED, DOWN_PRESSED, WAKE_PRESSED };
volatile ButtonStates buttonState = NOT_PRESSED;
bool resetWatchdog = false;
uint8_t topGuiCounter = 0;

void IRAM_ATTR buttonISR() {
	if (digitalRead(WAKE_BUTTON)) {
		buttonState = WAKE_PRESSED;
	} else if (digitalRead(UP_BUTTON)) {
		buttonState = UP_PRESSED;
	} else if (digitalRead(DOWN_BUTTON)) {
		buttonState = DOWN_PRESSED;
	} else {
		buttonState = NOT_PRESSED;
	}
}

uint8_t menuIndex;

class MyRenderer : public MenuComponentRenderer {
public:
	void render(Menu const &menu) const {
		menuBuffer.fillSprite(TFT_BLACK);

		for (menuIndex = 0; menuIndex < menu.get_num_components(); ++menuIndex) {
			MenuComponent const *currentComponent = menu.get_menu_component(menuIndex);
			currentComponent->render(*this);
		}

		menuBuffer.pushSprite(20, 40);
	}

	void render_menu_item(MenuItem const &item) const {
		renderComponent(menuIndex, item.is_current(), item.get_name(), item.get_icon());
	}

	void render_back_menu_item(BackMenuItem const &item) const {
		renderComponent(menuIndex, item.is_current(), item.get_name(), item.get_icon());
	}

	void render_numeric_menu_item(NumericMenuItem const &item) const {
		renderComponent(menuIndex, item.is_current(), item.get_name(), item.get_icon());
		if (item.has_focus()) {
			menuBuffer.printf(" %0.1f <", item.get_value());
		} else {
			menuBuffer.printf(" %0.1f", item.get_value());
		}
	}

	void render_menu(Menu const &menu) const {
		renderComponent(menuIndex, menu.is_current(), menu.get_name(), menu.get_icon());
	}

	void renderComponent(uint8_t index, bool highlight, const char *name, const char *icon) const {
		if (highlight) {
			menuBuffer.fillRoundRect(0, (index)*MENU_ITEM_HEIGHT, MENU_WIDTH, MENU_ITEM_HEIGHT, MENU_ITEM_HEIGHT / 2,
									 TFT_DARKGREY);
		}

		if (icon != nullptr) {
			menuBuffer.loadFont(MaterialSymbols24);
			menuBuffer.setTextDatum(MC_DATUM);
			menuBuffer.drawString(icon, MENU_ITEM_HEIGHT / 2, (index + 1) * MENU_ITEM_HEIGHT - 20);
		}

		menuBuffer.loadFont(Roboto_Bold_24);
		menuBuffer.setTextDatum(CL_DATUM);
		menuBuffer.drawString(name, MENU_ITEM_HEIGHT + 5, (index + 1) * MENU_ITEM_HEIGHT - 22);
	}
};
MyRenderer menuRenderer;

void drawTopGui() {
	if (topGuiCounter == 0) {
		topGui.fillSprite(TFT_BLACK);
		topGui.setTextDatum(CL_DATUM);

		static char timeText[7];
		time_t currentEpoch;
		time(&currentEpoch);
		struct tm *timeInfo = localtime(&currentEpoch);
		strftime(timeText, sizeof(timeText), "%H:%M", timeInfo);

		topGui.drawString(timeText, 0, 10);
		topGui.setTextDatum(CR_DATUM);

		topGui.drawString(batteryText, 200, 10);
		if (gps.location.isValid()) {
			topGui.pushImage(110, 0, 20, 20, gpsOn);
		} else {
			topGui.pushImage(110, 0, 20, 20, gpsOff);
		}

		topGui.pushSprite(40, 10);
		topGuiCounter = 60;
	} else {
		topGuiCounter--;
	}
}

void setupMenuBuffer() {
	menuBuffer.fillSprite(TFT_BLACK);
	menuBuffer.loadFont(Roboto_Bold_24);
	menuBuffer.setCursor(0, 0);
}

void waitForButtonPress() {
	while (buttonState == NOT_PRESSED) {
		drawTopGui();
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

void drawSpinner(uint16_t totalTimeSeconds) {
	static char timeText[10];
	const uint32_t x = MENU_WIDTH / 2;
	const uint32_t y = MENU_HEIGHT / 2;
	const uint32_t startAngle = 30;

	menuBuffer.loadFont(Roboto_Bold_48);
	menuBuffer.setTextDatum(MC_DATUM);
	menuBuffer.setTextColor(TFT_WHITE, TFT_BLACK, true);
	menuBuffer.fillSprite(TFT_BLACK);
	buttonState = NOT_PRESSED;

	uint16_t totalTime = totalTimeSeconds * 1000;
	unsigned long endTime = millis() + totalTime;

	while (millis() < endTime && buttonState == NOT_PRESSED) {
		TickType_t xLastWakeTime = xTaskGetTickCount();

		uint16_t timeLeft = endTime - millis();

		snprintf(timeText, sizeof(timeText), "  %0.1f  ", float(timeLeft) / 1000.0);
		uint32_t endAngle = startAngle + ((float(timeLeft) / float(totalTime)) * (360 - 2 * startAngle));

		menuBuffer.drawString(timeText, x, y);
		menuBuffer.drawSmoothArc(x, y, 78, 70, startAngle, (360 - startAngle), TFT_DARKGREY, TFT_BLACK, true);
		if (endAngle - startAngle > 0) menuBuffer.drawSmoothArc(x, y, 78, 70, startAngle, endAngle, TFT_GREEN, TFT_BLACK, true);

		menuBuffer.pushSprite(20, 40);

		xTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(30));
	}
	menuBuffer.setTextColor(TFT_WHITE, TFT_WHITE, false);
}

void drawGPSView() {
	menuBuffer.printf("%02d-%02d-%02d %02d:%02d\n", gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(),
					  gps.time.minute());
	menuBuffer.printf("%0.6f\n", gps.location.lat());
	menuBuffer.printf("%0.6f\n", gps.location.lng());
	menuBuffer.printf("HDOP %0.1f\n", gps.hdop.hdop());
	menuBuffer.printf("Tracked Sats %i\n", gps.satellitesTracked.value());
	menuBuffer.printf("Visible Sats %i\n", gps.satellites.value());
	menuBuffer.printf("TTFF %ims\n", gps.timeToFirstFix());
	menuBuffer.printf("Chars: %i\n", gps.charsProcessed());
}

void drawSatelliteView() {
#define SIZE MENU_HEIGHT
#define X_POS (MENU_WIDTH - SIZE) / 2
#define Y_POS 0
#define CENTRE_X (SIZE / 2) + X_POS
#define CENTRE_Y (SIZE / 2) + Y_POS

	menuBuffer.drawSmoothCircle(CENTRE_X, CENTRE_Y, SIZE / 2, TFT_DARKGREY, TFT_BLACK);
	menuBuffer.drawFastHLine(X_POS, CENTRE_Y, SIZE, TFT_DARKGREY);
	menuBuffer.drawFastVLine(CENTRE_X, Y_POS, SIZE, TFT_DARKGREY);

	for (size_t i = 0; i < gps.satellitesTracked.value(); i++) {

		double radians = (gps.trackedSatellites[i].azimuth) / 57.2957795;
		double radius = ((90 - gps.trackedSatellites[i].elevation) / 90.0) * (SIZE / 2.0);
		int32_t xVal = int32_t(radius * sin(radians));
		int32_t yVal = -int32_t(radius * cos(radians));

		uint8_t signalStrength = map(gps.trackedSatellites[i].strength, 0, 40, 0, 255);
		uint16_t satelliteColor = menuBuffer.color565(0, signalStrength, 0);
		menuBuffer.fillCircle(CENTRE_X + xVal, CENTRE_Y + yVal, 2, satelliteColor);
	}
}

void drawDeviceInfo() {
	menuBuffer.printf("%s Rev %i\n", ESP.getChipModel(), ESP.getChipRevision());
	menuBuffer.printf("   %i core @ %iMHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());
	menuBuffer.println("FLASH");
	menuBuffer.printf("   Size: %.0f MB\n", static_cast<float>(ESP.getFlashChipSize()) / (1024.0 * 1024.0));
	menuBuffer.printf("   Speed: %.0f MHz\n", static_cast<float>(ESP.getFlashChipSpeed()) / (1000.0 * 1000.0));
	menuBuffer.println("FATFS");
	menuBuffer.printf("   Size: %.0f KB\n", static_cast<float>(flash.size()) / 1024.0);
	menuBuffer.printf("   Free: %.0f%%\n",
					  static_cast<float>(fatfs.freeClusterCount()) / static_cast<float>(fatfs.clusterCount()) * 100.0);
}

void onPageView(MenuComponent *p_menu_component) {
	const char *_name = p_menu_component->get_name();
	void (*drawFunction)() = nullptr;

	if (strcmp(_name, "GPS") == 0) {
		drawFunction = drawGPSView;
	} else if (strcmp(_name, "Satellites") == 0) {
		drawFunction = drawSatelliteView;
	} else if (strcmp(_name, "Device Info") == 0) {
		setupMenuBuffer();
		drawDeviceInfo();
		menuBuffer.pushSprite(20, 40);
	}

	buttonState = NOT_PRESSED;
	while (buttonState == NOT_PRESSED) {

		if (drawFunction != nullptr) {
			setupMenuBuffer();
			drawFunction();
			menuBuffer.pushSprite(20, 40);
		}
		drawTopGui();
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

void menuItemSelected(MenuComponent *p_menu_component) {
	setupMenuBuffer();
	menuBuffer.printf("%s selected", p_menu_component->get_name());
	menuBuffer.pushSprite(20, 40);
}

void drawCalibrationPrep(const char *name, bool chooseCalibrate) {
	setupMenuBuffer();

	menuBuffer.setCursor(0, 12);
	menuBuffer.println("Place probe in buffer");
	menuBuffer.println("solution and press ");
	menuBuffer.println("calibrate...");

	static char componentText[24];
	snprintf(componentText, sizeof(componentText), "Calibrate %s", name);
	menuRenderer.renderComponent(2, chooseCalibrate, componentText, "\ue3c9");
	menuRenderer.renderComponent(3, !chooseCalibrate, "Back", "\ue5c4");
	menuBuffer.pushSprite(20, 40);
}

void onRecord(MenuComponent *p_menu_component) { drawSpinner(10); }

void onCalibrate(MenuComponent *p_menu_component) {
	const char *_name = p_menu_component->get_name();
	bool chooseCalibrate = true;

	do {
		drawCalibrationPrep(_name, chooseCalibrate);
		buttonState = NOT_PRESSED;
		waitForButtonPress();

		if (buttonState == UP_PRESSED) {
			chooseCalibrate = true;
		} else if (buttonState == DOWN_PRESSED) {
			chooseCalibrate = false;
		}
	} while (buttonState != WAKE_PRESSED);

	if (chooseCalibrate == true) {
		drawSpinner(10);

		if (strcmp(_name, "pH 9.2") == 0) {
			pH.setCalibrationPoint(0, 9.23);
		} else if (strcmp(_name, "pH 6.9") == 0) {
			pH.setCalibrationPoint(1, 6.88);
		} else if (strcmp(_name, "pH 4.0") == 0) {
			pH.setCalibrationPoint(2, 4.00);
		} else if (strcmp(_name, ".256V") == 0) {
			orp.setCalibrationPoint(0, 256);
		}
	}
}

MenuSystem rootMenu(menuRenderer);
MenuItem measure("Measure", "\uef3a", &onRecord);

Menu calibrateMenu("Calibrate", "\ue429");
BackMenuItem calibrateMenuBack("Back", "\ue5c4", NULL, &rootMenu);

Menu phCalibrate("pH", "\uf87a");
MenuItem pH9("pH 9.2", "\ue3c9", &onCalibrate);
MenuItem pH7("pH 6.9", "\ue3c9", &onCalibrate);
MenuItem pH4("pH 4.0", "\ue3c9", &onCalibrate);
BackMenuItem pHBack("Back", "\ue5c4", NULL, &rootMenu);

Menu orpCalibrate("ORP", "\uf878");
MenuItem orp256(".256V", "\ue3c9", &onCalibrate);
BackMenuItem orpBack("Back", "\ue5c4", NULL, &rootMenu);

Menu tdsCalibrate("TDS", "\uf876");
BackMenuItem tdsBack("Back", "\ue5c4", NULL, &rootMenu);

Menu settingsMenu("Settings", "\ue8b8");
MenuItem deviceInfo("Device Info", "\ue88e", &onPageView);
NumericMenuItem num("Float", "\ue3c9", nullptr, 1.0, 0.1, 10.0, 0.1);
BackMenuItem settingsMenuBack("Back", "\ue5c4", NULL, &rootMenu);

Menu debugMenu("Debug", "\ueb8e");
MenuItem gpsStats("GPS", "\ue0c8", &onPageView);
MenuItem satellites("Satellites", "\ue0c8", &onPageView);
BackMenuItem debugMenuBack("Back", "\ue5c4", NULL, &rootMenu);

void setupMenu() {
	rootMenu.get_root_menu().add_item(&measure);

	rootMenu.get_root_menu().add_menu(&calibrateMenu);

	calibrateMenu.add_menu(&phCalibrate);
	phCalibrate.add_item(&pH4);
	phCalibrate.add_item(&pH7);
	phCalibrate.add_item(&pH9);
	phCalibrate.add_item(&pHBack);

	calibrateMenu.add_menu(&orpCalibrate);
	orpCalibrate.add_item(&orp256);
	orpCalibrate.add_item(&orpBack);

	// calibrateMenu.add_menu(&tdsCalibrate);
	// tdsCalibrate.add_item(&tdsBack);

	calibrateMenu.add_item(&calibrateMenuBack);

	rootMenu.get_root_menu().add_menu(&settingsMenu);
	settingsMenu.add_item(&deviceInfo);
	settingsMenu.add_item(&num);
	settingsMenu.add_item(&settingsMenuBack);

	rootMenu.get_root_menu().add_menu(&debugMenu);
	debugMenu.add_item(&gpsStats);
	debugMenu.add_item(&satellites);
	debugMenu.add_item(&debugMenuBack);

	rootMenu.display();
}

void setupButtons() {
	pinMode(WAKE_BUTTON, INPUT);
	pinMode(UP_BUTTON, INPUT);
	pinMode(DOWN_BUTTON, INPUT);
	attachInterrupt(WAKE_BUTTON, buttonISR, RISING);
	attachInterrupt(UP_BUTTON, buttonISR, RISING);
	attachInterrupt(DOWN_BUTTON, buttonISR, RISING);
}

void guiTask(void *parameter) {
#if defined(SPI_EN)
	pinMode(SPI_EN, OUTPUT);
	digitalWrite(SPI_EN, HIGH);
#endif

	screen.init();
	screen.setRotation(1);
	screen.fillScreen(TFT_BLACK);

	topGui.setColorDepth(8);
	topGui.createSprite(200, 20);
	topGui.loadFont(Roboto_Bold_24);
	topGui.setTextColor(TFT_WHITE);

	menuBuffer.setColorDepth(8);
	menuBuffer.createSprite(MENU_WIDTH, MENU_HEIGHT);
	menuBuffer.setTextColor(TFT_WHITE);

	setupMenu();

	pinMode(BACKLIGHT, OUTPUT);
	analogWriteFrequency(40000);
	analogWrite(BACKLIGHT, BACKLIGHT_BRIGHTNESS);

	while (true) {
		drawTopGui();
		if (buttonState > NOT_PRESSED) {
			switch (buttonState) {
			case WAKE_PRESSED:
				rootMenu.select();
				break;
			case UP_PRESSED:
				rootMenu.prev();
				break;
			case DOWN_PRESSED:
				rootMenu.next();
				break;
			}
			rootMenu.display();
			resetWatchdog = true;
			buttonState = NOT_PRESSED;
		}
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}