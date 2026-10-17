import assert from "node:assert/strict";
import { test } from "node:test";
import { SLIP_END, SLIP_ESC, SlipReassembler, slipEncode } from "../src/lib/slip.js";
import { chunk, concat, rng } from "./fakes.js";

const payload = (n, seed) => {
  const r = rng(seed);
  return Uint8Array.from({ length: n }, () => Math.floor(r() * 256));
};

test("whole packet in one piece", () => {
  const s = new SlipReassembler();
  s.push(slipEncode([1, 2, 3]));
  assert.deepEqual([...s.shift()], [1, 2, 3]);
  assert.equal(s.shift(), undefined);
});

test("escapes for END and ESC are undone", () => {
  const s = new SlipReassembler();
  s.push(slipEncode([SLIP_END, 5, SLIP_ESC]));
  assert.deepEqual([...s.shift()], [SLIP_END, 5, SLIP_ESC]);
});

test("packets split into 1..31 byte pieces come out whole and in order", () => {
  for (let seed = 1; seed <= 25; seed++) {
    const packets = Array.from({ length: 12 }, (_, i) => payload(1 + ((i * 37 + seed) % 300), seed * 100 + i));
    const s = new SlipReassembler();
    for (const c of chunk(concat(...packets.map(slipEncode)), rng(seed))) s.push(c);
    const got = [];
    let p;
    while ((p = s.shift())) got.push(p);
    assert.equal(got.length, packets.length, `seed ${seed}`);
    got.forEach((g, i) => assert.deepEqual(g, packets[i]));
  }
});

test("one byte at a time, split right inside an escape", () => {
  const data = [SLIP_ESC, SLIP_END, 0x10];
  const s = new SlipReassembler();
  for (const b of slipEncode(data)) {
    s.push([b]);
  }
  assert.deepEqual([...s.shift()], data);
});

test("boot text before the first packet is kept as noise, not an error", () => {
  const s = new SlipReassembler();
  const text = new TextEncoder().encode("rst:0x1 (POWERON_RESET),boot:0x3\r\nwaiting for download\r\n");
  for (const c of chunk(concat(text, slipEncode([9, 9])), rng(2))) s.push(c);
  assert.deepEqual([...s.shift()], [9, 9]);
  assert.match(s.takeNoise(), /waiting for download/);
});

test("back-to-back END bytes do not make empty packets", () => {
  const s = new SlipReassembler();
  s.push([SLIP_END, SLIP_END, SLIP_END, 7, SLIP_END]);
  assert.equal(s.pending, 1);
  assert.deepEqual([...s.shift()], [7]);
});

test("a broken escape drops that packet and the next one still arrives", () => {
  const s = new SlipReassembler();
  s.push([SLIP_END, 1, SLIP_ESC, 0x42, 2, SLIP_END]);
  s.push(slipEncode([3]));
  assert.equal(s.badEscapes, 1);
  const got = [];
  let p;
  while ((p = s.shift())) got.push([...p]);
  assert.deepEqual(got.at(-1), [3]);
});

test("partial reports a half-received packet", () => {
  const s = new SlipReassembler();
  s.push([SLIP_END, 1, 2]);
  assert.equal(s.partial, true);
  s.push([SLIP_END]);
  assert.equal(s.partial, false);
});
