/**
 * One reader, one loop. PacketPump owns the port's reader for as long as it runs,
 * feeds every chunk to a SlipReassembler, and keeps a short raw tail for the boot log.
 *
 * stop() is the only way the reader goes away: it cancels once, waits for the loop
 * to finish, and releases the lock. Nothing else cancels the reader mid-stream.
 */

import { SlipReassembler } from "./slip.js";

const RAW_KEEP = 4096;

export class PacketPump {
  /** @param {{readable: ReadableStream<Uint8Array>|null}} port */
  constructor(port) {
    this.port = port;
    this.slip = new SlipReassembler();
    this.raw = new Uint8Array(0);
    this.reader = null;
    this.loop = null;
    this.error = null;
    this.waiters = new Set();
    this.chunks = 0;
  }

  get running() {
    return this.loop !== null;
  }

  start() {
    if (this.loop) return;
    if (!this.port.readable) throw new Error("Port is not open.");
    this.error = null;
    this.reader = this.port.readable.getReader();
    const reader = this.reader;
    this.loop = (async () => {
      try {
        for (;;) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value && value.length) this._take(value);
        }
      } catch (e) {
        this.error = e;
      } finally {
        try {
          reader.releaseLock();
        } catch {
          /* already released */
        }
        this._wake();
      }
    })();
  }

  async stop() {
    if (!this.loop) return;
    const loop = this.loop;
    try {
      await this.reader.cancel();
    } catch {
      /* the loop records real errors */
    }
    await loop;
    this.reader = null;
    this.loop = null;
  }

  _take(chunk) {
    this.chunks++;
    this.slip.push(chunk);
    const merged = new Uint8Array(this.raw.length + chunk.length);
    merged.set(this.raw);
    merged.set(chunk, this.raw.length);
    this.raw = merged.length > RAW_KEEP ? merged.slice(merged.length - RAW_KEEP) : merged;
    this._wake();
  }

  _wake() {
    for (const w of this.waiters) w();
    this.waiters.clear();
  }

  _wait(ms) {
    return new Promise((resolve) => {
      const t = setTimeout(() => {
        this.waiters.delete(done);
        resolve(false);
      }, ms);
      const done = () => {
        clearTimeout(t);
        resolve(true);
      };
      this.waiters.add(done);
    });
  }

  /** Next whole SLIP packet. Throws on timeout (esptool-js retries on throw). */
  async nextPacket(timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const p = this.slip.shift();
      if (p) return p;
      if (!this.loop) throw new Error("Serial reader is closed.");
      const left = deadline - Date.now();
      if (left <= 0) {
        throw new Error(this.slip.partial ? "Packet content transfer stopped" : "No serial data received.");
      }
      await this._wait(left);
    }
  }

  /** Raw bytes for the boot-log check. Returns what is there after up to timeoutMs. */
  async readRaw(n, timeoutMs) {
    if (this.raw.length === 0 && this.loop) await this._wait(timeoutMs);
    const out = this.raw.slice(0, n);
    this.raw = this.raw.slice(out.length);
    return out;
  }

  get rawWaiting() {
    return this.raw.length;
  }

  clear() {
    this.slip.reset();
    this.raw = new Uint8Array(0);
  }
}
