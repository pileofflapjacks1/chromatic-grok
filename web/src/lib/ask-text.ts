/** Plain-ASCII folding and the short-reply system prompt for the handheld. */

export const REPLY_MAX = 280;

const BASE_PROMPT =
  "You are answering on a 160x144 handheld. Reply in under 280 characters. Plain ASCII only. No markdown, no lists, no emoji. A few sentences at most.";

export function foldReply(text: string, maxChars = REPLY_MAX): string {
  let out = "";
  for (const ch of text) {
    const code = ch.codePointAt(0) ?? 0;
    if (ch === "\n" || ch === "\r" || ch === "\t") {
      if (out.length > 0 && !out.endsWith(" ")) out += " ";
    } else if (code >= 32 && code <= 126) {
      out += ch;
    } else if (code > 126) {
      out += "?";
    }
  }
  const folded = out.replace(/ +/g, " ").trim();
  if (folded.length <= maxChars) return folded;
  if (maxChars <= 3) return folded.slice(0, maxChars);
  return `${folded.slice(0, maxChars - 3).trimEnd()}...`;
}

/** One quoted `grok show` line, same escaping as modretrochromatic.grok.me. */
export function grokShowLine(reply: string): string {
  const cleaned = foldReply(reply).replaceAll("\\", "\\\\").replaceAll('"', '\\"');
  return `grok show "${cleaned}"\n`;
}

/**
 * Generic short-reply prompt when no bot is selected, or when the bot is Plain Grok.
 * Any other name is spoken in that bot's voice, with its job as the instruction.
 */
export function systemPrompt(botName: string, botJob: string): string {
  const name = cleanLabel(botName, 40);
  const job = cleanLabel(botJob, 80);
  if (!name) return BASE_PROMPT;
  if (name === "Plain Grok" && (!job || job.toLowerCase() === "no special instructions")) {
    return BASE_PROMPT;
  }
  if (!job || job.toLowerCase() === "no special instructions") {
    return `${BASE_PROMPT} Speak as ${name}.`;
  }
  return `${BASE_PROMPT} Speak as ${name}. ${job}.`;
}

export function cleanLabel(text: string, max: number): string {
  const cleaned = text.replace(/[\u0000-\u001f\u007f]/g, " ").replace(/ +/g, " ").trim();
  return Array.from(cleaned).slice(0, max).join("");
}
