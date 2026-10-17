/** Sanity checks on a full 4 MB flash image (backup or restore file). */

import { BOOTLOADER_OFFSET, FLASH_SIZE, IMAGE_MAGIC, PARTITION_TABLE_MAX, PARTITION_TABLE_OFFSET } from "./constants.js";
import { parsePartitionTable } from "./partition.js";

/**
 * Throws if the image is not a plausible Chromatic flash dump.
 * Returns the parsed partition table.
 */
export function checkFlashImage(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new Error("Image must be bytes.");
  if (bytes.length !== FLASH_SIZE) {
    throw new Error(`Image is ${bytes.length.toLocaleString("en-US")} bytes; a full backup is ${FLASH_SIZE.toLocaleString("en-US")}.`);
  }
  if (bytes[BOOTLOADER_OFFSET] !== IMAGE_MAGIC) {
    throw new Error(`No bootloader image at 0x1000 (expected 0xE9, found 0x${bytes[BOOTLOADER_OFFSET].toString(16).toUpperCase()}).`);
  }
  if (bytes[PARTITION_TABLE_OFFSET] !== 0xaa || bytes[PARTITION_TABLE_OFFSET + 1] !== 0x50) {
    throw new Error("No partition table at 0x8000 (magic 0xAA50 missing).");
  }
  return parsePartitionTable(bytes.subarray(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + PARTITION_TABLE_MAX));
}
