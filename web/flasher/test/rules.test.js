import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { test } from "node:test";
import { checkFirmwarePlan, checkFirmwareWrite, checkRestoreWrite } from "../src/lib/regions.js";
import { formatPartitionTable, parsePartitionTable } from "../src/lib/partition.js";
import { checkFlashImage } from "../src/lib/image.js";
import { md5Hex, padTo4, sha256Hex } from "../src/lib/hash.js";
import { checkFlashSize, checkIdentity, normalizeMac } from "../src/lib/identity.js";
import { backupVerified, gate, RISK_BOX_COUNT } from "../src/lib/gating.js";
import { classifyConnectError, classifyOpenError, classifyRequestPortError } from "../src/lib/errors.js";
import { classicResetSteps, hardResetSteps, pinState, runSteps } from "../src/lib/signals.js";
import { timeLeft } from "../src/lib/eta.js";
import * as copy from "../src/lib/copy.js";
import { fakeFlash, partitionEntry, stockTable } from "./helpers.js";

test("only 0x8000 and 0x10000 may be written", () => {
  assert.equal(checkFirmwareWrite(0x8000, 0xc00), "partition-table");
  assert.equal(checkFirmwareWrite(0x10000, 0x1f0000), "app");
  assert.throws(() => checkFirmwareWrite(0x10000, 0x1f0001), /at most/);
  assert.throws(() => checkFirmwareWrite(0x8000, 0x1001), /NVS/);
  for (const off of [0x0, 0x1000, 0x7fff]) assert.throws(() => checkFirmwareWrite(off, 16), /below 0x8000/);
  for (const off of [0x9000, 0xf000, 0x20000, 0x3ff000]) assert.throws(() => checkFirmwareWrite(off, 16), /only 0x8000/);
  assert.throws(() => checkFirmwareWrite(0x10000, 0), /Empty/);
  assert.throws(() => checkFirmwareWrite(65536.5, 4), /whole/);
});

test("a plan may not hit one region twice or be empty", () => {
  assert.throws(() => checkFirmwarePlan([]), /No files/);
  assert.throws(() => checkFirmwarePlan([{ offset: 0x10000, size: 4 }, { offset: 0x10000, size: 8 }]), /Two files/);
  assert.equal(checkFirmwarePlan([{ offset: 0x8000, size: 0xc00 }, { offset: 0x10000, size: 4 }]).length, 2);
});

test("restore writes only the full 4 MB at 0x0", () => {
  checkRestoreWrite(0, 4194304);
  assert.throws(() => checkRestoreWrite(0x1000, 4194304), /0x0 only/);
  assert.throws(() => checkRestoreWrite(0, 4194303), /exactly 4,194,304/);
});

test("partition table parses the stock layout", () => {
  const t = parsePartitionTable(stockTable());
  assert.deepEqual(t.map((p) => [p.label, p.type, p.subtype, p.offset, p.size]), [
    ["nvs", "data", "nvs", 0x9000, 0x6000],
    ["phy_init", "data", "phy", 0xf000, 0x1000],
    ["factory", "app", "factory", 0x10000, 0x100000],
  ]);
  assert.match(formatPartitionTable(t), /factory\s+app\s+factory\s+0x10000\s+0x100000/);
});

test("partition table: ota names, garbage and empty tables", () => {
  const t = new Uint8Array(64).fill(0xff);
  t.set(partitionEntry("ota_0", 0, 0x10, 0x10000, 0x100000));
  assert.equal(parsePartitionTable(t)[0].subtype, "ota_0");
  assert.throws(() => parsePartitionTable(new Uint8Array(64).fill(0xff)), /No partition entries/);
  assert.throws(() => parsePartitionTable(new Uint8Array(64)), /Bad partition entry/);
  assert.throws(() => parsePartitionTable(new Uint8Array(4)), /too short/);
});

test("image sanity checks: size, 0xE9 at 0x1000, 0xAA50 at 0x8000", () => {
  assert.equal(checkFlashImage(fakeFlash()).length, 3);
  assert.throws(() => checkFlashImage(new Uint8Array(100)), /4,194,304/);
  const noBoot = fakeFlash();
  noBoot[0x1000] = 0xff;
  assert.throws(() => checkFlashImage(noBoot), /0xE9/);
  const noTable = fakeFlash();
  noTable[0x8001] = 0;
  assert.throws(() => checkFlashImage(noTable), /0xAA50/);
});

test("md5Hex matches node:crypto for many lengths, incl. 4 MB", () => {
  for (const n of [0, 1, 3, 55, 56, 63, 64, 65, 1000, 4097]) {
    const b = Uint8Array.from({ length: n }, (_, i) => (i * 7 + n) & 0xff);
    assert.equal(md5Hex(b), createHash("md5").update(b).digest("hex"), `len ${n}`);
  }
  const big = fakeFlash();
  assert.equal(md5Hex(big), createHash("md5").update(big).digest("hex"));
  assert.equal(md5Hex(new TextEncoder().encode("abc")), "900150983cd24fb0d6963f7d28e17f72");
});

test("sha256Hex and padTo4", async () => {
  assert.equal(await sha256Hex(new Uint8Array(0)), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  assert.deepEqual([...padTo4(Uint8Array.from([1, 2, 3, 4, 5]))], [1, 2, 3, 4, 5, 0xff, 0xff, 0xff]);
  const four = Uint8Array.from([1, 2, 3, 4]);
  assert.equal(padTo4(four), four);
});

test("identity: ESP32 with 4 MB only, MAC normalized", () => {
  const ok = checkIdentity({ chipName: "ESP32", description: "ESP32-U4WDH (revision v3.0)", flashSizeKB: 4096, mac: "AA:BB:CC:DD:EE:0F" });
  assert.equal(ok.mac, "aa:bb:cc:dd:ee:0f");
  assert.throws(() => checkIdentity({ chipName: "ESP32-S3", flashSizeKB: 4096, mac: "aa:bb:cc:dd:ee:ff" }), /not the Chromatic/);
  assert.throws(() => checkIdentity({ chipName: "ESP32", description: "ESP32-C3", flashSizeKB: 4096, mac: "aa:bb:cc:dd:ee:ff" }), /not the Chromatic/);
  assert.throws(() => checkIdentity({ chipName: "ESP32", flashSizeKB: 8192, mac: "aa:bb:cc:dd:ee:ff" }), /4 MB.*8192 KB/);
  assert.throws(() => checkIdentity({ chipName: "ESP32", flashSizeKB: undefined, mac: "aa:bb:cc:dd:ee:ff" }), /unknown size/);
  assert.throws(() => normalizeMac("nope"), /Not a MAC/);
  assert.equal(normalizeMac("aa-bb-cc-dd-ee-ff"), "aa:bb:cc:dd:ee:ff");
  checkFlashSize(4096);
  assert.throws(() => checkFlashSize(2048), /4 MB/);
});

const base = { connected: true, identified: true, busy: false, backupDownloaded: true, backupVerified: true, manifestReady: true, riskBoxes: [true, true, true] };

test("install stays locked until all 3 checks AND the verified backup", () => {
  assert.equal(RISK_BOX_COUNT, 3);
  assert.equal(copy.CHECKS.length, RISK_BOX_COUNT);
  assert.equal(gate(base).flash, true);
  for (let i = 0; i < 3; i++) {
    const boxes = [true, true, true];
    boxes[i] = false;
    assert.equal(gate({ ...base, riskBoxes: boxes }).flash, false, `box ${i}`);
  }
  assert.equal(gate({ ...base, riskBoxes: [true, true] }).flash, false);
  assert.equal(gate({ ...base, riskBoxes: [true, true, true, true] }).flash, false);
  assert.equal(gate({ ...base, riskBoxes: [true, 1, true] }).flash, false);
  assert.equal(gate({ ...base, backupVerified: false }).flash, false);
  assert.equal(gate({ ...base, busy: true }).flash, false);
  assert.equal(gate({ ...base, connected: false }).flash, false);
  assert.equal(gate({ ...base, manifestReady: false }).flash, false);
  assert.equal(gate({ ...base, connected: false }).connect, true);
});

test("backup counts as verified only when all three checks agree", () => {
  const v = { chipMd5: "a", fileMd5: "a", savedSha256: "s", reselectedSha256: "s", sanityOk: true };
  assert.equal(backupVerified(v), true);
  assert.equal(backupVerified({ ...v, fileMd5: "b" }), false);
  assert.equal(backupVerified({ ...v, reselectedSha256: undefined }), false);
  assert.equal(backupVerified({ ...v, reselectedSha256: "t" }), false);
  assert.equal(backupVerified({ ...v, sanityOk: false }), false);
  assert.equal(backupVerified({ ...v, chipMd5: "", fileMd5: "" }), false);
});

test("errors use Zed's copy word for word", () => {
  const named = (name) => Object.assign(new Error("x"), { name });
  assert.equal(classifyOpenError(named("InvalidStateError")).message, "Another app is using your Chromatic. Close MR Updater, serial monitors, or other tabs, then try again.");
  assert.equal(classifyOpenError(named("NetworkError")).kind, "busy");
  assert.equal(classifyOpenError(named("SecurityError")), null);
  assert.equal(classifyRequestPortError(named("NotFoundError")).message, "We can't see your Chromatic. Make sure it's on, plugged in with a data cable, and you're using desktop Chrome or Edge.");
  assert.equal(classifyConnectError(new Error("Failed to connect with the device")).kind, "not-found");
  assert.equal(copy.ERR_VERIFY_FAILED, "Something didn't match after writing. Don't unplug. Click Restore to write your backup back.");
});

test("page copy: exact checks and status label", () => {
  assert.deepEqual(copy.CHECKS, [
    "My Chromatic is charged and plugged in, and other apps are closed.",
    "This is unofficial, and I'm doing it at my own risk.",
    "I'll keep my backup private, since it can include my Wi-Fi password.",
  ]);
  assert.match(copy.STATUS_LABEL, /^Untested on hardware\./);
  assert.equal(copy.RISK_DETAILS.length, 10);
  assert.ok(copy.RISK_DETAILS.includes("I'll keep both backup files together (the .bin and its .json) and only restore them to the same Chromatic they came from."));
});

test("reset steps: download mode only on DTR=1 RTS=0, both lines every step", async () => {
  const steps = classicResetSteps(50);
  assert.deepEqual(steps.filter((s) => !("wait" in s)).map(pinState), [
    { en: false, io0Low: false },
    { en: true, io0Low: true },
    { en: true, io0Low: false },
  ]);
  assert.deepEqual(hardResetSteps().filter((s) => !("wait" in s)).map(pinState), [
    { en: false, io0Low: false },
    { en: true, io0Low: false },
  ]);
  const calls = [];
  await runSteps({ setSignals: async (s) => calls.push(s) }, steps, async () => {});
  assert.equal(calls.length, 3);
  for (const c of calls) assert.deepEqual(Object.keys(c).sort(), ["dataTerminalReady", "requestToSend"]);
});

test("time left text", () => {
  assert.equal(timeLeft(0, 100, 0), "Starting…");
  assert.equal(timeLeft(50, 100, 120000), "About 2 minutes left");
  assert.equal(timeLeft(90, 100, 90000), "About 10 seconds left");
  assert.equal(timeLeft(99, 100, 99000), "A few seconds left");
  assert.equal(timeLeft(100, 100, 1), "Almost done…");
});
