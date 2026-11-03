/**
 * DTR/RTS sequences for the Chromatic bridge.
 *
 * EN = NOT RTS. IO0 is pulled low only when DTR=1 and RTS=0.
 * Every step sets both lines in one setSignals() call, because Windows
 * usbser.sys mishandles a change to RTS alone.
 *
 * A step is { dtr, rts } or { wait: ms }.
 */

/** Same timing as esptool's classic reset: hold EN low, release it with IO0 low, then let IO0 go. */
export function classicResetSteps(resetDelay = 50) {
  return [
    { dtr: false, rts: true }, // EN low (reset), IO0 high
    { wait: 100 },
    { dtr: true, rts: false }, // EN high (run), IO0 low -> download mode
    { wait: resetDelay },
    { dtr: false, rts: false }, // IO0 released
  ];
}

/** Reset into the normal app: EN pulse with IO0 high. */
export function hardResetSteps() {
  return [
    { dtr: false, rts: true },
    { wait: 100 },
    { dtr: false, rts: false },
  ];
}

/** What the chip sees for a given pair of lines. */
export function pinState({ dtr, rts }) {
  return { en: !rts, io0Low: dtr && !rts };
}

/** Run steps against something with setSignals() (a SerialPort or a fake). */
export async function runSteps(port, steps, sleep = (ms) => new Promise((r) => setTimeout(r, ms))) {
  for (const s of steps) {
    if ("wait" in s) await sleep(s.wait);
    else await port.setSignals({ dataTerminalReady: s.dtr, requestToSend: s.rts });
  }
}
