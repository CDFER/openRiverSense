#include <Arduino.h>
#include <MenuSystem.h>
#include <tft_eSPI.h>

#include "MaterialSymbols24.h"
#include "Roboto-Bold-24.h"
#include "Roboto-Bold-48.h"
#include "gpsOff.h"
#include "gpsOn.h"
double averageHDOP = 0.0;
double averageSats = 0.0;

TFT_eSPI screen;
TFT_eSprite topGui = TFT_eSprite(&screen);
TFT_eSprite menuBuffer = TFT_eSprite(&screen);

uint8_t menuIndex;

#define MENU_WIDTH 200
#define MENU_HIEGHT 200
#define MENU_ITEM_HIEGHT 48

struct Button {
	const uint8_t PIN;
	bool volatile pressed;
};

Button wakeButton = { WAKE_BUTTON, false };
Button upButton = { UP_BUTTON, false };
Button downButton = { DOWN_BUTTON, false };

void IRAM_ATTR buttonISR() {
	wakeButton.pressed = digitalRead(wakeButton.PIN);
	upButton.pressed = digitalRead(upButton.PIN);
	downButton.pressed = digitalRead(downButton.PIN);
}

class MyRenderer : public MenuComponentRenderer {
public:
	void render(Menu const& menu) const {
		menuBuffer.fillSprite(TFT_BLACK);

		for (menuIndex = 0; menuIndex < menu.get_num_components(); ++menuIndex) {
			MenuComponent const* cp_m_comp = menu.get_menu_component(menuIndex);
			if (cp_m_comp->is_current())
				menuBuffer.fillRoundRect(0, (menuIndex)*MENU_ITEM_HIEGHT, MENU_WIDTH, MENU_ITEM_HIEGHT, MENU_ITEM_HIEGHT / 2, TFT_DARKGREY);
			cp_m_comp->render(*this);
		}

		menuBuffer.pushSprite(20, 50);
	}

	void render_menu_item(MenuItem const& menu_item) const {
		renderComponent(menu_item.get_name(), menu_item.get_icon());
	}

	void render_back_menu_item(BackMenuItem const& menu_item) const {
		renderComponent(menu_item.get_name(), menu_item.get_icon());
	}

	void render_numeric_menu_item(NumericMenuItem const& menu_item) const {
		renderComponent(menu_item.get_name(), menu_item.get_icon(), menu_item.get_value());

		if (menu_item.has_focus()) {
			menuBuffer.printf("<");
		}
	}

	void render_menu(Menu const& menu) const {
		renderComponent(menu.get_name(), menu.get_icon());
	}

	void renderComponent(const char* name, const char* icon, float value = -1.0) const {
		menuBuffer.loadFont(MaterialSymbols24);
		menuBuffer.setTextDatum(MC_DATUM);
		menuBuffer.drawString(icon, MENU_ITEM_HIEGHT / 2, (menuIndex + 1) * MENU_ITEM_HIEGHT - 20);

		menuBuffer.loadFont(Roboto_Bold_24);
		menuBuffer.setTextDatum(CL_DATUM);
		menuBuffer.setTextWrap(false);

		menuBuffer.drawString(name, MENU_ITEM_HIEGHT + 5, (menuIndex + 1) * MENU_ITEM_HIEGHT - 22);

		if (value != -1.0) {
			menuBuffer.printf(" %0.1f", value);
		}
	}
};
MyRenderer my_renderer;

// Forward declarations

void on_item_selected(MenuComponent* p_menu_component);
void on_measure(MenuComponent* p_menu_component);
void on_gpsView(MenuComponent* p_menu_component);
void on_satelliteView(MenuComponent* p_menu_component);
void on_usbView(MenuComponent* p_menu_component);
void on_sensorView(MenuComponent* p_menu_component);
void drawTopGui(const char* localTime, bool locationFound, const char* batteryPercentage);
void drawRecordingGui(float currentSeconds, float totalSeconds);
void drawGPSGui();
const char* calculateBatteryPercentage(uint16_t batteryMilliVolts);
extern const char* getCurrentDateTime(const char* format);

// Menu variables
MenuSystem rootMenu(my_renderer);
MenuItem measure("Measure", "\uef3a", &on_measure);

Menu calibrateMenu("Calibrate", "\ue429");
MenuItem phCalibrate("pH", "\uf87a", &on_item_selected);
MenuItem orpCalibrate("ORP", "\uf878", &on_item_selected);
MenuItem nitrateCalibrate("Nitrate", "\uf876", &on_item_selected);
BackMenuItem back1("Back", "\ue5c4", NULL, &rootMenu);

Menu settingsMenu("Settings", "\ue8b8");
MenuItem logView("Log", "\ue002", &on_item_selected);
MenuItem deleteFiles("Delete Data", "\ue92b", &on_item_selected);
MenuItem deviceInfo("Device Info", "\ue88e", &on_item_selected);
BackMenuItem back2("Back", "\ue5c4", NULL, &rootMenu);

Menu debugMenu("Debug", "\ueb8e");
MenuItem gpsView("GPS", "\ue0c8", &on_gpsView);
MenuItem satelliteView("Satellites", "\ue0c8", &on_satelliteView);
MenuItem usbView("USB", "\ue1e0", &on_usbView);
MenuItem sensorView("Sensors", "\ue1e0", &on_sensorView);
BackMenuItem back3("Back", "\ue5c4", NULL, &rootMenu);

NumericMenuItem num("Float", "\ue3c9", nullptr, 1.0, 0.1, 10.0, 0.1);

void on_item_selected(MenuComponent* p_menu_component) {
	menuBuffer.loadFont(Roboto_Bold_24);
	menuBuffer.setCursor(0, 180);
	menuBuffer.print("Selected  ");
	menuBuffer.pushSprite(20, 50);
	delay(1000);  // so we can look the result on the LCD
}

void on_measure(MenuComponent* p_menu_component) {
	menuBuffer.loadFont(Roboto_Bold_48);
	menuBuffer.setTextDatum(MC_DATUM);

	for (float i = 0; i < 5; i += 0.03) {
		drawRecordingGui(i, 5);
		menuBuffer.pushSprite(20, 50);
		vTaskDelay(30 / portTICK_PERIOD_MS);
	}
}

void on_gpsView(MenuComponent* p_menu_component) {
	menuBuffer.unloadFont();
	menuBuffer.setTextFont(2);
	do {
		drawGPSGui();
		menuBuffer.pushSprite(20, 50);
		vTaskDelay(30 / portTICK_PERIOD_MS);
	} while (!wakeButton.pressed);
	wakeButton.pressed = false;
}

void on_sensorView(MenuComponent* p_menu_component) {
	extern uint32_t multiSampleADCmV(uint8_t pin, uint16_t samples);
	extern double voltageToTemperature(int voltage_mV);
	extern double voltageTopH(int voltage_mV);
	extern double voltageToORP(int voltage_mV);


	menuBuffer.loadFont(Roboto_Bold_24);

	do {
		double tempC = voltageToTemperature(multiSampleADCmV(JST_IO_2_2, 1000));
		double pH = voltageTopH(multiSampleADCmV(JST_IO_1_2, 10000));
		double orp = voltageToORP(multiSampleADCmV(JST_IO_1_2, 10000));

		menuBuffer.fillSprite(TFT_BLACK);
		menuBuffer.setCursor(0, 0);

		menuBuffer.printf("pH	= %.2f\n", pH);
		menuBuffer.printf("ORP	= %.0fmV\n", orp);
		menuBuffer.printf("Temp	= %.1f°C\n", tempC);

		menuBuffer.pushSprite(20, 50);
		vTaskDelay(30 / portTICK_PERIOD_MS);
	} while (!wakeButton.pressed);
	wakeButton.pressed = false;
}

void on_satelliteView(MenuComponent* p_menu_component) {
#define SIZE 200
#define X_POS 0
#define Y_POS 0
#define CENTRE_X (SIZE / 2) + X_POS
#define CENTRE_Y (SIZE / 2) + Y_POS

	menuBuffer.fillSprite(TFT_BLACK);
	menuBuffer.drawSmoothCircle(CENTRE_X, CENTRE_Y, SIZE / 2, TFT_WHITE, TFT_BLACK);
	menuBuffer.drawFastHLine(X_POS, CENTRE_Y, SIZE, TFT_WHITE);
	menuBuffer.drawFastVLine(CENTRE_X, Y_POS, SIZE, TFT_WHITE);

	do {
		for (size_t i = 0; i < gps.satellitesTracked.value(); i++) {
			double radians = (gps.trackedSatellites[i].azimuth) / 57.2957795;
			double radius = ((90 - gps.trackedSatellites[i].elevation) / 90.0) * (SIZE / 2.0);
			int32_t xVal = int32_t(radius * sin(radians));
			int32_t yVal = -int32_t(radius * cos(radians));

			menuBuffer.fillCircle(CENTRE_X + xVal, CENTRE_Y + yVal, 2, menuBuffer.color565(map(gps.trackedSatellites[i].strength, 50, 0, 0, 255), map(gps.trackedSatellites[i].strength, 0, 50, 0, 255), 0));
		}
		menuBuffer.pushSprite(20, 50);
		vTaskDelay(30 / portTICK_PERIOD_MS);
	} while (!wakeButton.pressed);
	wakeButton.pressed = false;
}

void on_usbView(MenuComponent* p_menu_component) {
	extern bool usbMounted;
	extern bool usbSuspended;
	extern uint32_t usbSerialBitrate;
	extern bool usbSerialActive;
	extern const char* resetReasonText;

	menuBuffer.loadFont(Roboto_Bold_24);
	menuBuffer.setTextDatum(TL_DATUM);
	menuBuffer.setTextWrap(true);
	do {
		menuBuffer.fillSprite(TFT_BLACK);
		menuBuffer.setCursor(0, 0);
		menuBuffer.printf(" USB:%s\n", usbMounted ? "Mounted" : "Unmounted");
		menuBuffer.printf("%s\n", usbSuspended ? "Idle" : "Resumed");
		menuBuffer.println("");
		menuBuffer.printf("Serial: %s\n", usbSerialActive ? "Active" : "Failed");
		menuBuffer.printf("%d baud\n", usbSerialBitrate);
		menuBuffer.println("");
		menuBuffer.printf("Reset: %s\n", resetReasonText);

		menuBuffer.pushSprite(20, 50);
		vTaskDelay(30 / portTICK_PERIOD_MS);
	} while (!wakeButton.pressed);
	wakeButton.pressed = false;
}

void guiTask(void* parameter) {
	extern uint32_t multiSampleADCmV(uint8_t pin, uint16_t samples);

	pinMode(SPI_EN, OUTPUT);
	digitalWrite(SPI_EN, HIGH);

	screen.init();
	screen.setRotation(2);
	screen.fillScreen(TFT_BLACK);

	topGui.setColorDepth(8);
	topGui.createSprite(200, 20);
	topGui.loadFont(Roboto_Bold_24);
	topGui.setTextColor(TFT_WHITE);

	menuBuffer.createSprite(MENU_HIEGHT, MENU_WIDTH);
	menuBuffer.setTextColor(TFT_WHITE);

	rootMenu.get_root_menu().add_item(&measure);
	rootMenu.get_root_menu().add_menu(&calibrateMenu);
	calibrateMenu.add_item(&phCalibrate);
	calibrateMenu.add_item(&orpCalibrate);
	calibrateMenu.add_item(&nitrateCalibrate);
	calibrateMenu.add_item(&back1);

	rootMenu.get_root_menu().add_menu(&settingsMenu);
	settingsMenu.add_item(&logView);
	settingsMenu.add_item(&deviceInfo);
	settingsMenu.add_item(&deleteFiles);
	settingsMenu.add_item(&back2);

	rootMenu.get_root_menu().add_menu(&debugMenu);
	debugMenu.add_item(&gpsView);
	debugMenu.add_item(&satelliteView);
	debugMenu.add_item(&usbView);
	debugMenu.add_item(&sensorView);
	debugMenu.add_item(&back3);

	rootMenu.display();
	rootMenu.next();
	rootMenu.display();
	rootMenu.next();
	rootMenu.display();
	rootMenu.next();
	rootMenu.display();
	rootMenu.select();
	rootMenu.display();

	pinMode(BACKLIGHT, OUTPUT);
	digitalWrite(BACKLIGHT, HIGH);

	pinMode(WAKE_BUTTON, INPUT);
	pinMode(UP_BUTTON, INPUT);
	pinMode(DOWN_BUTTON, INPUT);
	attachInterrupt(WAKE_BUTTON, buttonISR, CHANGE);
	attachInterrupt(UP_BUTTON, buttonISR, CHANGE);
	attachInterrupt(DOWN_BUTTON, buttonISR, CHANGE);

	while (true) {
		drawTopGui(getCurrentDateTime("%H:%M"), gps.location.isValid(), calculateBatteryPercentage((multiSampleADCmV(VBAT_SENSE, 100) * VBAT_SENSE_SCALE)));
		for (size_t i = 0; i < 100; i++) {
			if (upButton.pressed) {
				upButton.pressed = false;
				rootMenu.prev();
				rootMenu.display();
			}

			if (downButton.pressed) {
				downButton.pressed = false;
				rootMenu.next();
				rootMenu.display();
			}

			if (wakeButton.pressed) {
				wakeButton.pressed = false;
				rootMenu.select();
				rootMenu.display();
			}
			vTaskDelay(10 / portTICK_PERIOD_MS);
		}
	}
}

void drawTopGui(const char* localTime, bool locationFound, const char* batteryPercentage) {
	topGui.fillSprite(TFT_BLACK);
	topGui.setTextDatum(CL_DATUM);
	topGui.drawString(localTime, 0, 10);
	topGui.setTextDatum(CR_DATUM);
	topGui.drawString(batteryPercentage, 200, 10);
	if (locationFound) {
		topGui.pushImage(110, 0, 20, 20, gpsOn);
	}
	else {
		topGui.pushImage(110, 0, 20, 20, gpsOff);
	}

	topGui.pushSprite(20, 10);
}

void drawRecordingGui(float currentSeconds, float totalSeconds) {
	menuBuffer.fillSprite(TFT_BLACK);

	static char timeLeft[5];
	snprintf(timeLeft, sizeof(timeLeft), "%0.0f", (totalSeconds - currentSeconds) + 0.5);

	menuBuffer.drawString(timeLeft, MENU_WIDTH / 2, MENU_HIEGHT / 2);
	menuBuffer.drawSmoothArc(MENU_WIDTH / 2, MENU_HIEGHT / 2, 78, 70, 30, 30 + (currentSeconds / totalSeconds) * 300.00, TFT_GREEN, TFT_BLACK, true);
}

void drawGPSGui() {
	if (gps.location.isValid()) {
		if (averageHDOP == 0.0) {
			averageHDOP = gps.hdop.hdop();
		}
		else {
			averageHDOP = (averageHDOP * 0.9999) + (gps.hdop.hdop() * 0.0001);
		}

		if (averageSats == 0.0) {
			averageSats = double(gps.satellites.value());
		}
		else {
			averageSats = (averageSats * 0.9999) + (double(gps.satellites.value()) * 0.0001);
		}
	}

	menuBuffer.fillSprite(TFT_BLACK);
	menuBuffer.setCursor(0, 0);
	menuBuffer.printf("UTC: %02d-%02d-%02d %02d:%02d\n", gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute());
	menuBuffer.printf("NZST: %s\n", getCurrentDateTime("%Y-%m-%d %H:%M"));
	menuBuffer.printf("%0.6f, %0.6f\n", gps.location.lat(), gps.location.lng());
	menuBuffer.printf("HDOP %0.1f AV %0.1f\n", gps.hdop.hdop(), averageHDOP);
	menuBuffer.printf("Chars: %i\n", gps.charsProcessed());
}

/**
 * @brief Calculate battery percentage based on voltage.
 *
 * @param batteryMilliVolts The battery voltage in millivolts.
 * @return The battery percentage as a null-terminated char array.
 */
const char* calculateBatteryPercentage(uint16_t batteryMilliVolts) {
	static char batteryPercentage[5];  // Static array to hold the battery percentage
	batteryPercentage[4] = '\0';	   // Null-terminate the array

	const uint16_t batteryDischargeCurve[2][12] = {
		{0, 3300, 3400, 3500, 3600, 3700, 3800, 3900, 4000, 4100, 4200, 9999},
		{0, 0, 13, 22, 39, 53, 64, 78, 92, 100, 100, 100} };

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