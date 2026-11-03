#!/usr/bin/env node
/**
 * Build firmware/manifest.json from an ESP-IDF build.
 *
 *   node web/flasher/tools/make-manifest.mjs [--build build] [--out web/flasher/firmware]
 *        [--commit <40-hex sha>] [--repo https://github.com/pileofflapjacks1/chromatic-grok]
 *
 * Copies build/partition_table/partition-table.bin and build/chromatic_grok.bin into --out
 * and writes manifest.json with their offsets, sizes and SHA-256. --commit defaults to
 * `git rev-parse HEAD` and must be the commit the build came from. Publishes nothing.
 */

import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { copyFileSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { parseManifest } from "../src/lib/manifest.js";
import { APP_OFFSET, PARTITION_TABLE_OFFSET } from "../src/lib/constants.js";

function args(argv) {
  const out = {};
  for (let i = 0; i < argv.length; i += 2) {
    if (!argv[i].startsWith("--")) throw new Error(`Unexpected argument ${argv[i]}`);
    out[argv[i].slice(2)] = argv[i + 1];
  }
  return out;
}

export function buildManifest({ commit, repo, version, files }) {
  return parseManifest({
    name: "chromatic-grok",
    title: "Chromatic Grok",
    version,
    commit,
    commitUrl: `${repo.replace(/\/$/, "")}/commit/${commit}`,
    license: "GPL-3.0-only",
    untestedOnHardware: true,
    files,
  });
}

function main() {
  const a = args(process.argv.slice(2));
  const build = resolve(a.build ?? "build");
  const out = resolve(a.out ?? "web/flasher/firmware");
  const repo = a.repo ?? "https://github.com/pileofflapjacks1/chromatic-grok";
  const commit = a.commit ?? execFileSync("git", ["rev-parse", "HEAD"], { encoding: "utf8" }).trim();
  let version = a.version;
  if (!version) {
    try {
      version = JSON.parse(readFileSync(join(build, "project_description.json"), "utf8")).project_version;
    } catch {
      version = "unknown";
    }
  }
  const inputs = [
    { name: "partition-table.bin", src: join(build, "partition_table", "partition-table.bin"), offset: PARTITION_TABLE_OFFSET },
    { name: "chromatic_grok.bin", src: join(build, "chromatic_grok.bin"), offset: APP_OFFSET },
  ];
  mkdirSync(out, { recursive: true });
  const files = inputs.map(({ name, src, offset }) => {
    const data = readFileSync(src);
    copyFileSync(src, join(out, name));
    return { name, offset, size: data.length, sha256: createHash("sha256").update(data).digest("hex") };
  });
  const manifest = buildManifest({ commit, repo, version, files });
  writeFileSync(join(out, "manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
  console.log(`wrote ${join(out, "manifest.json")} for ${commit}`);
}

if (import.meta.url === `file://${process.argv[1]}`) main();
