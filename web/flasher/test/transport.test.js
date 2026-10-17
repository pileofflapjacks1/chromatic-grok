import assert from "node:assert/strict";
import { test } from "node:test";
import { ESPLoader } from "../vendor/esptool-js-0.5.7/bundle.js";
import { ChromaticTransport, resetConstructors } from "../src/serial.js";
import { FakeChromaticPort } from "./fakes.js";

const quiet = { clean() {}, writeLine() {}, write() {} };

function loaderFor(port) {
  const transport = new ChromaticTransport(port);
  const loader = new ESPLoader({ transport, baudrate: 115200, romBaudrate: 115200, terminal: quiet, resetConstructors });
  return { transport, loader };
}

test("esptool-js syncs and detects ESP32 through 1-31 byte chunks and boot text", async () => {
  const port = new FakeChromaticPort({ seed: 11 });
  const { transport, loader } = loaderFor(port);
  await loader.connect("default_reset", 3, true);
  assert.equal(loader.chip.CHIP_NAME, "ESP32");
  assert.equal(port.openOpts.baudRate, 115200);
  assert.ok(port.commands.includes(0x08));
  await transport.disconnect();
  assert.equal(port.closed, true);
});

test("every signal change sets DTR and RTS together", async () => {
  const port = new FakeChromaticPort();
  const { transport, loader } = loaderFor(port);
  await loader.connect("default_reset", 1, false);
  await loader.after("hard_reset");
  assert.ok(port.signals.length >= 5);
  for (const s of port.signals) {
    assert.deepEqual(Object.keys(s).sort(), ["dataTerminalReady", "requestToSend"]);
  }
  // classic reset: EN low, then EN high with IO0 low, then release
  assert.deepEqual(port.signals.slice(0, 3), [
    { dataTerminalReady: false, requestToSend: true },
    { dataTerminalReady: true, requestToSend: false },
    { dataTerminalReady: false, requestToSend: false },
  ]);
  // hard reset leaves the reader stopped so disconnect can close cleanly
  assert.equal(Boolean(port._stream && port._stream.locked), false);
  await transport.disconnect();
});

test("reader is released before each reset (no read in flight while signals change)", async () => {
  const port = new FakeChromaticPort();
  const seen = [];
  const orig = port.setSignals.bind(port);
  port.setSignals = async (s) => {
    seen.push(Boolean(port._stream && port._stream.locked));
    return orig(s);
  };
  const { transport, loader } = loaderFor(port);
  await loader.connect("default_reset", 1, false);
  assert.ok(seen.length > 0 && seen.every((locked) => locked === false));
  await transport.disconnect();
});

test("other baud rates are refused", async () => {
  const { transport } = loaderFor(new FakeChromaticPort());
  await assert.rejects(transport.connect(460800), /115200 only/);
});

test("busy port maps to Zed's 'port busy' text", async () => {
  for (const name of ["InvalidStateError", "NetworkError"]) {
    const err = Object.assign(new Error("Failed to open serial port."), { name });
    const { transport } = loaderFor(new FakeChromaticPort({ openError: err }));
    await assert.rejects(transport.connect(115200), (e) => e.kind === "busy" && /Another app is using your Chromatic/.test(e.message));
  }
});

test("a silent chip fails to connect after retries", async () => {
  const port = new FakeChromaticPort({ silent: true });
  const { transport, loader } = loaderFor(port);
  loader.DEFAULT_TIMEOUT = 50;
  await assert.rejects(loader.connect("default_reset", 2, false), /Failed to connect/);
  await transport.disconnect();
});
