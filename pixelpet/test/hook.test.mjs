import test from "node:test";
import assert from "node:assert/strict";
import { createBridge } from "../bridge/server.mjs";

test("actual CodeArts hook export reports abstract activity only", async () => {
  const hookToken = "h".repeat(40);
  const petToken = "p".repeat(40);
  const { server, store } = createBridge({ hookToken, petToken });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const previous = {
    PIXELPET_HOOK_TOKEN: process.env.PIXELPET_HOOK_TOKEN,
    PIXELPET_SOURCE: process.env.PIXELPET_SOURCE,
    PIXELPET_PORT: process.env.PIXELPET_PORT
  };
  try {
    process.env.PIXELPET_HOOK_TOKEN = hookToken;
    process.env.PIXELPET_SOURCE = "codearts-ide";
    process.env.PIXELPET_PORT = String(server.address().port);
    // Dynamic import AFTER env port is set.
    const { PixelPetStatusPlugin } = await import("../hooks/pixelpet-status.js");
    const hooks = await PixelPetStatusPlugin({});
    const sensitive = {
      sessionID: "private-dev-session", tool: "edit",
      args: { code: "TOP_SECRET_SOURCE", apiKey: "TOP_SECRET_KEY" }
    };
    const output = { output: "PRIVATE_STDOUT", title: "PRIVATE_TITLE" };
    await hooks["chat.message"]({ ...sensitive, parts: ["PRIVATE_PROMPT"] }, output);
    assert.equal(store.snapshot().state, "thinking");
    await hooks["tool.execute.before"](sensitive, output);
    assert.equal(store.snapshot().state, "coding");
    assert.equal(store.snapshot().activity, "editing");
    await hooks["tool.execute.after"](sensitive, output);
    assert.equal(store.snapshot().state, "thinking"); // NO fabricated success.
    await hooks["permission.ask"](sensitive, output);
    assert.equal(store.snapshot().state, "waiting_approval");
    await hooks["chat.error"]({ ...sensitive, error: "PRIVATE_ERROR_LOG" }, output);
    assert.equal(store.snapshot().state, "error");
    await hooks["chat.finished"](sensitive, output);
    assert.equal(store.snapshot().state, "idle");
    const data = JSON.stringify(store.snapshot());
    for (const secret of ["TOP_SECRET_SOURCE", "TOP_SECRET_KEY",
                          "PRIVATE_STDOUT", "PRIVATE_TITLE",
                          "PRIVATE_PROMPT", "PRIVATE_ERROR_LOG",
                          "private-dev-session"]) {
      assert(!data.includes(secret), "hook leaked: " + secret);
    }
    assert.equal(store.snapshot().source, "codearts-ide");
    assert.equal(store.snapshot().seq, 6);
    // Hooks must always fail open, even with unusable auth.
    process.env.PIXELPET_HOOK_TOKEN = "";
    await hooks["tool.execute.before"](sensitive, output);
    assert.equal(store.snapshot().seq, 6);
    assert.equal(output.output, "PRIVATE_STDOUT"); // unmodified tool output
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) delete process.env[key];
      else process.env[key] = value;
    }
    await new Promise((resolve) => server.close(resolve));
  }
});
