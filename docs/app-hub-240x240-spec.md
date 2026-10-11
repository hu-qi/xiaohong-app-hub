# XiaoHong App Hub — 1.54-inch / 240×240 interaction specification

## Physical constraints
- Board: first-generation WS63 XiaoHong, 1.54-inch ST7789 TFT, **240×240 px**.
- Input: three physical keys `+`, `-`, and Wakeup/M.
- No touch or IMU assumed. The screen is ~27.7 mm across.
- UI rule: **one app per screen, one dominant focal point**, no 2×2 grid, tiny descriptions, or multiple actions.

## Device-side navigation
- `+`: previous app; `-`: next app.
- Short Wakeup/M: launch an available built-in app.
- Long Wakeup/M: leave a running app; long again returns to AI.
- `Coming Soon` means the app is **not implemented**, *not* downloaded/installable.
- Keep the system 5-second Wakeup/M network reset event outside the App Hub key router.
- Current inherited entry shortcut: long `-` on the AI screen opens App Hub. **This replaces the original brightness-down function and requires a separate, tested key-mapping decision before stable release.**

## Exact layout (pixel coordinates)
| UI element | Area |
| --- | --- |
| Header `APP HUB` | x8 y5, width130, embedded 22px font |
| Page counter | x169 y10, width63 |
| Icon tile | x86 y47, 68×68 |
| Application title | x8 y122, width224, embedded 22px font |
| Status (Built In / Coming Soon) | x8 y155 |
| Three-key legend | x5–235 y195 |
| Back hint | x12 y218 |

The LVGL layout uses a small fixed number of objects; it does not create a row of objects per app. All labels use ASCII so rendering does not depend on downloading a Chinese/emoji font.

## Separate the App Hub and the App Store
- **Firmware UI (this PR):** browse registered apps, run built-ins, return safely to AI.
- **Web/mobile management (future):** browse catalog, inspect details, request installation or removal.
- **Device infrastructure (future):** signed manifest, persistent app metadata, download verification, storage partition budgeting, runtime isolation, transactional install/rollback.
- Until package storage and runtime are working, **do not display Install / Installed / Uninstall as real actions** on the device.

## Definition of done
1. Host lint/review of UI, screen lifecycle and snake edge cases.
2. Full WS63 build for target OpenHarmony branch: **not yet executed for this PR**.
3. Flash on first-generation XiaoHong; validate three-key navigation, snake and AI/Wi-Fi regression.
4. Only publish a new `.fwpkg` after a build from an identified commit and attach SHA256 + logs.

## Follow-up technical debt
- Remove the conflict between app-hub entry and brightness-down in AI mode without jeopardizing Wakeup hold-to-reset.
- Add native game-logic tests and hardware memory measurements.
- Reconcile the current repository source with previously uploaded v2.2.0 binary artifacts.
- Design and validate a compact downloadable app format before selecting a WASM/script runtime.
