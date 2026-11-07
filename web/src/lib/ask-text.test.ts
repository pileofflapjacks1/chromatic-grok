import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import test from "node:test";
import { foldReply, grokShowLine, systemPrompt } from "./ask-text.ts";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");

function splitArgv(which: "usb" | "ble", line: string): string[] {
  const built = spawnSync("sh", ["tests/build_usb_split_argv.sh"], {
    cwd: repoRoot,
    encoding: "utf8",
  });
  assert.equal(built.status, 0, built.stderr);
  const bin = built.stdout.trim().split("\n").pop();
  assert.ok(bin);
  const parsed = spawnSync(bin, [which, line], { encoding: "utf8" });
  assert.equal(parsed.status, 0, parsed.stderr);
  return JSON.parse(parsed.stdout) as string[];
}

function showText(argv: string[]): string {
  assert.deepEqual(argv.slice(0, 2), ["grok", "show"]);
  assert.equal(argv.length, 3);
  return argv[2] ?? "";
}

test("foldReply keeps printable ASCII and caps the length", () => {
  assert.equal(foldReply("Hello, world."), "Hello, world.");
  assert.equal(foldReply("A\nB\tC"), "A B C");
  assert.equal(foldReply("café"), "caf?");
  const long = "a".repeat(300);
  const folded = foldReply(long);
  assert.equal(folded.length, 280);
  assert.ok(folded.endsWith("..."));
});

test("grokShowLine escapes for the USB console splitter", () => {
  const line = grokShowLine('Say "hi"\nnow');
  assert.equal(line, 'grok show "Say \\"hi\\" now"\n');
  assert.ok(line.length < 1024);
  assert.ok(grokShowLine("\\".repeat(300)).length < 1024);
});

test("USB esp_console_split_argv round-trips folded grok show text", () => {
  const samples = [
    'Say "hi"\nnow',
    'C:\\path "a" \\"b\\"',
    "it's the dog's bowl",
    "''''''",
    "He said \"it's 'fine'\"",
    "hello\\",
    "hello\\n",
    "\\n",
    "a\\ b",
    "say \\'hi",
    "  a\r\nb\tc\u0001d  ",
    "hello\nwifi clear",
    "",
    " \n\t ",
    '""""',
    "curly \u201cquote\u201d",
    '"'.repeat(300),
    "\\".repeat(300),
  ];
  for (const sample of samples) {
    const wire = grokShowLine(sample);
    assert.equal(wire.endsWith("\n"), true);
    assert.equal(wire.slice(0, -1).includes("\n"), false);
    assert.ok(wire.length < 1024, `wire length ${wire.length}`);
    const usb = splitArgv("usb", wire.slice(0, -1));
    assert.equal(showText(usb), foldReply(sample), JSON.stringify(sample));
  }
});

test("USB escaping is not a BLE split_args round-trip", () => {
  const wire = grokShowLine('Say "hi" now').slice(0, -1);
  const folded = foldReply('Say "hi" now');
  assert.equal(showText(splitArgv("usb", wire)), folded);
  const ble = splitArgv("ble", wire);
  assert.notEqual(ble.length === 3 ? ble[2] : ble.join("\n"), folded);
});

test("systemPrompt uses the bot voice and falls back when none is selected", () => {
  const generic = systemPrompt("", "");
  assert.equal(systemPrompt("Plain Grok", "no special instructions"), generic);
  assert.match(generic, /under 280 characters/);
  assert.doesNotMatch(generic, /Speak as/);
  const guide = systemPrompt("Chromatic Guide", "short answers for the handheld");
  assert.match(guide, /Speak as Chromatic Guide/);
  assert.match(guide, /short answers for the handheld/);
  assert.match(guide, /No markdown/);
});
