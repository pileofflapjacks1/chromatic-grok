import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { checkManifestFile, parseManifest } from "../src/lib/manifest.js";
import { sha256Hex } from "../src/lib/hash.js";

const SHA = "0123456789abcdef0123456789abcdef01234567";
const h = (b) => createHash("sha256").update(b).digest("hex");
const pt = new Uint8Array(0xc00).fill(1);
const app = new Uint8Array(5000).fill(2);
const good = () => ({
  name: "chromatic-grok",
  commit: SHA,
  commitUrl: `https://github.com/pileofflapjacks1/chromatic-grok/commit/${SHA}`,
  files: [
    { name: "partition-table.bin", offset: 0x8000, size: pt.length, sha256: h(pt) },
    { name: "chromatic_grok.bin", offset: 0x10000, size: app.length, sha256: h(app) },
  ],
});

test("a good manifest parses", () => {
  assert.equal(parseManifest(good()).files.length, 2);
  assert.equal(parseManifest(JSON.stringify(good())).commit, SHA);
});

test("manifest rejects bad commit, url, names, hashes and offsets", () => {
  const m = (f) => {
    const x = good();
    f(x);
    return () => parseManifest(x);
  };
  assert.throws(m((x) => (x.commit = "abc")), /40-character/);
  assert.throws(m((x) => (x.commitUrl = "http://example.com/" + SHA)), /https/);
  assert.throws(m((x) => (x.commitUrl = "https://github.com/x/y/commit/" + "f".repeat(40))), /same commit/);
  assert.throws(m((x) => (x.files[0].name = "../evil.bin")), /Bad file name/);
  assert.throws(m((x) => (x.files[1].sha256 = "zz")), /Bad sha256/);
  assert.throws(m((x) => (x.files[1].offset = 0x1000)), /below 0x8000/);
  assert.throws(m((x) => (x.files[1].offset = 0x0)), /below 0x8000/);
  assert.throws(m((x) => (x.files[1].size = 0x1f0001)), /at most/);
  assert.throws(m((x) => (x.files = [])), /No files/);
});

test("each file's SHA-256 and size are checked before writing", async () => {
  const m = parseManifest(good());
  await checkManifestFile(m.files[1], app, sha256Hex);
  await assert.rejects(checkManifestFile(m.files[1], app.slice(1), sha256Hex), /manifest says/);
  const tampered = app.slice();
  tampered[0] = 9;
  await assert.rejects(checkManifestFile(m.files[1], tampered, sha256Hex), /does not match/);
});

test("make-manifest builds a valid manifest from fake build output", () => {
  const dir = mkdtempSync(join(tmpdir(), "mkman-"));
  mkdirSync(join(dir, "build", "partition_table"), { recursive: true });
  writeFileSync(join(dir, "build", "partition_table", "partition-table.bin"), pt);
  writeFileSync(join(dir, "build", "chromatic_grok.bin"), app);
  writeFileSync(join(dir, "build", "project_description.json"), JSON.stringify({ project_version: "0.14.0-grok" }));
  const script = join(dirname(fileURLToPath(import.meta.url)), "..", "tools", "make-manifest.mjs");
  execFileSync(process.execPath, [script, "--build", join(dir, "build"), "--out", join(dir, "out"), "--commit", SHA], { stdio: "pipe" });
  const man = parseManifest(readFileSync(join(dir, "out", "manifest.json"), "utf8"));
  assert.equal(man.version, "0.14.0-grok");
  assert.equal(man.license, "GPL-3.0-only");
  assert.deepEqual(man.files.map((f) => [f.name, f.offset, f.size, f.sha256]), [
    ["partition-table.bin", 0x8000, pt.length, h(pt)],
    ["chromatic_grok.bin", 0x10000, app.length, h(app)],
  ]);
  assert.deepEqual(new Uint8Array(readFileSync(join(dir, "out", "chromatic_grok.bin"))), app);
});
