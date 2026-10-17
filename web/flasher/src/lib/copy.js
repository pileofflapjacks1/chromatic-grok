/**
 * Zed's page copy (flasher-page-copy.md, 2026-10-17), word for word.
 * The 3 checks are the exact text Joe's team sent on 2026-10-17; the copy file's
 * simple version lists 3 slightly different lines (see PR). RISK_BOXES (the old
 * 10-box list) is kept as the full risk text behind "More info".
 */

export const STATUS_LABEL = "Untested on hardware. Not yet tested on a real Chromatic, including whether the stock bootloader starts this build.";

export const DISCLAIMER_BEFORE_LINK = "Unofficial community project. Not affiliated with, endorsed by, or sponsored by ModRetro or Nintendo. ModRetro and Chromatic are trademarks of their owners and are used here only to describe compatibility. This firmware is based on ModRetro's open-source MCU code and released under GPL-3.0. Source for every download: ";

export const RISK_DETAILS = [
  "This changes your Chromatic's built-in chip: its partition layout and app. A failed or interrupted flash can leave it unable to start.",
  "I've downloaded a full 4 MB backup of my current firmware (takes about 6 minutes) and kept it somewhere safe. The Flash button unlocks only after the backup is saved and verified.",
  "My backup can include my saved Wi-Fi password, so I'll keep it private and won't share or upload it.",
  "I'm using desktop Chrome or Edge. I won't use ModRetro's MR Updater for this, since its terms don't allow installing non-ModRetro firmware.",
  "My device is plugged in, charged, and turned on, and I won't close this tab or let my computer sleep until it finishes.",
  "I've closed MR Updater, serial monitors, and any other tab using the Chromatic. Only one app can talk to it at a time.",
  "I understand there's no automatic rollback yet. If something goes wrong, recovery means writing my full backup back over USB: ",
  "I'll keep both backup files together (the .bin and its .json) and only restore them to the same Chromatic they came from.",
  "Changing firmware may affect my warranty or official updates. I've checked ModRetro's terms.",
  "I'm doing this at my own risk.",
];

export const DOES_NOT_DO = "What this does not do: it never erases the whole chip, never changes eFuses, and doesn't touch the bootloader.";

export const GROK_LINK_NOTE = "The Grok link needs a small helper program running on your own computer, with your own Grok API key. The Chromatic talks to it over your local network without encryption (plain HTTP), so use it only on a home network you trust.";

export const ERR_PORT_BUSY = "Another app is using your Chromatic. Close MR Updater, serial monitors, or other tabs, then try again.";
export const ERR_NOT_FOUND = "We can't see your Chromatic. Make sure it's on, plugged in with a data cable, and you're using desktop Chrome or Edge.";
export const ERR_VERIFY_FAILED = "Something didn't match after writing. Don't unplug. Click Restore to write your backup back.";

export const CHECKS = [
  "My Chromatic is charged and plugged in, and other apps are closed.",
  "This is unofficial, and I'm doing it at my own risk.",
  "I'll keep my backup private, since it can include my Wi-Fi password.",
];

export const STEP_CONNECT = "Plug your Chromatic into your computer and turn it on.";
export const STEP_BACKUP = "Saving a copy of your Chromatic first, in case you want to go back. About 6 minutes.";
export const STEP_INSTALL = "Ready. Keep it plugged in until this finishes.";
export const STEP_DONE = "Done! Your Chromatic will restart.";
export const UNDO_LINK = "Undo: put my Chromatic back the way it was.";
export const BROWSER_CHECK = "Open this page in Chrome or Edge on a computer";
export const SCOPE_LINE = "Adds Wi-Fi and Bluetooth features to your Chromatic.";

export const BTN_CONNECT = "Connect";
export const BTN_BACKUP = "Save my backup";
export const BTN_INSTALL = "Install";

/** Placeholder until Carty G's help link exists. */
export const HELP_HREF = "#help-placeholder";
export const HELP_TEXT = "Stuck? Ask Carty G";
/** RISK_DETAILS[6] ends with a link to the restore steps; this is its text. */
export const RESTORE_STEPS_LINK_TEXT = "restore steps";
