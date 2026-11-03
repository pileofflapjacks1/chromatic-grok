/** Fake serial ports and a tiny fake ESP32 ROM for tests. No hardware involved. */

import { SlipReassembler, slipEncode } from "../src/lib/slip.js";

/** Deterministic PRNG so chunk splits are reproducible. */
export function rng(seed = 1) {
  let s = seed >>> 0;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 2 ** 32);
}

/** Split bytes into pieces of 1..31 bytes, like the Chromatic bridge. */
export function chunk(bytes, rand = rng(7), min = 1, max = 31) {
  const out = [];
  for (let i = 0; i < bytes.length; ) {
    const n = Math.min(bytes.length - i, min + Math.floor(rand() * (max - min + 1)));
    out.push(bytes.slice(i, i + n));
    i += n;
  }
  return out;
}

export function concat(...parts) {
  const n = parts.reduce((a, p) => a + p.length, 0);
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) {
    out.set(p, o);
    o += p.length;
  }
  return out;
}

/** A port whose readable we feed by hand. */
export function fakeReadablePort() {
  let controller;
  const port = {
    readable: new ReadableStream({ start: (c) => (controller = c) }),
    feed(bytes) {
      controller.enqueue(bytes);
    },
    end() {
      controller.close();
    },
  };
  return port;
}

const le32 = (v) => [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];

/** A ROM response packet. */
export function romResponse(op, val = 0, data = [0, 0]) {
  return Uint8Array.from([0x01, op, data.length & 0xff, data.length >> 8, ...le32(val), ...data]);
}

export const ESP32_MAGIC = 0x00f01d83;

/**
 * A fake SerialPort that behaves like the bridge in front of an ESP32 ROM:
 * boot text after reset, then answers SYNC (0x08) and READ_REG (0x0A), with every
 * reply split into 1-31 byte chunks.
 */
export class FakeChromaticPort {
  constructor({ seed = 3, openError = null, silent = false } = {}) {
    this.rand = rng(seed);
    this.openError = openError;
    this.silent = silent;
    this.signals = [];
    this.opened = false;
    this.closed = false;
    this.commands = [];
    this._stream = null;
    this.writable = null;
  }
  /** Like Web Serial: after a cancel, the next access gets a fresh stream. */
  get readable() {
    if (!this.opened || this.closed) return null;
    if (!this._stream) {
      this._stream = new ReadableStream({
        start: (c) => (this.ctrl = c),
        cancel: () => {
          this._stream = null;
          this.ctrl = null;
        },
      });
    }
    return this._stream;
  }
  getInfo() {
    return { usbVendorId: 0x374e, usbProductId: 0x0101 };
  }
  async open(opts) {
    if (this.openError) throw this.openError;
    this.openOpts = opts;
    this.opened = true;
    const rx = new SlipReassembler();
    this.writable = new WritableStream({
      write: (bytes) => {
        rx.push(bytes);
        let p;
        while ((p = rx.shift())) this.onCommand(p);
      },
    });
  }
  async close() {
    if ((this._stream && this._stream.locked) || this.writable.locked) throw new Error("close() while locked");
    this.closed = true;
  }
  async setSignals(s) {
    this.signals.push({ ...s });
    // Leaving reset (EN high) with IO0 low prints the download-mode banner.
    if (s.requestToSend === false && s.dataTerminalReady === true && !this.silent) {
      this.send(new TextEncoder().encode("ets Jun  8 2016 00:22:57\r\n\r\nrst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))\r\nwaiting for download\r\n"));
    }
  }
  send(bytes) {
    if (!this.readable) return;
    for (const c of chunk(bytes, this.rand)) this.ctrl.enqueue(c);
  }
  onCommand(p) {
    const op = p[1];
    this.commands.push(op);
    if (this.silent) return;
    if (op === 0x08) {
      const all = [];
      for (let i = 0; i < 8; i++) all.push(slipEncode(romResponse(0x08, 0x12345678)));
      this.send(concat(...all));
    } else if (op === 0x0a) {
      this.send(slipEncode(romResponse(0x0a, ESP32_MAGIC)));
    }
  }
}
