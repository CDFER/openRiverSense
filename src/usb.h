#include <FFat.h>
#include <FS.h>

#include "SD.h"
#include "cdcusb.h"
#include "esp32s2/rom/rtc.h"
#include "flashdisk.h"

CDCusb USBSerial;
FlashUSB USBDrive;
#define ARDUINO_USB_MODE

bool usbMounted = false;
bool usbSuspended = true;
bool usbSerialActive = false;
uint32_t usbSerialBitrate = 0;
const char *resetReasonText;

class myUSBCallbacks : public USBCallbacks {
	void onMount() {
		usbMounted = true;
	}
	void onUnmount() {
		usbMounted = false;
	}
	void onSuspend() {
		usbSuspended = true;
	}
	void onResume(bool resume) {
		usbSuspended = false;
	}
};

class myUSBSerialCallbacks : public CDCCallbacks {
	void onCodingChange(cdc_line_coding_t const *p_line_coding) {
		usbSerialBitrate = USBSerial.getBitrate();
	}
};

const char *returnResetText(int reason) {
	switch (reason) {
		case 1:
			return "Vbat power on reset";
		case 3:
			return "Software reset digital core";
		case 4:
			return "Legacy watch dog reset digital core";
		case 5:
			return "Deep Sleep reset digital core";
		case 6:
			return "Reset by SLC module, reset digital core";
		case 7:
			return "Timer Group0 Watch dog reset digital core";
		case 8:
			return "Timer Group1 Watch dog reset digital core";
		case 9:
			return "RTC Watch dog Reset digital core";
		case 10:
			return "Instrusion tested to reset CPU";
		case 11:
			return "Time Group reset CPU";
		case 12:
			return "Software reset CPU";
		case 13:
			return "RTC Watch dog Reset CPU";
		case 14:
			return "for APP CPU, reseted by PRO CPU";
		case 15:
			return "Reset when the vdd voltage is not stable";
		case 16:
			return "RTC Watch dog reset digital core and rtc module";
		default:
			return "Unkown";
	}
}

void usbTask(void *parameter) {
	RESET_REASON resetReason = rtc_get_reset_reason(0);
	resetReasonText = returnResetText(resetReason);

	USBSerial.registerDeviceCallbacks(new myUSBCallbacks());
	USBSerial.setCallbacks(new myUSBSerialCallbacks());

	USBDrive.init();
	USBSerial.begin();
	USBDrive.begin();
	usbSerialActive = true;
	// FFat.begin();

	// File file = FFat.open("/log.txt", FILE_APPEND, true);
	// file.printf("%s,%imV,boot\n", getCurrentDateTime("%Y-%m-%d,%H:%M"), multiSampleADCmV(VBAT_SENSE, 100) * VBAT_SENSE_SCALE);
	// file.close();

	// File file = FFat.open("/log.txt", FILE_APPEND, true);
	// 		file.printf("%s,setTime,%imV,", getCurrentDateTime("%Y-%m-%d,%H:%M"), multiSampleADCmV(VBAT_SENSE, 100) * VBAT_SENSE_SCALE);
	// 		file.close();

	vTaskDelete(NULL);
}