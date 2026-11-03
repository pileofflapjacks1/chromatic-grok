/** Every check a restore must pass before anything is written. Pure apart from the hash callback. */

import { decodeBackup } from "./container.js";
import { checkFlashSize, normalizeMac } from "./identity.js";
import { checkFlashImage } from "./image.js";
import { checkRestoreWrite } from "./regions.js";

/**
 * @param {{file:Uint8Array, chipMac:string, chipFlashSizeKB:number|undefined,
 *          sha256Hex:(b:Uint8Array)=>Promise<string>}} a
 * @returns {Promise<{header:object, image:Uint8Array, partitions:object[]}>}
 */
export async function checkRestore({ file, chipMac, chipFlashSizeKB, sha256Hex }) {
  checkFlashSize(chipFlashSizeKB); // read from the chip again right before restoring
  const { header, image } = decodeBackup(file);
  checkRestoreWrite(0, image.length);
  if ((await sha256Hex(image)) !== header.sha256) {
    throw new Error("This backup file is damaged (its contents don't match its checksum). Nothing was written.");
  }
  if (header.mac !== normalizeMac(chipMac)) {
    throw new Error("This backup came from a different Chromatic. A backup can only go back on the Chromatic it was made from. Nothing was written.");
  }
  const partitions = checkFlashImage(image);
  return { header, image, partitions };
}
