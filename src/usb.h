
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
		case POWERON_RESET:
			return "Vbat power on reset";
		case RTC_SW_SYS_RESET:
			return "Software reset digital core";
		case DEEPSLEEP_RESET:
			return "Deep Sleep reset digital core";
		case TG0WDT_SYS_RESET:
			return "Timer Group0 Watch dog reset digital core";
		case TG1WDT_SYS_RESET:
			return "Timer Group1 Watch dog reset digital core";
		case RTCWDT_SYS_RESET:
			return "RTC Watch dog Reset digital core";
		case INTRUSION_RESET:
			return "Instrusion tested to reset CPU";
		case TG0WDT_CPU_RESET:
			return "Time Group reset CPU";
		case RTC_SW_CPU_RESET:
			return "Software reset CPU";
		case RTCWDT_CPU_RESET:
			return "RTC Watch dog Reset CPU";
		case RTCWDT_BROWN_OUT_RESET:
			return "Reset when the vdd voltage is not stable";
		case RTCWDT_RTC_RESET:
			return "RTC Watch dog reset digital core and rtc module";
		default:
			return "Unknown Reset";
	}
}

void usbTask(void *parameter) {
	RESET_REASON resetReason = rtc_get_reset_reason(0);
	resetReasonText = returnResetText(resetReason);

	USBSerial.registerDeviceCallbacks(new myUSBCallbacks());
	USBSerial.setCallbacks(new myUSBSerialCallbacks());

	FFat.begin(true);

	USBDrive.init("/ffat", "ffat");
	USBSerial.begin();
	USBDrive.begin();
	usbSerialActive = true;

	File file = FFat.open("/log.txt", FILE_APPEND, true);
	file.println("xxx");
	file.close();

	vTaskDelete(NULL);
}