/** Talking to the Chromatic's ESP32 through esptool-js. All checks come from lib/. */

import { ESPLoader } from "../vendor/esptool-js-0.5.7/bundle.js";
import {
  BAUD,
  FLASH_SIZE,
  JTAG_PRODUCT_ID,
  JTAG_VENDOR_ID,
  READ_CHUNK,
  USB_PRODUCT_ID,
  USB_VENDOR_ID,
} from "./lib/constants.js";
import { classifyConnectError, FlasherError, notFound, verifyFailed } from "./lib/errors.js";
import { md5Hex, padTo4, sha256Hex } from "./lib/hash.js";
import { checkIdentity } from "./lib/identity.js";
import { checkFirmwarePlan } from "./lib/regions.js";
import { checkRestore } from "./lib/restore.js";
import { ChromaticTransport, resetConstructors } from "./serial.js";

export const PORT_FILTERS = [{ usbVendorId: USB_VENDOR_ID, usbProductId: USB_PRODUCT_ID }];

/** Refuse anything that is not the ESP32 serial port. */
export function checkPortInfo(info) {
  if (info.usbVendorId === JTAG_VENDOR_ID && info.usbProductId === JTAG_PRODUCT_ID) {
    throw new FlasherError("refused", "That is the Chromatic's JTAG bridge (0x33AA:0x0120), not the ESP32 serial port. Pick the 0x374E:0x0101 port.");
  }
  if (info.usbVendorId !== USB_VENDOR_ID || info.usbProductId !== USB_PRODUCT_ID) throw notFound("wrong USB id");
}

/** Open, reset into download mode, load the stub, and identify. */
export async function connectChromatic(port, log) {
  checkPortInfo(port.getInfo());
  const transport = new ChromaticTransport(port);
  const terminal = { clean() {}, writeLine: (s) => log(s), write: (s) => log(s) };
  const loader = new ESPLoader({ transport, baudrate: BAUD, romBaudrate: BAUD, terminal, resetConstructors });
  try {
    await loader.main("default_reset"); // sync, detect, then runStub (stub before any MD5)
    const description = await loader.chip.getChipDescription(loader);
    const mac = await loader.chip.readMac(loader);
    const flashSizeKB = await loader.getFlashSize();
    const { mac: normMac } = checkIdentity({ chipName: loader.chip.CHIP_NAME, description, flashSizeKB, mac });
    return { loader, transport, mac: normMac, description, flashSizeKB };
  } catch (e) {
    await closeQuietly(transport);
    if (e instanceof FlasherError) throw e;
    throw classifyConnectError(e) ?? e;
  }
}

async function closeQuietly(transport) {
  try {
    if (transport.device.readable || transport.device.writable) await transport.disconnect();
  } catch {
    /* port may already be closed */
  }
}

/** Read the whole 4 MB in READ_CHUNK pieces. */
export async function readFullFlash(loader, onProgress) {
  const out = new Uint8Array(FLASH_SIZE);
  for (let addr = 0; addr < FLASH_SIZE; addr += READ_CHUNK) {
    const data = await loader.readFlash(addr, READ_CHUNK);
    if (data.length < READ_CHUNK) throw new Error(`Short read at 0x${addr.toString(16)}.`);
    out.set(data.subarray(0, READ_CHUNK), addr);
    onProgress?.(addr + READ_CHUNK, FLASH_SIZE);
  }
  return out;
}

export const chipMd5 = (loader, addr, size) => loader.flashMd5sum(addr, size);

async function writeAndCheck(loader, regions, onProgress) {
  await loader.writeFlash({
    fileArray: regions.map((r) => ({ data: loader.ui8ToBstr(r.data), address: r.offset })),
    flashSize: "keep",
    flashMode: "keep",
    flashFreq: "keep",
    eraseAll: false,
    compress: true,
    reportProgress: (i, written, total) => onProgress?.(i, written, total),
  });
  for (const r of regions) {
    const padded = padTo4(r.data);
    const want = md5Hex(padded);
    const got = await chipMd5(loader, r.offset, padded.length);
    if (got !== want) throw verifyFailed(`MD5 at 0x${r.offset.toString(16)}: chip ${got}, file ${want}`);
  }
}

/** Write manifest files (already SHA-checked) at 0x8000 and 0x10000 only. */
export async function writeFirmware(loader, files, onProgress) {
  checkFirmwarePlan(files.map((f) => ({ offset: f.offset, size: f.data.length })));
  await writeAndCheck(loader, files.map((f) => ({ offset: f.offset, data: f.data })), onProgress);
}

/** Write a checked .chromatic backup back at 0x0. */
export async function restoreBackup(loader, mac, file, onProgress) {
  const flashSizeKB = await loader.getFlashSize();
  const { image } = await checkRestore({ file, chipMac: mac, chipFlashSizeKB: flashSizeKB, sha256Hex });
  await writeAndCheck(loader, [{ offset: 0, data: image }], onProgress);
}

/** Reset into the app and close the port. */
export async function resetAndClose(loader, transport) {
  await loader.after("hard_reset");
  await transport.disconnect();
}
