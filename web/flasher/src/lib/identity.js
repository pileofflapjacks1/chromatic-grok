/** Check the connected chip before any read or write. */

import { CHIP_NAME, FLASH_SIZE_KB } from "./constants.js";

const MAC_RE = /^[0-9a-f]{2}(:[0-9a-f]{2}){5}$/;

export function normalizeMac(mac) {
  const m = String(mac ?? "").trim().toLowerCase().replace(/-/g, ":");
  if (!MAC_RE.test(m)) throw new Error(`Not a MAC address: ${mac}`);
  return m;
}

/** "ESP32-D0WD (revision 3)" style descriptions start with the family name. */
export function isEsp32(chipName, description = "") {
  if (chipName !== CHIP_NAME) return false;
  // ESP32-S2/S3/C3 report other CHIP_NAMEs; keep a second guard on the description.
  return !/^ESP32-(S|C|H|P)/i.test(description);
}

/**
 * @param {{chipName:string, description?:string, flashSizeKB:number|undefined, mac:string}} info
 * @returns {{mac:string}}
 */
export function checkIdentity(info) {
  if (!isEsp32(info.chipName, info.description)) {
    throw new Error(`This is not the Chromatic's ESP32 (found ${info.chipName || "unknown chip"}${info.description ? `, ${info.description}` : ""}).`);
  }
  checkFlashSize(info.flashSizeKB);
  return { mac: normalizeMac(info.mac) };
}

/** The flash ID must report exactly 4 MB. Used at connect time and again before a restore. */
export function checkFlashSize(flashSizeKB) {
  if (flashSizeKB !== FLASH_SIZE_KB) {
    const found = typeof flashSizeKB === "number" ? `${flashSizeKB} KB` : "an unknown size";
    throw new Error(`Flash must be 4 MB; the chip reports ${found}.`);
  }
}
