/** Wizard UI. All safety checks live in lib/ and device.js; this file only wires them to the page. */

import * as copy from "./lib/copy.js";
import { BAUD, FLASH_SIZE, ESPTOOL_JS_VERSION } from "./lib/constants.js";
import { backupFileName, encodeBackup, makeHeader } from "./lib/container.js";
import { classifyRequestPortError, FlasherError } from "./lib/errors.js";
import { timeLeft } from "./lib/eta.js";
import { backupVerified, gate } from "./lib/gating.js";
import { md5Hex, sha256Hex } from "./lib/hash.js";
import { checkFlashImage } from "./lib/image.js";
import { checkManifestFile, parseManifest } from "./lib/manifest.js";
import { formatPartitionTable } from "./lib/partition.js";
import {
  chipMd5,
  connectChromatic,
  PORT_FILTERS,
  readFullFlash,
  resetAndClose,
  restoreBackup,
  writeFirmware,
} from "./device.js";

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => [...document.querySelectorAll(sel)];

const state = {
  session: null, // {loader, transport, mac, description, flashSizeKB}
  busy: false,
  image: null, // last backup image (Uint8Array)
  backupFile: null, // .chromatic bytes
  verify: {},
  checks: copy.CHECKS.map(() => false),
  manifest: null,
  firmware: null, // [{name, offset, data}]
};

/* ---------- page text ---------- */

function fillCopy() {
  $$("[data-status-label]").forEach((el) => (el.textContent = copy.STATUS_LABEL));
  $$("[data-browser-check]").forEach((el) => (el.textContent = copy.BROWSER_CHECK));
  $$("[data-copy]").forEach((el) => (el.textContent = copy[el.dataset.copy]));
  $$("[data-undo]").forEach((el) => (el.textContent = copy.UNDO_LINK));
  const help = copy.helpLink();
  $$("[data-help]").forEach((el) => {
    el.textContent = help.text;
    el.href = help.href;
    el.hidden = !help.show;
    if (el.parentElement) el.parentElement.hidden = !help.show;
  });
  $$("[data-features]").forEach((el) => {
    el.textContent = "";
    for (const part of copy.boldParts(copy.FEATURES_LINE)) {
      if (part.strong) {
        const b = document.createElement("strong");
        b.textContent = part.text;
        el.append(b);
      } else el.append(part.text);
    }
  });
  $$("[data-does-not-do]").forEach((el) => (el.textContent = copy.DOES_NOT_DO));
  renderDisclaimer();

  const checks = $("#checks");
  copy.CHECKS.forEach((text, i) => {
    const li = document.createElement("li");
    const label = document.createElement("label");
    const box = document.createElement("input");
    box.type = "checkbox";
    box.addEventListener("change", () => {
      state.checks[i] = box.checked;
      refresh();
    });
    label.append(box, " ", text);
    li.append(label);
    checks.append(li);
  });

  const details = $("#risk-details");
  copy.RISK_DETAILS.forEach((text, i) => {
    const li = document.createElement("li");
    li.append(text);
    if (i === 6) {
      const a = document.createElement("a");
      a.href = "#restore";
      a.textContent = copy.RESTORE_STEPS_LINK_TEXT;
      li.append(a, ".");
    }
    details.append(li);
  });
}

function renderDisclaimer() {
  $$("[data-disclaimer]").forEach((el) => {
    el.textContent = copy.DISCLAIMER_BEFORE_LINK;
    if (state.manifest) {
      const a = document.createElement("a");
      a.href = state.manifest.commitUrl;
      a.textContent = `commit ${state.manifest.commit.slice(0, 12)}`;
      el.append(a);
    } else {
      el.append("(the commit link appears once a build is published)");
    }
  });
}

/* ---------- screens, status, progress ---------- */

function show(name) {
  $$("[data-screen]").forEach((s) => (s.hidden = s.dataset.screen !== name));
  $$("[data-step-tab]").forEach((t) => t.classList.toggle("current", t.dataset.stepTab === name));
  $(".restore-link").hidden = name === "restore";
  if (name === "restore") $("#restore-connect-note").hidden = Boolean(state.session);
}

function log(msg) {
  const pre = $("#log");
  pre.textContent += String(msg).replace(/\r/g, "") + "\n";
  if (pre.textContent.length > 200000) pre.textContent = pre.textContent.slice(-100000);
}

function showError(err) {
  const box = $("#error");
  if (!err) {
    box.hidden = true;
    return;
  }
  const msg = err instanceof FlasherError ? err.message : `Something went wrong: ${err.message || err}`;
  box.textContent = msg;
  box.hidden = false;
  if (err instanceof FlasherError && err.kind === "verify-failed") {
    const a = document.createElement("a");
    a.href = "#restore";
    a.textContent = " Go to Restore.";
    box.append(a);
  }
  if (err && err.detail) log(`detail: ${err.detail}`);
  log(`error: ${err.message || err}`);
}

function progress(name) {
  const wrap = $(`[data-progress="${name}"]`);
  const bar = wrap.querySelector("progress");
  const text = wrap.querySelector("span");
  const t0 = Date.now();
  wrap.hidden = false;
  bar.value = 0;
  text.textContent = "Starting…";
  return {
    set(done, total) {
      bar.value = total ? done / total : 0;
      text.textContent = timeLeft(done, total, Date.now() - t0);
    },
    note(s) {
      text.textContent = s;
    },
    finish(s = "Finished") {
      bar.value = 1;
      text.textContent = s;
    },
  };
}

const status = (key, text) => ($(`[data-${key}-status]`).textContent = text);

function check(label, ok, detail = "") {
  const li = document.createElement("li");
  li.className = ok ? "ok" : "bad";
  li.textContent = label + (detail ? ` (${detail})` : "");
  $("#verify-list").append(li);
}

function refresh() {
  const verified = backupVerified(state.verify);
  const g = gate({
    connected: Boolean(state.session),
    identified: Boolean(state.session),
    busy: state.busy,
    backupDownloaded: Boolean(state.backupFile),
    backupVerified: verified,
    riskBoxes: state.checks,
    manifestReady: Boolean(state.firmware),
  });
  $("#connect").disabled = !g.connect;
  $("#restore-connect").disabled = !g.connect;
  $("#backup").disabled = !g.backup;
  $("#confirm-file").disabled = !g.confirmSaved;
  $("#install").disabled = !g.flash;
  $("#restore-btn").disabled = !(g.restore && $("#restore-file").files.length === 1);
  $("#export-raw").disabled = !state.image || state.busy;
  const hint = !state.firmware
    ? "There's nothing to install yet."
    : !verified
      ? "Install unlocks after your backup is saved and confirmed."
      : !g.allBoxes
        ? "Tick all three boxes to unlock Install."
        : "";
  $("[data-install-hint]").textContent = hint;
}

async function withBusy(fn) {
  state.busy = true;
  refresh();
  showError(null);
  try {
    return await fn();
  } catch (e) {
    showError(e);
    return undefined;
  } finally {
    state.busy = false;
    refresh();
  }
}

async function wakeLock() {
  try {
    return await navigator.wakeLock?.request("screen");
  } catch {
    return null; // not fatal; the risk text asks people to keep the computer awake
  }
}

function download(bytes, name, type = "application/octet-stream") {
  const url = URL.createObjectURL(new Blob([bytes], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 60000);
}

const readFile = async (input) => new Uint8Array(await input.files[0].arrayBuffer());

/* ---------- step 1: connect ---------- */

async function connect(nextScreen) {
  await withBusy(async () => {
    let port;
    try {
      port = await navigator.serial.requestPort({ filters: PORT_FILTERS });
    } catch (e) {
      throw classifyRequestPortError(e) ?? e;
    }
    const p = progress("connect");
    p.note("Connecting…");
    const s = await connectChromatic(port, log);
    state.session = s;
    $("[data-chip]").textContent = s.description;
    $("[data-flash]").textContent = `${s.flashSizeKB / 1024} MB`;
    $("[data-mac]").textContent = s.mac;
    log(`Connected at ${BAUD}: ${s.description}, ${s.flashSizeKB} KB flash, MAC ${s.mac}`);
    p.finish("Connected");
    show(nextScreen);
  });
}

/* ---------- step 2: backup + automatic verify ---------- */

async function backup() {
  await withBusy(async () => {
    const { loader, mac } = state.session;
    state.verify = {};
    state.image = state.backupFile = null;
    $("#verify-list").textContent = "";
    $("#confirm-saved").hidden = true;
    const lock = await wakeLock();
    try {
      const p = progress("backup");
      const image = await readFullFlash(loader, (done, total) => p.set(done, total));
      p.note("Checking the copy…");
      const fileMd5 = md5Hex(image);
      const onChip = await chipMd5(loader, 0, FLASH_SIZE);
      check("Chip and copy match (MD5 of 0x0..0x400000)", onChip === fileMd5, `${onChip} / ${fileMd5}`);
      let sanityOk = false;
      try {
        const table = checkFlashImage(image);
        $("#partitions").textContent = formatPartitionTable(table);
        check("0xE9 at 0x1000 and 0xAA50 at 0x8000", true);
        sanityOk = true;
      } catch (e) {
        check("0xE9 at 0x1000 and 0xAA50 at 0x8000", false, e.message);
      }
      if (onChip !== fileMd5 || !sanityOk) {
        throw new Error("The copy didn't check out. Nothing was changed on your Chromatic. Please click Save my backup again.");
      }
      const date = new Date();
      const header = makeHeader({ mac, sha256: await sha256Hex(image), date });
      const file = encodeBackup(header, image);
      state.image = image;
      state.backupFile = file;
      state.verify = { chipMd5: onChip, fileMd5, sanityOk, savedSha256: await sha256Hex(file) };
      download(file, backupFileName(date));
      p.finish("Backup saved");
      status("backup", "Your backup was saved.");
      $("#confirm-saved").hidden = false;
    } finally {
      await lock?.release?.();
    }
  });
}

async function confirmSaved() {
  const input = $("#confirm-file");
  if (input.files.length !== 1) return;
  const sha = await sha256Hex(await readFile(input));
  state.verify.reselectedSha256 = sha;
  const ok = sha === state.verify.savedSha256;
  check("Saved file re-selected and SHA-256 matches", ok, sha);
  status("confirm", ok ? "Confirmed. Your backup is safe on your computer." : "That isn't the backup file we just saved. Please pick the newest chromatic-backup file.");
  refresh();
  if (backupVerified(state.verify)) show("install");
}

function exportRaw() {
  if (!state.image) return;
  download(state.image, backupFileName(new Date(), "bin"));
}

/* ---------- step 3: install ---------- */

async function loadManifest() {
  try {
    const res = await fetch("./firmware/manifest.json", { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const m = parseManifest(await res.json());
    const files = [];
    for (const f of m.files) {
      const r = await fetch(`./firmware/${f.name}`, { cache: "no-store" });
      if (!r.ok) throw new Error(`${f.name}: HTTP ${r.status}`);
      const data = new Uint8Array(await r.arrayBuffer());
      await checkManifestFile(f, data, sha256Hex);
      files.push({ name: f.name, offset: f.offset, data });
    }
    state.manifest = m;
    state.firmware = files;
    $("#manifest").textContent = m.files.map((f) => `@0x${f.offset.toString(16)} ${f.size} B sha256 ${f.sha256}`).join("\n");
  } catch (e) {
    log(`No installable build: ${e.message}`);
    $("#manifest").textContent = "No build available yet (details in the log).";
  }
  renderCards();
  renderDisclaimer();
  refresh();
}

/** One card per build that really exists in the manifest. Nothing for unbuilt features. */
function renderCards() {
  const box = $("#cards");
  box.textContent = "";
  const m = state.manifest;
  if (!m) return;
  const card = document.createElement("div");
  card.className = "card";
  const h = document.createElement("h3");
  h.textContent = m.title || m.name;
  const label = document.createElement("p");
  label.className = "untested";
  label.textContent = copy.STATUS_LABEL;
  const scope = document.createElement("p");
  scope.textContent = copy.SCOPE_LINE;
  const src = document.createElement("p");
  const a = document.createElement("a");
  a.href = m.commitUrl;
  a.textContent = `Source code (GPL-3.0), commit ${m.commit.slice(0, 12)}`;
  src.append(a);
  card.append(h, label, scope, src);
  for (const f of m.files) {
    const p = document.createElement("p");
    const dl = document.createElement("a");
    dl.href = `./firmware/${f.name}`;
    dl.download = f.name;
    dl.textContent = `Download install file ${m.files.indexOf(f) + 1} of ${m.files.length}`;
    const tag = document.createElement("span");
    tag.className = "untested";
    tag.textContent = copy.STATUS_LABEL;
    p.append(dl, " ", tag);
    card.append(p);
  }
  const grok = document.createElement("div");
  grok.className = "card";
  const gh = document.createElement("h4");
  gh.append("Grok link ");
  const tag = document.createElement("span");
  tag.className = "tag";
  tag.textContent = "Advanced, needs setup";
  gh.append(tag);
  const gp = document.createElement("p");
  gp.className = "note";
  gp.textContent = copy.GROK_LINK_NOTE;
  grok.append(gh, gp);
  card.append(grok);
  box.append(card);
}

async function install() {
  await withBusy(async () => {
    if (!backupVerified(state.verify)) throw new Error("Back up and confirm first.");
    const { loader, transport } = state.session;
    const lock = await wakeLock();
    try {
      const p = progress("install");
      const total = state.firmware.reduce((n, f) => n + f.data.length, 0);
      await writeFirmware(loader, state.firmware, (i, written, all) => {
        const before = state.firmware.slice(0, i).reduce((n, f) => n + f.data.length, 0);
        p.set(before + (all ? (written / all) * state.firmware[i].data.length : 0), total);
      });
      p.finish("Installed and checked");
      await resetAndClose(loader, transport);
      state.session = null;
      show("done");
    } finally {
      await lock?.release?.();
    }
  });
}

/* ---------- restore ---------- */

async function restore() {
  await withBusy(async () => {
    const { loader, transport, mac } = state.session;
    const file = await readFile($("#restore-file"));
    const lock = await wakeLock();
    try {
      const p = progress("restore");
      await restoreBackup(loader, mac, file, (_i, written, all) => p.set(written, all));
      p.finish("Restored and checked");
      status("restore", "Your Chromatic is back the way it was. It's restarting now.");
      await resetAndClose(loader, transport);
      state.session = null;
    } finally {
      await lock?.release?.();
    }
  });
}

/* ---------- start ---------- */

function supported() {
  const mobile = navigator.userAgentData?.mobile ?? /Android|iPhone|iPad|Mobile/i.test(navigator.userAgent);
  return "serial" in navigator && window.isSecureContext && !mobile;
}

function route() {
  if (location.hash === "#restore") show("restore");
}

function main() {
  fillCopy();
  if (!supported()) {
    $("#unsupported").hidden = false;
    return;
  }
  $("#app").hidden = false;
  log(`esptool-js ${ESPTOOL_JS_VERSION}, ${BAUD} baud`);
  $("#connect").addEventListener("click", () => connect("backup"));
  $("#restore-connect").addEventListener("click", () => connect("restore"));
  $("#backup").addEventListener("click", backup);
  $("#confirm-file").addEventListener("change", confirmSaved);
  $("#install").addEventListener("click", install);
  $("#restore-file").addEventListener("change", refresh);
  $("#restore-btn").addEventListener("click", restore);
  $("#export-raw").addEventListener("click", exportRaw);
  window.addEventListener("hashchange", route);
  navigator.serial.addEventListener("disconnect", () => {
    if (state.session && !state.busy) {
      state.session = null;
      log("Chromatic unplugged.");
      refresh();
    }
  });
  show("connect");
  route();
  refresh();
  loadManifest();
}

main();
