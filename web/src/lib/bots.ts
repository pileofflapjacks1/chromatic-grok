/** Local Grok Bot list. There is no public API for a user's real bots. */

import { cleanLabel } from "./ask-text.ts";

export type Bot = {
  id: string;
  name: string;
  job: string;
  builtin: boolean;
};

export const JOB_MAX = 80;
export const NAME_MAX = 40;
export const STORAGE_KEY = "chromatic.grok.bots.v1";

export const DEFAULT_BOTS: readonly Bot[] = [
  {
    id: "chromatic-guide",
    name: "Chromatic Guide",
    job: "short answers for the handheld",
    builtin: true,
  },
  {
    id: "card-captain",
    name: "Card Captain",
    job: "Pokémon TCG collector tips",
    builtin: true,
  },
  {
    id: "quick-riddle",
    name: "Quick Riddle",
    job: "one tiny riddle or joke",
    builtin: true,
  },
  {
    id: "plain-grok",
    name: "Plain Grok",
    job: "no special instructions",
    builtin: true,
  },
];

const DEFAULT_IDS = new Set(DEFAULT_BOTS.map((bot) => bot.id));

export type BotStore = {
  bots: Bot[];
  selectedId: string;
};

export function freshStore(): BotStore {
  return {
    bots: DEFAULT_BOTS.map((bot) => ({ ...bot })),
    selectedId: "chromatic-guide",
  };
}

export function parseStore(raw: string | null): BotStore {
  const base = freshStore();
  if (!raw) return base;
  let data: unknown;
  try {
    data = JSON.parse(raw);
  } catch {
    return base;
  }
  if (!data || typeof data !== "object") return base;
  const record = data as { selectedId?: unknown; custom?: unknown };
  const custom: Bot[] = [];
  if (Array.isArray(record.custom)) {
    for (const item of record.custom) {
      const bot = customBot(item);
      if (bot) custom.push(bot);
    }
  }
  const bots = [...base.bots, ...custom];
  const known = new Set(bots.map((bot) => bot.id));
  const selectedId = typeof record.selectedId === "string" ? record.selectedId : "";
  return {
    bots,
    selectedId: known.has(selectedId) ? selectedId : "",
  };
}

export function serializeStore(store: BotStore): string {
  return JSON.stringify({
    selectedId: store.selectedId,
    custom: store.bots
      .filter((bot) => !bot.builtin)
      .map(({ id, name, job }) => ({ id, name, job })),
  });
}

export function addBot(store: BotStore, name: string, job: string, id: string): BotStore {
  const cleanedName = cleanLabel(name, NAME_MAX);
  const cleanedJob = cleanLabel(job, JOB_MAX);
  if (!cleanedName || DEFAULT_IDS.has(id) || store.bots.some((bot) => bot.id === id)) {
    return store;
  }
  const bot: Bot = { id, name: cleanedName, job: cleanedJob, builtin: false };
  return {
    bots: [...store.bots, bot],
    selectedId: bot.id,
  };
}

export function removeBot(store: BotStore, id: string): BotStore {
  const target = store.bots.find((bot) => bot.id === id);
  if (!target || target.builtin) return store;
  return {
    bots: store.bots.filter((bot) => bot.id !== id),
    selectedId: store.selectedId === id ? "" : store.selectedId,
  };
}

function customBot(item: unknown): Bot | null {
  if (!item || typeof item !== "object") return null;
  const rec = item as { id?: unknown; name?: unknown; job?: unknown };
  if (typeof rec.id !== "string" || DEFAULT_IDS.has(rec.id)) return null;
  if (typeof rec.name !== "string") return null;
  const name = cleanLabel(rec.name, NAME_MAX);
  if (!name) return null;
  const job = typeof rec.job === "string" ? cleanLabel(rec.job, JOB_MAX) : "";
  return { id: rec.id, name, job, builtin: false };
}
