#!/usr/bin/env node
/* Demonstration without CodeArts installed. Sends only allowlisted events. */
const token = process.env.PIXELPET_HOOK_TOKEN;
if (typeof token !== "string" || token.length < 16) {
  console.error("Set PIXELPET_HOOK_TOKEN to the Bridge's local-ingress token");
  process.exit(2);
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const steps = [
  ["chat.message", "none"],
  ["tool.execute.before", "editing"],
  ["tool.execute.after", "none"],
  ["tool.execute.before", "testing"],
  ["chat.finished", "none"]
];
for (const [kind, activity] of steps) {
  const reply = await fetch("http://127.0.0.1:17373/v1/events", {
    method: "POST",
    headers: {
      "Authorization": "Bearer " + token,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      source: "codearts-cli", session: "demo-session", kind, activity
    }),
    signal: AbortSignal.timeout(3000)
  });
  if (!reply.ok) throw new Error("Bridge rejected " + kind + ": HTTP " + reply.status);
  console.log(kind, "=>", activity);
  await sleep(1000);
}
console.log("Demo complete; chat.finished maps to idle, NOT test success.");
