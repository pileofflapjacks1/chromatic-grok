import assert from "node:assert/strict";
import test from "node:test";
import { addBot, freshStore, parseStore, removeBot, serializeStore } from "./bots.ts";

test("defaults survive a reload and cannot be removed", () => {
  const saved = serializeStore(freshStore());
  const loaded = parseStore(saved);
  assert.deepEqual(
    loaded.bots.map((bot) => bot.id),
    ["chromatic-guide", "card-captain", "quick-riddle", "plain-grok"],
  );
  assert.equal(loaded.selectedId, "chromatic-guide");
  assert.equal(removeBot(loaded, "card-captain").bots.length, 4);
});

test("added bots persist and a removed custom bot clears selection", () => {
  const added = addBot(freshStore(), "Deck Coach", "one binder tip", "user-1");
  const loaded = parseStore(serializeStore(added));
  assert.equal(loaded.bots.at(-1)?.name, "Deck Coach");
  assert.equal(loaded.bots.at(-1)?.job, "one binder tip");
  assert.equal(loaded.selectedId, "user-1");
  const removed = removeBot(loaded, "user-1");
  assert.equal(removed.selectedId, "");
  assert.equal(removed.bots.length, 4);
});

test("a missing selection falls back to no bot", () => {
  const loaded = parseStore(JSON.stringify({ selectedId: "gone", custom: [] }));
  assert.equal(loaded.selectedId, "");
  assert.equal(loaded.bots.length, 4);
});

test("job text is capped at 80 characters", () => {
  const added = addBot(freshStore(), "Long", "x".repeat(120), "user-2");
  assert.equal(added.bots.at(-1)?.job.length, 80);
});
