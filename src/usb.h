#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
#include "SPI.h"
#include "SdFat.h"

// file system object from SdFat
FatVolume fatfs;

// ESP32 use same flash device that store code.
// Therefore there is no need to specify the SPI and SS
Adafruit_FlashTransport_ESP32 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

// USB Mass Storage object
Adafruit_USBD_MSC usb_msc;

bool fileSystemActive; // Check if flash is formatted and ready to use
bool fs_changed;	   // Set to true when write to flash

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and
// return number of copied bytes (must be multiple of block size)
int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
	// Note: SPIFLash Bock API: readBlocks/writeBlocks/syncBlocks
	// already include 4K sector caching internally. We don't need to cache it, yahhhh!!
	return flash.readBlocks(lba, (uint8_t *)buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and
// return number of written bytes (must be multiple of block size)
int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
	// Note: SPIFLash Bock API: readBlocks/writeBlocks/syncBlocks
	// already include 4K sector caching internally. We don't need to cache it, yahhhh!!
	return flash.writeBlocks(lba, buffer, bufsize / 512) ? bufsize : -1;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void msc_flush_cb(void) {
	// sync with flash
	flash.syncBlocks();

	// clear file system's cache to force refresh
	fatfs.cacheClear();
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool msc_ready_callback(void) {
	// if fs has changed, mark unit as not ready temporarily to force PC to flush cache
	bool ret = !fs_changed;
	fs_changed = false;
	return ret;
}

void refreshMassStorage(void) { fs_changed = true; }

void usbTask(void *parameter) {
	flash.begin();

	// Set disk vendor id, product id and revision with string up to 8, 16, 4 characters respectively
	usb_msc.setID("Kea", "OpenRiverSense", "0.1");

	// Set callback
	usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);

	// Set disk size, block size should be 512 regardless of spi flash page size
	usb_msc.setCapacity(flash.size() / 512, 512);

	// MSC is ready for read/write
	fs_changed = false;
	usb_msc.setReadyCallback(0, msc_ready_callback);

	// Init file system on the flash
	fileSystemActive = fatfs.begin(&flash);

	usb_msc.begin();

	// while (!Serial)
	// 	delay(10); // wait for native usb
	// Serial.print("JEDEC ID: 0x");
	// Serial.println(flash.getJEDECID(), HEX);
	// Serial.print("Flash size: ");
	// Serial.print(flash.size() / 1024);
	// Serial.println(" KB");
	if (!fileSystemActive) {
		ESP_LOGE("FATFS", "Failed to init");
	}
	vTaskDelete(NULL);
}