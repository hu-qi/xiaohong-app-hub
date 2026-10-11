# App Hub 1.54-inch UI implementation

This branch is for the first-generation WS63 XiaoHong **1.54-inch, 240 × 240 pixel** TFT (not a phone or large smartwatch UI). It is based on the screenshot supplied by the user.

## Reference-to-LVGL layout

| Element | Position / size | Styling |
|---|---|---|
| Screen | 0,0 / 240×240 | RGB #0B1424 |
| Title | 12,15 / 75px label | "App Hub" |
| Index | 199,18 / 32px label | Dynamic "1 / 4" |
| Icon tile | 82,47 / 76×76px | #19453E, radius 13px |
| Snake icon | 104,69 / 32×32px | Built-in RGB565 in firmware, no emoji font |
| App name | 0,136 / 240px label | Centered |
| Status | approx y174 | Green Built-in "Installed" or gray "Coming soon" |
| Confirm | 89,202 / 62×25px | Blue #315ECA, M action |
| Navigation | x18 and x198, y202 | Physical +/- keys, label Prev / Next |

App Hub shows **one app at a time**. This build does not support installing or uninstalling apps. "Installed" means built into firmware; the app installer is still a stub.

### Font limitation

The reference screenshot includes a Chinese 12px status caption and emoji-shaped snake. Current code uses small ASCII text and a 32×32 RGB565 snake icon to avoid depending on downloaded fonts. It is a close layout approximation, not a pixel-identical screenshot.

### Input and regression

- VolUp short: previous; VolDown short: next; Wakeup/M short: enter; Wakeup/M long: return.
- The original long VolDown **still conflicts** with brightness-down because it opens App Hub. Resolve this before stable release.
- Wakeup/M 5-second Wi-Fi reconfiguration remains handled in MainTask.
- All LVGL mutations run on LvglTask through the display message queue; change screens before deleting old ones.

### Test / release policy

- Static checks: python3 scripts/check_app_hub_visual.py
- Pixel preview: the companion PNG is a layout-only rendering, **not** an actual LVGL emulator screenshot.
- The cloud build has **not** compiled this branch against WS63 toolchain.
- Existing firmware/ files are from earlier revisions, **not** this UI.
- Run WS63 build, inspect code size and free SRAM, then flash and test on device before merging and issuing Release.
