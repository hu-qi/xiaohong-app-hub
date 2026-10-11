import test from "node:test";
import assert from "node:assert/strict";
import { createBridge } from "../bridge/server.mjs";

const HOOK_TOKEN = "h".repeat(40);
const PET_TOKEN = "p".repeat(40);
async function withBridge(fn, clock = Date.now) {
  const { server, store } = createBridge({
    hookToken: HOOK_TOKEN, petToken: PET_TOKEN, clock, ttlMs: 2000
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const base = "http://127.0.0.1:" + server.address().port;
  try { await fn(base, store); }
  finally { await new Promise((resolve) => server.close(resolve)); }
}
function send(base, payload, token = HOOK_TOKEN) {
  return fetch(base + "/v1/events", {
    method: "POST",
    headers: { "content-type": "application/json",
               authorization: "Bearer " + token },
    body: JSON.stringify(payload)
  });
}
function state(base, token = PET_TOKEN) {
  return fetch(base + "/v1/state", {
    headers: { authorization: "Bearer " + token }
  });
}
const allowed = { source: "codearts-cli", session: "project-secret",
                  kind: "tool.execute.before", activity: "editing" };

test("separate credentials and strict least-privilege endpoints", async () => {
  await withBridge(async (base) => {
    assert.equal((await fetch(base + "/healthz")).status, 200);
    assert.equal((await send(base, allowed, PET_TOKEN)).status, 401);
    assert.equal((await state(base, HOOK_TOKEN)).status, 401);
    assert.equal((await state(base)).status, 200);
    assert.equal((await send(base, allowed)).status, 202);
    const result = await (await state(base)).json();
    assert.equal(result.schema, 1);
    assert.equal(result.state, "coding");
    assert.equal(result.activity, "editing");
    assert.equal(result.source, "codearts-cli");
    assert.equal(result.seq, 1);
    assert.notEqual(result.session, "project-secret");
    assert(!JSON.stringify(result).includes("project-secret"));
    assert.equal((await fetch(base + "/v1/events")).status, 404);
    assert.equal((await fetch(base + "/v1/state?token=anything")).status, 404);
    assert.equal((await fetch(base + "/unknown")).status, 404);
  });
});

test("unknown fields, wrong content type and huge inputs rejected without state pollution", async () => {
  await withBridge(async (base, store) => {
    assert.equal((await send(base, { ...allowed, prompt: "PROMPT" })).status, 400);
    assert.equal((await send(base, { ...allowed, command: "rm -rf" })).status, 400);
    assert.equal((await send(base, { ...allowed, args: { secret: "x" } })).status, 400);
    assert.equal((await send(base, { ...allowed, state: "success" })).status, 400);
    assert.equal((await send(base, { ...allowed, source: "third-party" })).status, 400);
    assert.equal((await fetch(base + "/v1/events", {
      method: "POST", headers: { authorization: "Bearer " + HOOK_TOKEN,
                                  "content-type": "text/plain" },
      body: JSON.stringify(allowed)
    })).status, 415);
    assert.equal((await send(base, { ...allowed, session: "x".repeat(1000) })).status, 413);
    assert.equal(store.snapshot().seq, 0);
    assert.equal(store.snapshot().state, "offline");
  });
});

test("stale coding state goes offline, never 'success'", async () => {
  let now = 100000;
  await withBridge(async (base) => {
    assert.equal((await send(base, allowed)).status, 202);
    assert.equal((await (await state(base)).json()).state, "coding");
    now += 1999;
    assert.equal((await (await state(base)).json()).state, "coding");
    now += 1;
    assert.equal((await (await state(base)).json()).state, "offline");
    assert.equal((await send(base, { ...allowed, kind: "chat.finished" })).status, 202);
    assert.equal((await (await state(base)).json()).state, "idle");
  }, () => now);
});

test("missing or identical/weak tokens reject startup", () => {
  assert.throws(() => createBridge({hookToken:"short",petToken:PET_TOKEN}));
  assert.throws(() => createBridge({hookToken:PET_TOKEN,petToken:PET_TOKEN}));
  assert.throws(() => createBridge({hookToken:HOOK_TOKEN,petToken:"short"}));
});
