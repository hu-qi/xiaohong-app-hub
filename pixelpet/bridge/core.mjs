// Local-only CodeArts event model. Deliberately discards prompts, source and output.
import { createHmac, randomBytes } from "node:crypto";

export const EVENTS = Object.freeze([
  "chat.message", "tool.execute.before", "tool.execute.after",
  "chat.error", "chat.finished", "turn.end", "user.approval",
  "permission.ask"
]);
export const SOURCES = Object.freeze(["codearts-cli", "codearts-ide"]);
export const ACTIVITIES = Object.freeze(["none", "editing", "command", "testing", "tool"]);
const eventKeys = new Set(["source", "kind", "session", "activity"]);

export function parseEvent(payload) {
  if (!payload || typeof payload !== "object" || Array.isArray(payload) ||
      Object.keys(payload).some((key) => !eventKeys.has(key))) {
    throw new TypeError("invalid event shape");
  }
  const { source, kind, session = "", activity = "none" } = payload;
  if (!SOURCES.includes(source) || !EVENTS.includes(kind) ||
      !ACTIVITIES.includes(activity) || typeof session !== "string" ||
      session.length > 128) {
    throw new TypeError("invalid event fields");
  }
  // Tool outputs, message parts, args and error details are never accepted.
  return { source, kind, session, activity };
}

export function eventState({ kind, activity }) {
  switch (kind) {
    case "chat.message": return "thinking";
    case "tool.execute.before":
      return activity === "editing" ? "coding" : "running";
    case "tool.execute.after": return "thinking"; // NOT a success signal.
    case "user.approval":
    case "permission.ask": return "waiting_approval";
    case "chat.error": return "error";
    case "chat.finished":
    case "turn.end": return "idle"; // End of a turn != successful tests.
    default: return "idle";
  }
}

export function createStateStore({ clock = Date.now, ttlMs = 10000 } = {}) {
  if (!Number.isSafeInteger(ttlMs) || ttlMs < 1000 || ttlMs > 60000) {
    throw new TypeError("ttlMs must be 1000..60000");
  }
  const salt = randomBytes(32);
  let current = null;
  let seq = 0;

  return {
    ingest(input) {
      const evt = parseEvent(input);
      const now = clock();
      seq += 1;
      const session = evt.session
        ? createHmac("sha256", salt).update(evt.session).digest("hex").slice(0, 12)
        : "anonymous";
      current = {
        schema: 1, source: evt.source, session, state: eventState(evt),
        activity: evt.activity, seq, timestamp_ms: now, ttl_ms: ttlMs
      };
      return { ...current };
    },
    snapshot() {
      if (!current) {
        return {
          schema: 1, source: "none", session: "none", state: "offline",
          activity: "none", seq, timestamp_ms: clock(), ttl_ms: ttlMs
        };
      }
      // An inactive CodeArts session is not a successful job. Expire stale state.
      if (clock() - current.timestamp_ms >= ttlMs) {
        return { ...current, state: "offline", activity: "none" };
      }
      return { ...current };
    }
  };
}
