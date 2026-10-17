/** Which flash regions this page is allowed to write. Everything else is refused. */

import {
  APP_MAX,
  APP_OFFSET,
  BOOTLOADER_OFFSET,
  FLASH_SIZE,
  PARTITION_TABLE_MAX,
  PARTITION_TABLE_OFFSET,
} from "./constants.js";

const hex = (n) => "0x" + n.toString(16);

/**
 * Check a firmware write. Returns the region name ("partition-table" or "app").
 * Throws with a plain reason otherwise.
 */
export function checkFirmwareWrite(offset, size) {
  if (!Number.isInteger(offset) || !Number.isInteger(size)) throw new Error("Offset and size must be whole numbers.");
  if (size <= 0) throw new Error(`Empty file at ${hex(offset)}.`);
  if (offset < PARTITION_TABLE_OFFSET) {
    throw new Error(`Refusing to write at ${hex(offset)}: nothing below 0x8000 is ever written (bootloader lives at ${hex(BOOTLOADER_OFFSET)}).`);
  }
  if (offset === PARTITION_TABLE_OFFSET) {
    if (size > PARTITION_TABLE_MAX) throw new Error(`Partition table is ${size} bytes; the limit is ${PARTITION_TABLE_MAX} (NVS starts at 0x9000).`);
    return "partition-table";
  }
  if (offset === APP_OFFSET) {
    if (size > APP_MAX) throw new Error(`App is ${size} bytes; the factory slot holds at most ${APP_MAX} (${hex(APP_MAX)}).`);
    return "app";
  }
  throw new Error(`Refusing to write at ${hex(offset)}: only 0x8000 (partition table) and 0x10000 (app) are allowed.`);
}

/** Check a whole firmware plan: each file allowed, no region twice. */
export function checkFirmwarePlan(files) {
  if (!Array.isArray(files) || files.length === 0) throw new Error("No files to write.");
  const seen = new Set();
  for (const f of files) {
    const region = checkFirmwareWrite(f.offset, f.size);
    if (seen.has(region)) throw new Error(`Two files target the ${region} region.`);
    seen.add(region);
  }
  return files;
}

/** A restore writes the full 4 MB image at 0x0, and nothing else. */
export function checkRestoreWrite(offset, size) {
  if (offset !== 0) throw new Error("A restore is written at 0x0 only.");
  if (size !== FLASH_SIZE) throw new Error(`A restore image must be exactly ${FLASH_SIZE.toLocaleString("en-US")} bytes, not ${size.toLocaleString("en-US")}.`);
}
