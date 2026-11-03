/**
 * The single-file backup: chromatic-backup-<date>.chromatic
 *
 * Layout (all little-endian):
 *   0   8 bytes   ASCII "CHRMBKUP"
 *   8   uint32    N, length of the JSON header in bytes (1..65536)
 *   12  N bytes   UTF-8 JSON header:
 *                 { "magic": "chromatic-backup", "version": 1, "mac": "aa:bb:cc:dd:ee:ff",
 *                   "flashSize": 4194304, "sha256": "<64 hex of the image>",
 *                   "date": "<ISO 8601 UTC>", "toolVersion": "<name> <version>" }
 *   12+N          the raw flash image, exactly flashSize (4,194,304) bytes, nothing after it
 *
 * The MAC lives in eFuse, not flash, so the header is what ties the image to one chip.
 * The raw image (bytes 12+N..end) is what esptool's read_flash would give.
 */

import { FLASH_SIZE, TOOL_NAME, TOOL_VERSION } from "./constants.js";
import { normalizeMac } from "./identity.js";

export const FILE_MAGIC = "CHRMBKUP";
export const HEADER_MAGIC = "chromatic-backup";
export const FORMAT_VERSION = 1;
export const HEADER_MAX = 65536;
const PREFIX = 12;

/** chromatic-backup-YYYY-MM-DD-HHMMSS.chromatic (UTC). */
export function backupFileName(date, ext = "chromatic") {
  const iso = date.toISOString();
  return `chromatic-backup-${iso.slice(0, 10)}-${iso.slice(11, 19).replace(/:/g, "")}.${ext}`;
}

export function makeHeader({ mac, sha256, date }) {
  return {
    magic: HEADER_MAGIC,
    version: FORMAT_VERSION,
    mac: normalizeMac(mac),
    flashSize: FLASH_SIZE,
    sha256,
    date: date.toISOString(),
    toolVersion: `${TOOL_NAME} ${TOOL_VERSION}`,
  };
}

/** Build the file bytes. */
export function encodeBackup(header, image) {
  if (image.length !== FLASH_SIZE) throw new Error(`Image must be ${FLASH_SIZE} bytes.`);
  const json = new TextEncoder().encode(JSON.stringify(header));
  if (json.length > HEADER_MAX) throw new Error("Header too large.");
  const out = new Uint8Array(PREFIX + json.length + image.length);
  for (let i = 0; i < 8; i++) out[i] = FILE_MAGIC.charCodeAt(i);
  new DataView(out.buffer).setUint32(8, json.length, true);
  out.set(json, PREFIX);
  out.set(image, PREFIX + json.length);
  return out;
}

/**
 * Split a .chromatic file. Checks structure only (magic, version, sizes, field formats).
 * The sha256, MAC and chip checks happen in restore.js.
 * @returns {{header:object, image:Uint8Array}}
 */
export function decodeBackup(bytes) {
  const bad = (why) => new Error(`This isn't a Chromatic backup file (${why}).`);
  if (!(bytes instanceof Uint8Array) || bytes.length < PREFIX) throw bad("too short");
  for (let i = 0; i < 8; i++) if (bytes[i] !== FILE_MAGIC.charCodeAt(i)) throw bad("wrong magic");
  const n = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(8, true);
  if (n < 2 || n > HEADER_MAX || PREFIX + n > bytes.length) throw bad("bad header length");
  let header;
  try {
    header = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes.subarray(PREFIX, PREFIX + n)));
  } catch {
    throw bad("unreadable header");
  }
  if (!header || header.magic !== HEADER_MAGIC) throw bad("wrong header magic");
  if (header.version !== FORMAT_VERSION) throw bad(`unsupported version ${header.version}`);
  if (header.flashSize !== FLASH_SIZE) throw new Error(`The backup says its size is ${header.flashSize}; it must be ${FLASH_SIZE.toLocaleString("en-US")} bytes.`);
  if (!/^[0-9a-f]{64}$/.test(String(header.sha256))) throw bad("no valid sha256");
  const image = bytes.subarray(PREFIX + n);
  if (image.length !== FLASH_SIZE) {
    throw new Error(`The backup holds ${image.length.toLocaleString("en-US")} bytes of flash; it must be exactly ${FLASH_SIZE.toLocaleString("en-US")}.`);
  }
  return { header: { ...header, mac: normalizeMac(header.mac) }, image };
}
