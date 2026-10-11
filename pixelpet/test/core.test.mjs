import test from "node:test";
import assert from "node:assert/strict";
import { createStateStore, eventState, parseEvent } from "../bridge/core.mjs";

test("state transitions never interpret a completed tool as successful tests", () => {
  const kind = (name, activity="none") => eventState({ kind: name, activity });
  assert.equal(kind("chat.message"), "thinking");
  assert.equal(kind("tool.execute.before", "editing"), "coding");
  assert.equal(kind("tool.execute.before", "testing"), "running");
  assert.equal(kind("tool.execute.after"), "thinking");
  assert.equal(kind("chat.finished"), "idle");
  assert.equal(kind("turn.end"), "idle");
  assert.equal(kind("chat.error"), "error");
  assert.equal(kind("user.approval"), "waiting_approval");
  assert.equal(kind("permission.ask"), "waiting_approval");
});

test("strict event model rejects prompts, output, unknown sessions, and arbitrary state", () => {
  const good = { source: "codearts-cli", kind: "chat.message", session: "secret-123" };
  assert.deepEqual(parseEvent(good), { ...good, activity: "none" });
  for (const payload of [
    { ...good, prompt: "private" },
    { ...good, output: "source code" },
    { ...good, state: "success" },
    { ...good, source: "other" },
    { ...good, kind: "tool.execute.unknown" },
    { ...good, session: "x".repeat(129) },
    { ...good, session: 4 },
    null, [], "hello"
  ]) assert.throws(() => parseEvent(payload));
});

test("session id hashed; event expires to offline; sequence stays monotonic", () => {
  let time = 1000000;
  const store = createStateStore({ clock: () => time, ttlMs: 3000 });
  assert.equal(store.snapshot().state, "offline");
  let frame = store.ingest({
    source: "codearts-ide", kind: "chat.message", session: "sensitive-id"
  });
  assert.equal(frame.state, "thinking");
  assert.equal(frame.seq, 1);
  assert.notEqual(frame.session, "sensitive-id");
  assert.match(frame.session, /^[0-9a-f]{12}$/);
  time += 1000;
  frame = store.ingest({
    source: "codearts-ide", kind: "tool.execute.before",
    activity: "editing", session: "sensitive-id"
  });
  assert.equal(frame.state, "coding");
  assert.equal(frame.seq, 2);
  time += 2999;
  assert.equal(store.snapshot().state, "coding");
  time += 1;
  assert.equal(store.snapshot().state, "offline");
  assert.equal(store.snapshot().seq, 2);
  assert.throws(() => createStateStore({ ttlMs: 1 }));
  assert.throws(() => createStateStore({ ttlMs: 100000 }));
});
