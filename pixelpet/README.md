# XiaoHong PixelPet · CodeArts Vibe Coding LAN Bridge

**First runnable milestone:** CodeArts CLI / IDE Hook → developer laptop local Bridge → authenticated, read-only LAN state API → (future) XiaoHong WS63 pixel pet.

This is a **coding-status desktop companion**, not a ChatGPT-like pet or remote code executor. The first stage runs entirely on your Mac/Windows/Linux development computer: **Node.js 22+, zero npm dependencies**.

## 1. Run the bridge

From this repository:

```sh
cd pixelpet
node --version  # v22+
export PIXELPET_HOOK_TOKEN="$(node -e 'console.log(require("crypto").randomBytes(24).toString("hex"))')"
export PIXELPET_PET_TOKEN="$(node -e 'console.log(require("crypto").randomBytes(24).toString("hex"))')"
npm start
```

Both randomly generated tokens must be distinct and at least 16 characters. Keep them out of Git, screenshots, terminal transcripts, and firmware sources.

The default bind is **127.0.0.1:17373**. This is intentional: you can first test the Hook safely without exposing a network port.

In another terminal with the **same `PIXELPET_HOOK_TOKEN`**:

```sh
cd pixelpet
npm run check && npm test
node scripts/demo.mjs
```

For a local status query, with `PIXELPET_PET_TOKEN` set in that terminal:

```sh
curl -s -H "Authorization: Bearer $PIXELPET_PET_TOKEN" http://127.0.0.1:17373/v1/state
```

## 2. Connect CodeArts CLI or IDE

Huawei Cloud's 2026 CodeArts docs describe project/user JS/TS Hook plugins under:

- User: `~/.codeartsdoer/plugin/` or `~/.codeartsdoer/plugins/`
- Project: `<project>/.codeartsdoer/plugin/` or `<project>/.codeartsdoer/plugins/`

Install the supplied observer (choose ONE scope; test CLI and IDE separately):

```sh
mkdir -p ~/.codeartsdoer/plugin
cp hooks/pixelpet-status.js ~/.codeartsdoer/plugin/pixelpet-status.js
```

Ensure the **CodeArts IDE/CLI process** inherits these environment variables:
`PIXELPET_HOOK_TOKEN` (must equal the Bridge's), `PIXELPET_PORT=17373` and either
`PIXELPET_SOURCE=codearts-cli` or `PIXELPET_SOURCE=codearts-ide`. Restart CodeArts after adding the plugin.

On macOS, a GUI IDE opened from Dock might not inherit your interactive shell's `export` values. Configure the IDE's process environment before starting it; do not commit a token or place it in a project source file.

The hook only observes `chat.message`, `tool.execute.before/after`, `chat.error`, `chat.finished`, `turn.end`, `user.approval`, `permission.ask`. Hook calls are **best-effort, 180ms timeout and fail open**; no CodeArts arguments/results/permissions are modified.

The plugin convention follows Huawei Cloud's official documented JS/TS named-hook approach. Actual execution has been tested with a simulated invocation in Node; **a live local CodeArts IDE/CLI integration test is still required**.

- [CodeArts CLI Hooks](https://support.huaweicloud.com/usermanual-cli/codeartsagent_cli_0018.html)
- [CodeArts IDE Hooks](https://support.huaweicloud.com/usermanual-codeartsagent/codeartsagent_ug_0036.html)

## 3. Enable XiaoHong LAN read access (only on a trusted network)

After local testing, find the Mac's **real LAN IP address** (for example `192.168.1.20`) and bind on all interfaces so the local CodeArts Hook still reaches `127.0.0.1`:

```sh
export PIXELPET_BIND=0.0.0.0
export PIXELPET_LAN_ACK=1
npm start
```

Open `http://127.0.0.1:17373/preview` on your development computer for a **live 240×240 pixel-pet browser preview**, then enter the `PIXELPET_PET_TOKEN` when prompted. The token remains only in the current page's input and is not stored. This preview is **not** the XiaoHong device display.

The POST Hook input endpoint **still rejects non-loopback clients**. The GET endpoint is:

```text
GET http://192.168.1.20:17373/v1/state
Authorization: Bearer <PIXELPET_PET_TOKEN>
```

**Security warning:** this MVP speaks plain HTTP, so a Wi-Fi eavesdropper on an untrusted LAN could intercept the bearer token. Use it only on a **private trusted network** during development; do not expose/port-forward to the Internet or use untrusted/shared networks. A production pairing/TLS story is required before broad distribution.

The firmware does not yet consume this endpoint. The WS63 network client, permissions, device pairing, protocol parser and LVGL animations are a **separate, future firmware integration**.

## 4. Wire contract

```json
{
  "schema": 1,
  "source": "codearts-cli",
  "session": "76bc2c41826a",
  "state": "coding",
  "activity": "editing",
  "seq": 15,
  "timestamp_ms": 1791000000000,
  "ttl_ms": 10000
}
```

Only the following hook request keys are accepted: `source`, `kind`, `session` and `activity`. Strings like `prompt`, `code`, `path`, `args`, `output`, `metadata` are explicitly rejected.

- The session ID is replaced by an **ephemeral HMAC pseudonym**; no raw CodeArts session ID is exposed on the LAN.
- `tool.execute.after` → `thinking` (**not** success).
- `chat.finished` / `turn.end` → `idle` (**not** successful tests).
- The bridge expires old activity to `offline` after `ttl_ms`.
- If the Bridge is unreachable, the device itself must show `disconnected`.
- `success` is **not emitted** without a trusted CI/test outcome source (out of scope for this MVP).

## 5. Development / status

```sh
cd pixelpet
npm test
npm run check
```

The test suite checks API authorization, data minimization, hostile payload rejection, timeouts, source normalization, hook invocation, and safe fallback when credentials are absent. GitHub Actions also executes these tests on Node 22.

**Not delivered:** an installed app on WS63, real CodeArts IDE/CLI field confirmation, GitCode downloadable `.xhapp`, a pixel sprite renderer on device, or any remote control of CodeArts.

This PR is deliberately **based on main, independent of PR #2**. PR #2 is the first-gen firmware/App Hub path. Integrate after both are independently validated.
