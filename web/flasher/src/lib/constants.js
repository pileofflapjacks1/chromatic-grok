/** Fixed facts about the Chromatic ESP32 and what this page may write. */

export const TOOL_NAME = "chromatic-grok-web-flasher";
export const TOOL_VERSION = "0.1.0";
export const ESPTOOL_JS_VERSION = "0.5.7";

/** The composite USB serial port that reaches the ESP32 (the one to flash). */
export const USB_VENDOR_ID = 0x374e;
export const USB_PRODUCT_ID = 0x0101;
/** The JTAG bridge. It also shows up as a serial device but is not the ESP32 console. */
export const JTAG_VENDOR_ID = 0x33aa;
export const JTAG_PRODUCT_ID = 0x0120;

/** The bridge is only known to work at this rate. There is no baud change. */
export const BAUD = 115200;

export const CHIP_NAME = "ESP32";
export const FLASH_SIZE = 0x400000; // 4 MB, 4,194,304 bytes
export const FLASH_SIZE_KB = 4096;

export const BOOTLOADER_OFFSET = 0x1000;
export const PARTITION_TABLE_OFFSET = 0x8000;
export const PARTITION_TABLE_MAX = 0x1000; // ends at NVS (0x9000)
export const APP_OFFSET = 0x10000;
export const APP_MAX = 0x1f0000;

export const IMAGE_MAGIC = 0xe9;
export const PARTITION_MAGIC = 0x50aa; // bytes AA 50 on flash

/** esptool-js read chunk. Each chunk is one READ_FLASH command. */
export const READ_CHUNK = 0x10000;
