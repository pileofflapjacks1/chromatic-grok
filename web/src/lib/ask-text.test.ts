import assert from "node:assert/strict";
import test from "node:test";
import { foldReply, grokShowLine, systemPrompt } from "./ask-text.ts";

test("foldReply keeps printable ASCII and caps the length", () => {
  assert.equal(foldReply("Hello, world."), "Hello, world.");
  assert.equal(foldReply("A\nB\tC"), "A B C");
  assert.equal(foldReply("café"), "caf?");
  const long = "a".repeat(300);
  const folded = foldReply(long);
  assert.equal(folded.length, 280);
  assert.ok(folded.endsWith("..."));
});

test("grokShowLine quotes the reply for the mcu console", () => {
  const line = grokShowLine('Say "hi"\nnow');
  assert.equal(line, 'grok show "Say \\"hi\\" now"\n');
  assert.ok(line.length < 1024);
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
