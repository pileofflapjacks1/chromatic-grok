import assert from "node:assert/strict";
import { test } from "node:test";
import { backupFileName, decodeBackup, encodeBackup, FILE_MAGIC, makeHeader } from "../src/lib/container.js";
import { sha256Hex } from "../src/lib/hash.js";
import { checkRestore } from "../src/lib/restore.js";
import { fakeFlash } from "./helpers.js";

const MAC = "aa:bb:cc:00:11:22";
const DATE = new Date("2026-10-17T18:37:05Z");

async function makeFile(image = fakeFlash(), header = {}) {
  const h = { ...makeHeader({ mac: MAC, sha256: await sha256Hex(image), date: DATE }), ...header };
  return { image, file: encodeBackup(h, image) };
}

function rewriteHeader(file, mutate) {
  const n = new DataView(file.buffer).getUint32(8, true);
  const h = JSON.parse(new TextDecoder().decode(file.subarray(12, 12 + n)));
  mutate(h);
  const json = new TextEncoder().encode(JSON.stringify(h));
  const out = new Uint8Array(12 + json.length + (file.length - 12 - n));
  out.set(file.subarray(0, 8));
  new DataView(out.buffer).setUint32(8, json.length, true);
  out.set(json, 12);
  out.set(file.subarray(12 + n), 12 + json.length);
  return out;
}

test("file name", () => {
  assert.equal(backupFileName(DATE), "chromatic-backup-2026-10-17-183705.chromatic");
  assert.equal(backupFileName(DATE, "bin"), "chromatic-backup-2026-10-17-183705.bin");
});

test("container round trip keeps header and the exact image", async () => {
  const { image, file } = await makeFile();
  assert.equal(new TextDecoder().decode(file.subarray(0, 8)), FILE_MAGIC);
  const { header, image: back } = decodeBackup(file);
  assert.deepEqual(back, image);
  assert.equal(header.magic, "chromatic-backup");
  assert.equal(header.version, 1);
  assert.equal(header.mac, MAC);
  assert.equal(header.flashSize, 4194304);
  assert.equal(header.date, DATE.toISOString());
  assert.match(header.toolVersion, /chromatic-grok-web-flasher/);
});

test("container rejects bad magic, bad lengths, bad header", async () => {
  const { file } = await makeFile();
  const bad = file.slice();
  bad[0] = 0x58;
  assert.throws(() => decodeBackup(bad), /wrong magic/);
  assert.throws(() => decodeBackup(file.subarray(0, 5)), /too short/);
  const hugeLen = file.slice();
  new DataView(hugeLen.buffer).setUint32(8, 0xffffffff, true);
  assert.throws(() => decodeBackup(hugeLen), /header length/);
  assert.throws(() => decodeBackup(rewriteHeader(file, (h) => (h.magic = "nope"))), /header magic/);
  assert.throws(() => decodeBackup(rewriteHeader(file, (h) => (h.version = 2))), /version 2/);
  assert.throws(() => decodeBackup(rewriteHeader(file, (h) => (h.flashSize = 8388608))), /must be 4,194,304/);
  assert.throws(() => decodeBackup(file.subarray(0, file.length - 1)), /exactly 4,194,304/);
  const extra = new Uint8Array(file.length + 1);
  extra.set(file);
  assert.throws(() => decodeBackup(extra), /exactly 4,194,304/);
  assert.throws(() => encodeBackup({}, new Uint8Array(10)), /4194304/);
});

const ok = (file) => ({ file, chipMac: MAC, chipFlashSizeKB: 4096, sha256Hex });

test("restore accepts a good backup for the same chip", async () => {
  const { image, file } = await makeFile();
  const r = await checkRestore(ok(file));
  assert.deepEqual(r.image, image);
  assert.equal(r.partitions.length, 3);
  await checkRestore({ ...ok(file), chipMac: "AA:BB:CC:00:11:22" });
});

test("restore refuses when the chip's flash is not exactly 4 MB", async () => {
  const { file } = await makeFile();
  for (const kb of [2048, 8192, 16384, undefined]) {
    await assert.rejects(checkRestore({ ...ok(file), chipFlashSizeKB: kb }), /Flash must be 4 MB/, `flash ${kb}`);
  }
});

test("restore refuses a different Chromatic", async () => {
  const { file } = await makeFile();
  await assert.rejects(checkRestore({ ...ok(file), chipMac: "aa:bb:cc:00:11:23" }), /different Chromatic/);
});

test("restore refuses a damaged image (sha256 mismatch)", async () => {
  const { file } = await makeFile();
  const bad = file.slice();
  bad[bad.length - 10] ^= 1;
  await assert.rejects(checkRestore(ok(bad)), /damaged/);
});

test("restore refuses bad magic and wrong size", async () => {
  const { file } = await makeFile();
  const bad = file.slice();
  bad[1] = 0;
  await assert.rejects(checkRestore(ok(bad)), /wrong magic/);
  await assert.rejects(checkRestore(ok(file.subarray(0, file.length - 4096))), /4,194,304/);
});

test("restore runs the image sanity checks", async () => {
  const image = fakeFlash();
  image[0x1000] = 0;
  const { file } = await makeFile(image);
  await assert.rejects(checkRestore(ok(file)), /0xE9/);
});
