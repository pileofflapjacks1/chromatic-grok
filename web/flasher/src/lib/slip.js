/**
 * SLIP packet reassembly for the Chromatic USB bridge.
 *
 * The bridge hands over ROM and stub output in small pieces (1 to 31 bytes), and
 * boot text can sit in front of the first packet. This class takes bytes in any
 * split and hands back whole, unescaped packets. Bytes outside a packet are kept
 * as "noise" (for the log) and never thrown at the caller.
 *
 * Written from the SLIP rules (RFC 1055) and the esptool serial protocol.
 */

export const SLIP_END = 0xc0;
export const SLIP_ESC = 0xdb;
export const SLIP_ESC_END = 0xdc;
export const SLIP_ESC_ESC = 0xdd;

const NOISE_KEEP = 4096;

export class SlipReassembler {
  constructor() {
    this.reset();
  }

  reset() {
    /** @type {Uint8Array[]} */
    this.packets = [];
    this.inPacket = false;
    this.escaping = false;
    this.current = [];
    this.noise = [];
    this.badEscapes = 0;
  }

  /** Feed any number of bytes. Returns how many packets are now waiting. */
  push(bytes) {
    for (let i = 0; i < bytes.length; i++) {
      const b = bytes[i];
      if (!this.inPacket) {
        if (b === SLIP_END) {
          this.inPacket = true;
          this.current = [];
          this.escaping = false;
        } else if (this.noise.length < NOISE_KEEP) {
          this.noise.push(b);
        }
        continue;
      }
      if (this.escaping) {
        this.escaping = false;
        if (b === SLIP_ESC_END) this.current.push(SLIP_END);
        else if (b === SLIP_ESC_ESC) this.current.push(SLIP_ESC);
        else {
          // Broken escape: drop this packet and wait for the next frame start.
          this.badEscapes++;
          this.inPacket = false;
          this.current = [];
        }
        continue;
      }
      if (b === SLIP_ESC) {
        this.escaping = true;
      } else if (b === SLIP_END) {
        if (this.current.length === 0) {
          // Two ENDs in a row: the second one starts the real packet.
          continue;
        }
        this.packets.push(Uint8Array.from(this.current));
        this.current = [];
        this.inPacket = false;
      } else {
        this.current.push(b);
      }
    }
    return this.packets.length;
  }

  /** Next whole packet, or undefined. */
  shift() {
    return this.packets.shift();
  }

  get pending() {
    return this.packets.length;
  }

  /** True while part of a packet has arrived but not its end. */
  get partial() {
    return this.inPacket && (this.current.length > 0 || this.escaping);
  }

  /** Text that arrived outside packets (boot log), then cleared. */
  takeNoise() {
    const s = String.fromCharCode(...this.noise);
    this.noise = [];
    return s;
  }
}

/** Encode one packet (for tests and fakes). */
export function slipEncode(data) {
  const out = [SLIP_END];
  for (const b of data) {
    if (b === SLIP_END) out.push(SLIP_ESC, SLIP_ESC_END);
    else if (b === SLIP_ESC) out.push(SLIP_ESC, SLIP_ESC_ESC);
    else out.push(b);
  }
  out.push(SLIP_END);
  return Uint8Array.from(out);
}
