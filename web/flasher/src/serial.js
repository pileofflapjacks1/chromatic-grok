/**
 * esptool-js 0.5.7 adapted to the Chromatic bridge.
 *
 * - ChromaticTransport replaces the Transport read path with PacketPump, so SLIP
 *   packets split into 1-31 byte USB chunks (and boot text before them) still
 *   arrive whole. flushInput() no longer cancels the reader.
 * - DTR and RTS are always set together in one setSignals() call.
 * - 115200 only. The reader is stopped cleanly before every reset and disconnect.
 */

import { Transport } from "../vendor/esptool-js-0.5.7/bundle.js";
import { BAUD } from "./lib/constants.js";
import { classifyOpenError } from "./lib/errors.js";
import { PacketPump } from "./lib/pump.js";
import { classicResetSteps, hardResetSteps, runSteps } from "./lib/signals.js";

export class ChromaticTransport extends Transport {
  constructor(device, tracing = false) {
    super(device, tracing, true);
    this.pump = new PacketPump(device);
    this._dtr = false;
    this._rts = false;
  }

  async connect(baud = BAUD, serialOptions = {}) {
    if (baud !== BAUD) throw new Error(`The Chromatic bridge runs at ${BAUD} only.`);
    try {
      await this.device.open({ baudRate: BAUD, ...serialOptions, baudRate: BAUD });
    } catch (e) {
      throw classifyOpenError(e) ?? e;
    }
    this.baudrate = BAUD;
    this.pump = new PacketPump(this.device);
    this.pump.start();
  }

  async *read(timeout) {
    for (;;) yield await this.pump.nextPacket(timeout);
  }

  async newRead(numBytes, timeout) {
    return this.pump.readRaw(numBytes, timeout);
  }

  inWaiting() {
    return this.pump.rawWaiting;
  }

  async flushInput() {
    this.pump.clear();
  }

  async *rawRead() {
    throw new Error("rawRead is not used by the Chromatic flasher.");
  }

  async setSignalsTogether(dtr, rts) {
    this._dtr = dtr;
    this._rts = rts;
    this._DTR_state = dtr;
    await this.device.setSignals({ dataTerminalReady: dtr, requestToSend: rts });
  }

  async setRTS(state) {
    await this.setSignalsTogether(this._dtr, state);
  }

  async setDTR(state) {
    await this.setSignalsTogether(state, this._rts);
  }

  /** Stop the reader, run a DTR/RTS sequence, start a fresh reader. */
  async runSignalSteps(steps, restart = true) {
    await this.pump.stop();
    for (const s of steps) {
      if ("wait" in s) await this.sleep(s.wait);
      else await this.setSignalsTogether(s.dtr, s.rts);
    }
    if (restart) {
      this.pump = new PacketPump(this.device);
      this.pump.start();
    }
  }

  async disconnect() {
    await this.pump.stop();
    await this.waitForUnlock(100);
    await this.device.close();
  }
}

/** Classic reset into download mode, with both lines set together. */
export class ChromaticClassicReset {
  constructor(transport, resetDelay) {
    this.transport = transport;
    this.resetDelay = resetDelay;
  }
  async reset() {
    await this.transport.runSignalSteps(classicResetSteps(this.resetDelay));
  }
}

/** Reset into the app. The reader stays stopped; the caller disconnects next. */
export class ChromaticHardReset {
  constructor(transport) {
    this.transport = transport;
  }
  async reset() {
    await this.transport.runSignalSteps(hardResetSteps(), false);
  }
}

export const resetConstructors = {
  classicReset: (t, delay) => new ChromaticClassicReset(t, delay),
  hardReset: (t) => new ChromaticHardReset(t),
};

export { runSteps };
