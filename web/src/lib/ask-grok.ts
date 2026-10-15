import { createServerFn } from "@tanstack/react-start";
import { foldReply, systemPrompt } from "./ask-text.ts";

const CHAT_URL = "https://api.x.ai/v1/chat/completions";
const DEFAULT_MODEL = "grok-4.7";

export type AskInput = {
  prompt: string;
  botName?: string;
  botJob?: string;
};

export type AskResult = { ok: true; text: string } | { ok: false; error: string };

export const askGrok = createServerFn({ method: "POST" })
  .validator((data: AskInput) => ({
    prompt: typeof data?.prompt === "string" ? data.prompt.trim().slice(0, 400) : "",
    botName: typeof data?.botName === "string" ? data.botName : "",
    botJob: typeof data?.botJob === "string" ? data.botJob : "",
  }))
  .handler(async ({ data }): Promise<AskResult> => {
    if (!data.prompt) return { ok: false, error: "Type a question first." };
    const key = readSecret("XAI_API_KEY");
    if (!key) return { ok: false, error: "Set XAI_API_KEY in web/.env, then restart the dev server." };
    const model = readSecret("XAI_MODEL") || DEFAULT_MODEL;
    const response = await fetch(CHAT_URL, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${key}`,
      },
      body: JSON.stringify({
        model,
        stream: false,
        reasoning_effort: "low",
        messages: [
          { role: "system", content: systemPrompt(data.botName, data.botJob) },
          { role: "user", content: data.prompt },
        ],
      }),
      signal: AbortSignal.timeout(45_000),
    });
    const payload = (await response.json().catch(() => null)) as ChatPayload | null;
    if (!response.ok) {
      return { ok: false, error: payload?.error?.message || `xAI returned ${response.status}` };
    }
    const text = foldReply(messageText(payload));
    if (!text) return { ok: false, error: "Grok returned an empty reply." };
    return { ok: true, text };
  });

type ChatPayload = {
  error?: { message?: string };
  choices?: Array<{ message?: { content?: unknown; refusal?: string | null } }>;
};

function messageText(payload: ChatPayload | null): string {
  const message = payload?.choices?.[0]?.message;
  if (!message) return "";
  if (typeof message.refusal === "string" && message.refusal.trim()) return message.refusal;
  const content = message.content;
  if (typeof content === "string") return content;
  if (!Array.isArray(content)) return "";
  return content
    .map((part) => {
      if (typeof part === "string") return part;
      if (part && typeof part === "object" && "text" in part && typeof part.text === "string") {
        return part.text;
      }
      return "";
    })
    .join("");
}

function readSecret(name: string): string {
  return process.env[name]?.trim() ?? "";
}
