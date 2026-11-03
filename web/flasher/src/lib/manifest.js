/**
 * manifest.json: which firmware files to write, from which commit.
 *
 * {
 *   "name": "chromatic-grok", "version": "0.14.0-grok",
 *   "commit": "<40 hex>", "commitUrl": "https://github.com/.../commit/<sha>",
 *   "license": "GPL-3.0-only",
 *   "files": [ { "name": "partition-table.bin", "offset": 32768, "size": 3072, "sha256": "<64 hex>" }, ... ]
 * }
 */

import { checkFirmwarePlan } from "./regions.js";

const SHA1 = /^[0-9a-f]{40}$/;
const SHA256 = /^[0-9a-f]{64}$/;
const SAFE_NAME = /^[A-Za-z0-9._-]+$/;

export function parseManifest(json) {
  const m = typeof json === "string" ? JSON.parse(json) : json;
  if (!m || typeof m !== "object") throw new Error("manifest.json is not an object.");
  if (!SHA1.test(String(m.commit))) throw new Error("manifest.json needs a full 40-character commit SHA.");
  let url;
  try {
    url = new URL(m.commitUrl);
  } catch {
    throw new Error("manifest.json needs a commitUrl.");
  }
  if (url.protocol !== "https:" || !url.pathname.includes(m.commit)) throw new Error("commitUrl must be https and name the same commit.");
  if (!Array.isArray(m.files)) throw new Error("manifest.json has no files list.");
  const files = m.files.map((f) => {
    if (!SAFE_NAME.test(String(f.name))) throw new Error(`Bad file name in manifest: ${f.name}`);
    if (!SHA256.test(String(f.sha256))) throw new Error(`Bad sha256 for ${f.name}.`);
    return { name: f.name, offset: f.offset, size: f.size, sha256: f.sha256 };
  });
  checkFirmwarePlan(files);
  return { ...m, files };
}

/**
 * Check fetched bytes against the manifest entry. Throws on any mismatch.
 * @param {{name:string,size:number,sha256:string}} entry
 * @param {Uint8Array} bytes
 * @param {(b:Uint8Array)=>Promise<string>} sha256Hex
 */
export async function checkManifestFile(entry, bytes, sha256Hex) {
  if (bytes.length !== entry.size) throw new Error(`${entry.name} is ${bytes.length} bytes; the manifest says ${entry.size}.`);
  const got = await sha256Hex(bytes);
  if (got !== entry.sha256) throw new Error(`${entry.name} SHA-256 does not match the manifest. Nothing was written.`);
}
