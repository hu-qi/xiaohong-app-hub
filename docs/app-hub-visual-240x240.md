# App Hub 1.54-inch UI implementation

This branch is for the first-generation WS63 XiaoHong **1.54-inch, 240 × 240 pixel** TFT (not a phone or large smartwatch UI). It is based on the screenshot supplied by the user.

## Reference-to-LVGL layout

| Element | Position / size | Styling |
|---|---|---|
| Screen | 0,0 / 240×240 | RGB #0B1424 |
| Title | 12,15 / 75px label | "App Hub" |
| Index | 199,18 / 32px label | Dynamic "1 / 4" |
| Icon tile | 82,47 / 76×76px | #19453E, radius 13px |
| Application icon | 82,47 / 76×76px tile | 14 reusable LVGL solid geometry objects, no image decoder / emoji font |
| App name | 0,136 / 240px label | Centered |
| Status | approx y174 | Green Built-in "Installed" or gray "Coming soon" |
| Confirm | 89,202 / 62×25px | Blue #315ECA, M action |
| Navigation | x18 and x198, y202 | Physical +/- keys, label Prev / Next |

App Hub shows **one app at a time**. This build does not support installing or uninstalling apps. "Installed" means built into firmware; the app installer is still a stub.

### Font limitation

The reference image includes compact Chinese labels and a snake icon. Current code uses small ASCII UI text and per-app LVGL primitive drawings (no RGB565 image decoder). It is a close visual approximation, not a pixel-identical screenshot. Five app entries are available, including the resident AI Assistant.

### Input and regression

- VolUp (+) short: **next**; VolDown (-) short: **previous**; Wakeup/M short: enter; Wakeup/M long: return.
- **App Hub entry:** double-click M (two M short-release events within 350ms), handled entirely by MainTask and sent to LvglTask. From AI open the Hub; from the Hub list return to AI; from a game return to the Hub list. Single M is delayed ~350–390ms; M long and M 5-second holds retain their original behavior. **Never hold + and - together**: hardware RESET is reserved. Long + alone increases brightness; long - alone decreases brightness.
- GPIO volume keys use their stock independent handlers. No key combination logic is present in the WS63 app, and no LVGL operation runs in GPIO interrupts.
- Native CI verifies the actual first-generation key driver and explicitly rejects any App Hub software chord event. Hardware reset cannot be overridden by software.
- Wakeup/M 5-second Wi-Fi reconfiguration remains handled in MainTask.
- All LVGL mutations run on LvglTask through the display message queue; change screens before deleting old ones.

### Test / release policy

- Static checks: python3 scripts/check_app_hub_visual.py
- Native App Hub C host testing: tests/host/test_app_hub.c
- Hardware reset reservation test: tests/host/test_key_driver.c (mock GPIO, never exercises real reset)
- GPIO key driver integration: tests/host/test_key_driver.c
- Real LVGL 9.2.2 offscreen 240x240 rendering: tests/real_lvgl/apphub_render.c
- CI produces an actual LVGL 9.2.2 offscreen framebuffer artifact; it is **not** the WS63 device display.
- The cloud build has **not** compiled this branch against WS63 toolchain.
- Existing firmware/ files are from earlier revisions, **not** this UI.
- Run WS63 build, inspect code size and free SRAM, then flash and test on device before merging and issuing Release.

## Default system application (PR #2)

AI Assistant is registered as the first **resident system app**. The App Hub now has five entries; AI Assistant is the initial focus (1/5), while Snake is the next entry (2/5). Selecting AI Assistant returns to the original AI LVGL screen, preserving existing audio and Agent tasks. The original Wi-Fi provisioning and post-provision reboot sequence remain intact; pairing mode blocks the App Hub shortcut. See [boot and default app lifecycle](ai-assistant-default-app.md).

## Playable mini-games

All four built-in games are now implemented and marked available. Control mappings, native LVGL test coverage and memory/heap notes: [App Hub playable games](app-hub-playable-games.md).
