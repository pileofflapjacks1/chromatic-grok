import { useEffect, useRef, useState, type FormEvent, type KeyboardEvent, type ReactNode, type SVGProps } from "react";
import { askGrok } from "../lib/ask-grok.ts";
import { grokShowLine } from "../lib/ask-text.ts";
import {
  JOB_MAX,
  NAME_MAX,
  STORAGE_KEY,
  addBot,
  freshStore,
  parseStore,
  removeBot,
  serializeStore,
  type Bot,
} from "../lib/bots.ts";

type SerialPortLike = {
  open: (options: { baudRate: number }) => Promise<void>;
  close: () => Promise<void>;
  writable: WritableStream<Uint8Array> | null;
};

type Phase = "idle" | "thinking" | "ready" | "error";

const SUGGESTIONS = [
  "What's a good two-minute break?",
  "Explain Wi-Fi in one breath.",
  "Give me a tiny riddle.",
];
const TEST_LINE = "Chromatic link ok.";
const encoder = new TextEncoder();

export function Companion() {
  const initial = freshStore();
  const [bots, setBots] = useState<Bot[]>(initial.bots);
  const [selectedId, setSelectedId] = useState(initial.selectedId);
  const [hydrated, setHydrated] = useState(false);
  const [prompt, setPrompt] = useState("");
  const [reply, setReply] = useState("");
  const [phase, setPhase] = useState<Phase>("idle");
  const [status, setStatus] = useState("The answer shows here, then on the handheld.");
  const [linked, setLinked] = useState(false);
  const [serialOk, setSerialOk] = useState(false);
  const [draftName, setDraftName] = useState("");
  const [draftJob, setDraftJob] = useState("");
  const portRef = useRef<SerialPortLike | null>(null);
  const writerRef = useRef<WritableStreamDefaultWriter<Uint8Array> | null>(null);
  const listRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const stored = parseStore(localStorage.getItem(STORAGE_KEY));
    setBots(stored.bots);
    setSelectedId(stored.selectedId);
    setHydrated(true);
    setSerialOk(serialApi() !== null);
  }, []);

  useEffect(() => {
    if (!hydrated) return;
    localStorage.setItem(STORAGE_KEY, serializeStore({ bots, selectedId }));
  }, [bots, selectedId, hydrated]);

  useEffect(() => {
    if (!selectedId || !listRef.current) return;
    listRef.current
      .querySelector(`[data-bot-id="${CSS.escape(selectedId)}"]`)
      ?.scrollIntoView({ block: "nearest" });
  }, [selectedId]);

  function onListKeyDown(event: KeyboardEvent<HTMLDivElement>) {
    if (event.target instanceof HTMLInputElement) return;
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    event.preventDefault();
    const index = bots.findIndex((bot) => bot.id === selectedId);
    let next = index;
    if (event.key === "ArrowDown") next = Math.min(bots.length - 1, index < 0 ? 0 : index + 1);
    if (event.key === "ArrowUp") next = Math.max(0, index <= 0 ? 0 : index - 1);
    if (event.key === "Home") next = 0;
    if (event.key === "End") next = bots.length - 1;
    const bot = bots[next];
    if (!bot) return;
    setSelectedId(bot.id);
    listRef.current?.querySelector<HTMLButtonElement>(`[data-bot-id="${CSS.escape(bot.id)}"]`)?.focus();
  }

  function onAdd(event: FormEvent) {
    event.preventDefault();
    const name = draftName.trim();
    if (!name) {
      setPhase("error");
      setStatus("Name the bot first.");
      return;
    }
    const next = addBot({ bots, selectedId }, name, draftJob, `user-${crypto.randomUUID()}`);
    setBots(next.bots);
    setSelectedId(next.selectedId);
    setDraftName("");
    setDraftJob("");
  }

  function onRemove(bot: Bot) {
    if (bot.builtin) return;
    if (!window.confirm(`Remove ${bot.name}?`)) return;
    const next = removeBot({ bots, selectedId }, bot.id);
    setBots(next.bots);
    setSelectedId(next.selectedId);
  }

  async function onConnect() {
    const serial = serialApi();
    if (!serial) {
      setStatus("USB serial needs Chrome or Edge on this site. The screen preview still works.");
      return;
    }
    try {
      const port = await serial.requestPort();
      await port.open({ baudRate: 115200 });
      if (!port.writable) throw new Error("The port has no write channel.");
      writerRef.current = port.writable.getWriter();
      portRef.current = port;
      setLinked(true);
      setStatus("Linked. Ask something, then on the Chromatic open the menu and press Start.");
    } catch (err) {
      const message = err instanceof Error ? err.message : "Could not connect.";
      if (/cancel|dismiss|No port/i.test(message)) {
        setStatus("No handheld selected.");
        return;
      }
      setStatus(message);
    }
  }

  async function onDisconnect() {
    try {
      writerRef.current?.releaseLock();
    } catch {
      /* already released */
    }
    writerRef.current = null;
    try {
      await portRef.current?.close();
    } catch {
      /* already closed */
    }
    portRef.current = null;
    setLinked(false);
    setStatus("Unplugged from this page. The cable can stay in.");
  }

  async function sendShow(text: string) {
    const writer = writerRef.current;
    if (!writer) return false;
    try {
      await writer.write(encoder.encode(grokShowLine(text)));
      return true;
    } catch {
      setLinked(false);
      writerRef.current = null;
      setStatus("The link dropped. Connect the Chromatic again.");
      return false;
    }
  }

  async function onSend() {
    const trimmed = prompt.trim();
    if (!trimmed || phase === "thinking") return;
    const selected = bots.find((bot) => bot.id === selectedId);
    setPhase("thinking");
    setStatus("Asking Grok…");
    try {
      const result = await askGrok({
        data: {
          prompt: trimmed,
          botName: selected?.name ?? "",
          botJob: selected?.job ?? "",
        },
      });
      if (!result.ok) {
        setPhase("error");
        setStatus(result.error);
        return;
      }
      setReply(result.text);
      setPhase("ready");
      if (linked) {
        const sent = await sendShow(result.text);
        setStatus(
          sent
            ? "Sent. On the Chromatic, open the menu and press Start."
            : "Reply is on this screen. The handheld did not take it.",
        );
      } else {
        setStatus("Shown here. Connect the Chromatic to send the same line over USB.");
      }
    } catch (err) {
      setPhase("error");
      setStatus(err instanceof Error ? err.message : "Something went wrong.");
    }
  }

  async function onTest() {
    setReply(TEST_LINE);
    setPhase("ready");
    const sent = await sendShow(TEST_LINE);
    setStatus(
      sent
        ? "Test line sent. Open the menu on the Chromatic and press Start."
        : "Connect the Chromatic first, then send the test line.",
    );
  }

  const selected = bots.find((bot) => bot.id === selectedId);

  return (
    <main className="mx-auto flex min-h-dvh w-full max-w-5xl flex-col gap-8 px-4 py-8 sm:px-6 sm:py-12">
      <header className="flex flex-col gap-3 sm:flex-row sm:items-end sm:justify-between">
        <div className="max-w-xl">
          <p className="font-mono text-xs tracking-widest text-ink/60 uppercase">modretrochromatic.grok.me</p>
          <h1 className="mt-2 font-display text-4xl font-extrabold tracking-tight sm:text-5xl">
            Ask Grok. Read it on the Chromatic.
          </h1>
          <p className="mt-3 max-w-md text-base leading-relaxed text-ink/70">
            Type here. The reply is short enough for the handheld screen. Connect once over USB, then press Start on the device.
          </p>
        </div>
        <ConnectButton linked={linked} serialOk={serialOk} onConnect={() => void onConnect()} onDisconnect={() => void onDisconnect()} />
      </header>

      <section className="grid items-start gap-8 lg:grid-cols-[minmax(0,1fr)_320px]">
        <div className="order-2 flex flex-col gap-6 lg:order-1">
          <ol className="grid gap-3 sm:grid-cols-3">
            <Step n="1" title="Plug in" body="USB-C data cable from the Chromatic to this computer." />
            <Step n="2" title="Ask" body="Connect, type a question, and send. Grok keeps the answer short." />
            <Step n="3" title="Press Start" body="On the handheld, open the menu and press Start." />
          </ol>

          <section className="rounded-card border border-ink/10 bg-desk p-4 shadow-lg" aria-labelledby="bots-heading">
            <h2 id="bots-heading" className="font-display text-base font-bold">
              Bots
            </h2>
            <div
              ref={listRef}
              className="bot-list mt-3 flex flex-col gap-1 outline-none focus-visible:outline-2 focus-visible:outline-signal"
              role="listbox"
              aria-label="Bots"
              tabIndex={0}
              onKeyDown={onListKeyDown}
            >
              {bots.map((bot) => {
                const isSelected = bot.id === selectedId;
                return (
                  <div
                    key={bot.id}
                    role="option"
                    aria-selected={isSelected}
                    className={
                      isSelected
                        ? "flex items-stretch rounded-xl bg-signal text-desk"
                        : "flex items-stretch rounded-xl hover:bg-ink/5"
                    }
                  >
                    <button
                      type="button"
                      data-bot-id={bot.id}
                      className="min-h-11 min-w-0 flex-1 px-3 py-2 text-left"
                      onClick={() => setSelectedId(bot.id)}
                    >
                      <span className="block truncate font-display font-bold">{bot.name}</span>
                      <span className="block truncate text-xs opacity-80">{bot.job}</span>
                    </button>
                    {!bot.builtin && (
                      <button
                        type="button"
                        className={isSelected ? "px-3 text-xs underline" : "px-3 text-xs text-signal underline"}
                        onClick={() => onRemove(bot)}
                      >
                        Remove
                      </button>
                    )}
                  </div>
                );
              })}
            </div>
            {!selected && (
              <p className="mt-2 text-xs text-ink/60">No bot selected. The next reply uses the short generic voice.</p>
            )}
            <form onSubmit={onAdd} className="mt-3 grid gap-2 sm:grid-cols-[1fr_1fr_auto] sm:items-end">
              <label className="grid gap-1 text-xs text-ink/55">
                Name
                <input
                  value={draftName}
                  maxLength={NAME_MAX}
                  onChange={(event) => setDraftName(event.target.value)}
                  className="min-h-11 rounded-full border border-ink/15 bg-transparent px-3 text-sm text-ink"
                />
              </label>
              <label className="grid gap-1 text-xs text-ink/55">
                Job
                <input
                  value={draftJob}
                  maxLength={JOB_MAX}
                  onChange={(event) => setDraftJob(event.target.value)}
                  className="min-h-11 rounded-full border border-ink/15 bg-transparent px-3 text-sm text-ink"
                />
              </label>
              <button type="submit" className="min-h-11 rounded-full bg-ink px-4 text-sm font-bold text-desk">
                Add bot
              </button>
            </form>
            <p className="mt-1 font-mono text-xs text-ink/45 tabular-nums">
              {draftJob.length}/{JOB_MAX}
            </p>
          </section>

          <form
            className="rounded-card border border-ink/10 bg-desk p-4 shadow-lg"
            onSubmit={(event) => {
              event.preventDefault();
              void onSend();
            }}
          >
            <label className="font-mono text-xs tracking-widest text-ink/55 uppercase" htmlFor="prompt">
              Question
            </label>
            <textarea
              id="prompt"
              value={prompt}
              maxLength={400}
              rows={4}
              placeholder="What should the Chromatic say?"
              className="mt-2 w-full resize-none bg-transparent font-display text-lg leading-snug outline-none placeholder:text-ink/35"
              onChange={(event) => setPrompt(event.target.value)}
              onKeyDown={(event) => {
                if (event.key === "Enter" && (event.metaKey || event.ctrlKey)) {
                  event.preventDefault();
                  void onSend();
                }
              }}
            />
            <div className="mt-3 flex flex-wrap items-center gap-2">
              <button
                type="submit"
                disabled={!prompt.trim() || phase === "thinking"}
                className="inline-flex min-h-11 items-center gap-2 rounded-full bg-signal px-5 text-sm font-bold text-desk disabled:opacity-40"
              >
                <SendIcon className="size-4" aria-hidden />
                {phase === "thinking" ? "Asking…" : "Send to Grok"}
              </button>
              <button
                type="button"
                disabled={!linked}
                onClick={() => void onTest()}
                className="inline-flex min-h-11 items-center rounded-full border border-ink/15 px-4 text-sm font-semibold text-ink disabled:opacity-40"
              >
                Test the link
              </button>
              <span className="ml-auto font-mono text-xs text-ink/45 tabular-nums">{prompt.trim().length}/400</span>
            </div>
          </form>

          <div className="flex flex-wrap gap-2">
            {SUGGESTIONS.map((suggestion) => (
              <button
                key={suggestion}
                type="button"
                className="min-h-11 rounded-full border border-ink/10 bg-desk/70 px-3 text-left text-sm text-ink/80"
                onClick={() => setPrompt(suggestion)}
              >
                {suggestion}
              </button>
            ))}
          </div>
          <p className="font-mono text-sm leading-relaxed text-ink/70" role="status">
            {status}
          </p>
          {!serialOk && (
            <p className="text-sm leading-relaxed text-ink/60">
              This preview can show the reply. To reach a real Chromatic, open the page in Chrome or Edge on a computer and allow the USB serial port.
            </p>
          )}
        </div>
        <Lcd reply={reply} phase={phase} linked={linked} />
      </section>
    </main>
  );
}

function Step({ n, title, body }: { n: string; title: string; body: string }) {
  return (
    <li className="rounded-2xl border border-ink/10 bg-desk/80 p-3">
      <p className="font-mono text-xs text-ink/45">{n}</p>
      <p className="mt-1 font-display text-base font-bold">{title}</p>
      <p className="mt-1 text-sm leading-snug text-ink/65">{body}</p>
    </li>
  );
}

function ConnectButton({
  linked,
  serialOk,
  onConnect,
  onDisconnect,
}: {
  linked: boolean;
  serialOk: boolean;
  onConnect: () => void;
  onDisconnect: () => void;
}) {
  if (linked) {
    return (
      <button
        type="button"
        onClick={onDisconnect}
        className="inline-flex min-h-11 items-center gap-2 self-start rounded-full bg-ink px-4 text-sm font-bold text-desk"
      >
        <UnplugIcon className="size-4" aria-hidden />
        Disconnect
      </button>
    );
  }
  return (
    <button
      type="button"
      onClick={onConnect}
      disabled={!serialOk}
      className="inline-flex min-h-11 items-center gap-2 self-start rounded-full bg-ink px-4 text-sm font-bold text-desk disabled:opacity-40"
    >
      <CableIcon className="size-4" aria-hidden />
      Connect Chromatic
    </button>
  );
}

function Lcd({ reply, phase, linked }: { reply: string; phase: Phase; linked: boolean }) {
  return (
    <div className="order-1 mx-auto w-full max-w-[320px] lg:order-2 lg:sticky lg:top-8">
      <div className="rounded-4xl bg-ink p-4 text-desk shadow-xl">
        <div className="mb-3 flex items-center justify-between px-1">
          <span className="font-mono text-xs tracking-widest uppercase">Chromatic</span>
          <span className="font-mono text-xs tracking-widest text-lcd uppercase">{linked ? "Linked" : "Preview"}</span>
        </div>
        <div className="lcd-scan relative aspect-[10/9] overflow-hidden rounded-lg bg-lcd p-3 text-ink">
          <p className="font-mono text-xs tracking-widest text-ink/50 uppercase">Grok</p>
          <p className="mt-2 font-mono text-sm leading-5">
            {phase === "thinking" ? "Thinking…" : reply || "Ask Grok. The answer lands on this screen."}
          </p>
        </div>
        <div className="mt-4 grid grid-cols-[1fr_auto] items-center gap-3 px-1 pb-1">
          <div className="grid h-14 w-14 place-items-center rounded-full border-2 border-desk/30">
            <span className="size-6 rounded-sm border-2 border-desk/70" />
          </div>
          <div className="flex gap-2">
            <span className="grid size-11 place-items-center rounded-full bg-desk/15 font-mono text-xs">B</span>
            <span className="grid size-11 place-items-center rounded-full bg-signal font-mono text-xs font-bold text-desk">A</span>
          </div>
        </div>
      </div>
      <p className="mt-3 text-center font-mono text-xs tracking-wide text-ink/50">Menu, then Start, shows the line</p>
    </div>
  );
}

function serialApi(): { requestPort: () => Promise<SerialPortLike> } | null {
  if (typeof navigator === "undefined") return null;
  return (navigator as Navigator & { serial?: { requestPort: () => Promise<SerialPortLike> } }).serial ?? null;
}

function CableIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Icon {...props}>
      <path d="M17 21v-2a1 1 0 0 1-1-1v-1a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v1a1 1 0 0 1-1 1" />
      <path d="M19 15V6.5a1 1 0 0 0-7 0v11a1 1 0 0 1-7 0V9" />
      <path d="M21 21v-2h-4" />
      <path d="M3 5h4V3" />
      <path d="M7 5a1 1 0 0 1 1 1v1a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V6a1 1 0 0 1 1-1V3" />
    </Icon>
  );
}

function SendIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Icon {...props}>
      <path d="M14.536 21.686a.5.5 0 0 0 .937-.024l6.5-19a.496.496 0 0 0-.635-.635l-19 6.5a.5.5 0 0 0-.024.937l7.93 3.18a2 2 0 0 1 1.112 1.11z" />
      <path d="m21.854 2.147-10.94 10.939" />
    </Icon>
  );
}

function UnplugIcon(props: SVGProps<SVGSVGElement>) {
  return (
    <Icon {...props}>
      <path d="m19 5 3-3" />
      <path d="m2 22 3-3" />
      <path d="M6.3 20.3a2.4 2.4 0 0 0 3.4 0L12 18l-6-6-2.3 2.3a2.4 2.4 0 0 0 0 3.4Z" />
      <path d="M7.5 13.5 10 11" />
      <path d="M10.5 16.5 13 14" />
      <path d="m12 6 6 6 2.3-2.3a2.4 2.4 0 0 0 0-3.4l-2.6-2.6a2.4 2.4 0 0 0-3.4 0Z" />
    </Icon>
  );
}

function Icon({ children, ...props }: SVGProps<SVGSVGElement> & { children: ReactNode }) {
  return (
    <svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" {...props}>
      {children}
    </svg>
  );
}
