import assert from "node:assert/strict";
import { test } from "node:test";
import { PacketPump } from "../src/lib/pump.js";
import { slipEncode } from "../src/lib/slip.js";
import { chunk, fakeReadablePort } from "./fakes.js";

test("pump hands back whole packets from chunked input", async () => {
  const port = fakeReadablePort();
  const pump = new PacketPump(port);
  pump.start();
  for (const c of chunk(slipEncode([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]))) port.feed(c);
  assert.deepEqual([...(await pump.nextPacket(500))], [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
  await pump.stop();
});

test("nextPacket waits for a packet that arrives later", async () => {
  const port = fakeReadablePort();
  const pump = new PacketPump(port);
  pump.start();
  const pending = pump.nextPacket(1000);
  setTimeout(() => port.feed(slipEncode([42])), 20);
  assert.deepEqual([...(await pending)], [42]);
  await pump.stop();
});

test("nextPacket times out with esptool-style messages", async () => {
  const port = fakeReadablePort();
  const pump = new PacketPump(port);
  pump.start();
  await assert.rejects(pump.nextPacket(30), /No serial data received/);
  port.feed(Uint8Array.from([0xc0, 1, 2]));
  await assert.rejects(pump.nextPacket(30), /Packet content transfer stopped/);
  await pump.stop();
});

test("stop() releases the reader lock and the loop ends", async () => {
  const port = fakeReadablePort();
  const pump = new PacketPump(port);
  pump.start();
  assert.equal(port.readable.locked, true);
  await pump.stop();
  assert.equal(port.readable.locked, false);
  assert.equal(pump.running, false);
  await assert.rejects(pump.nextPacket(10), /closed/);
  await pump.stop(); // twice is fine
});

test("readRaw returns boot text", async () => {
  const port = fakeReadablePort();
  const pump = new PacketPump(port);
  pump.start();
  port.feed(new TextEncoder().encode("boot:0x3"));
  const raw = await pump.readRaw(100, 200);
  assert.equal(new TextDecoder().decode(raw), "boot:0x3");
  assert.equal(pump.rawWaiting, 0);
  await pump.stop();
});
