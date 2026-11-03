/** What the page lets the user do, from the current state. Pure, so it can be tested. */

export const RISK_BOX_COUNT = 3;

/**
 * @param {{connected:boolean, identified:boolean, busy:boolean, backupVerified:boolean,
 *          riskBoxes:boolean[], manifestReady:boolean}} s
 */
export function gate(s) {
  const allBoxes = Array.isArray(s.riskBoxes) && s.riskBoxes.length === RISK_BOX_COUNT && s.riskBoxes.every((x) => x === true);
  const ready = s.connected && s.identified && !s.busy;
  return {
    connect: !s.connected && !s.busy,
    backup: ready,
    confirmSaved: ready && s.backupDownloaded === true,
    flash: ready && s.backupVerified === true && allBoxes && s.manifestReady === true,
    restore: ready,
    allBoxes,
  };
}

/**
 * Step 7 is only "passed" when all three parts passed for the same backup.
 * @param {{chipMd5?:string, fileMd5?:string, savedSha256?:string, reselectedSha256?:string, sanityOk?:boolean}} v
 */
export function backupVerified(v) {
  return Boolean(
    v.chipMd5 && v.fileMd5 && v.chipMd5 === v.fileMd5 &&
    v.savedSha256 && v.reselectedSha256 && v.savedSha256 === v.reselectedSha256 &&
    v.sanityOk === true,
  );
}
