/** Map low-level failures to Zed's plain-language messages. */

import { ERR_NOT_FOUND, ERR_PORT_BUSY, ERR_VERIFY_FAILED } from "./copy.js";

export class FlasherError extends Error {
  /** @param {"busy"|"not-found"|"verify-failed"|"refused"} kind */
  constructor(kind, message, detail) {
    super(message);
    this.kind = kind;
    this.detail = detail;
  }
}

export const busy = (detail) => new FlasherError("busy", ERR_PORT_BUSY, detail);
export const notFound = (detail) => new FlasherError("not-found", ERR_NOT_FOUND, detail);
export const verifyFailed = (detail) => new FlasherError("verify-failed", ERR_VERIFY_FAILED, detail);

/** SerialPort.open() rejections. */
export function classifyOpenError(e) {
  const name = e && e.name;
  if (name === "InvalidStateError" || name === "NetworkError") return busy(String(e.message || name));
  return null;
}

/** requestPort() rejections: the user closed the chooser, or nothing matched. */
export function classifyRequestPortError(e) {
  if (e && e.name === "NotFoundError") return notFound(String(e.message || e.name));
  return null;
}

/** esptool-js connect() gives up with this text after its sync retries. */
export function classifyConnectError(e) {
  const msg = String((e && e.message) || e);
  if (/Failed to connect|No serial data|sync/i.test(msg)) return notFound(msg);
  return null;
}
