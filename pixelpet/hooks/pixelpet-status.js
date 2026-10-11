/**
 * Copy into ~/.codeartsdoer/plugin/pixelpet-status.js (or project .codeartsdoer/plugin/).
 * Observer-only CodeArts CLI / IDE hook. No source text, prompt, command line,
 * tool arguments, stdout, LLM response or secrets are ever forwarded.
 *
 * CodeArts plugin convention: export async plugin factory returning named hooks.
 * Set PIXELPET_HOOK_TOKEN and PIXELPET_SOURCE=codearts-cli|codearts-ide.
 */
const ENDPOINT = "http://127.0.0.1:17373/v1/events";
const ALLOWED_SOURCE = new Set(["codearts-cli", "codearts-ide"]);

function classifyTool(raw) {
  const name = typeof raw === "string" ? raw.toLowerCase() : "";
  // Classify locally. Never transmit the actual tool name / path / arguments.
  if (/(^|[./_-])(write|edit|patch|replace|multiedit|createfile|applypatch)([./_-]|$)/i.test(name)) {
    return "editing";
  }
  if (/(^|[./_-])(test|pytest|jest|vitest)([./_-]|$)/i.test(name)) {
    return "testing";
  }
  if (/(^|[./_-])(bash|shell|exec|terminal|command|run)([./_-]|$)/i.test(name)) {
    return "command";
  }
  return "tool";
}

async function report(kind, input, activity = "none") {
  const token = process.env.PIXELPET_HOOK_TOKEN;
  if (typeof token !== "string" || token.length < 16) return;
  const source = ALLOWED_SOURCE.has(process.env.PIXELPET_SOURCE)
    ? process.env.PIXELPET_SOURCE : "codearts-cli";
  const session = typeof input?.sessionID === "string"
    ? input.sessionID.slice(0, 128) : "";
  try {
    // Short, best-effort observer. A Bridge outage MUST NOT break CodeArts.
    await fetch(ENDPOINT, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Authorization": "Bearer " + token
      },
      body: JSON.stringify({ source, kind, session, activity }),
      signal: AbortSignal.timeout(180)
    });
  } catch {
    // Network telemetry is advisory, never a permission / tool gate.
  }
}

// The hook args/output are deliberately not touched and not serialized.
export const PixelPetStatusPlugin = async () => ({
  "chat.message": async (input, _output) => report("chat.message", input),
  "tool.execute.before": async (input, _output) =>
    report("tool.execute.before", input, classifyTool(input?.tool)),
  "tool.execute.after": async (input, _output) =>
    report("tool.execute.after", input),
  "chat.error": async (input, _output) => report("chat.error", input),
  "chat.finished": async (input, _output) => report("chat.finished", input),
  "turn.end": async (input, _output) => report("turn.end", input),
  "user.approval": async (input, _output) => report("user.approval", input),
  "permission.ask": async (input, _output) => report("permission.ask", input)
});
