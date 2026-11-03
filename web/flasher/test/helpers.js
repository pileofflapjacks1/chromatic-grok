/** Fake flash images and partition tables. */

import { FLASH_SIZE } from "../src/lib/constants.js";

export function partitionEntry(label, type, subtype, offset, size) {
  const e = new Uint8Array(32);
  const dv = new DataView(e.buffer);
  e[0] = 0xaa; e[1] = 0x50; e[2] = type; e[3] = subtype;
  dv.setUint32(4, offset, true);
  dv.setUint32(8, size, true);
  for (let i = 0; i < label.length; i++) e[12 + i] = label.charCodeAt(i);
  return e;
}

/** The stock-style table: nvs, phy_init, factory 1 MB. */
export function stockTable() {
  const t = new Uint8Array(0xc00).fill(0xff);
  t.set(partitionEntry("nvs", 1, 2, 0x9000, 0x6000), 0);
  t.set(partitionEntry("phy_init", 1, 1, 0xf000, 0x1000), 32);
  t.set(partitionEntry("factory", 0, 0, 0x10000, 0x100000), 64);
  t.set([0xeb, 0xeb], 96);
  return t;
}

export function fakeFlash() {
  const img = new Uint8Array(FLASH_SIZE).fill(0xff);
  img[0x1000] = 0xe9;
  img.set(stockTable(), 0x8000);
  img[0x10000] = 0xe9;
  for (let i = 0; i < 1000; i++) img[0x9000 + i] = i & 0xff;
  return img;
}
