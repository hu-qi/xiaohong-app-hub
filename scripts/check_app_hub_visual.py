#!/usr/bin/env python3
"""Check WS63 App Hub UI geometry and safety using source only (not real LVGL)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
app = root / "xiaohong/xiaohong/src/apps"
hub = (app / "app_hub.c").read_text()
icon = (app / "app_hub_icons.c").read_text()
main = (root / "xiaohong/xiaohong/src/main.c").read_text()
disp = (root / "xiaohong/xiaohong/src/display/lvgl_task.c").read_text()
build = (root / "xiaohong/xiaohong/BUILD.gn").read_text()
snake = (app / "snake/snake_game.c").read_text()

def check(cond, title):
    assert cond, title
    print("[PASS]", title)

check('hub_label(s_hub_screen, "App Hub"' in hub, "App Hub header")
check("lv_obj_set_pos(s_tile, 82, 47);" in hub and
      "lv_obj_set_size(s_tile, 76, 76);" in hub, "reference icon tile")
check("lv_obj_set_pos(button, 89, 202);" in hub and
      "lv_obj_set_size(button, 62, 25);" in hub, "reference M button")
check("lv_image_set_src(s_snake_image, &xh_app_hub_snake_icon);" in hub, "embedded sprite")
check(".header.cf = LV_COLOR_FORMAT_RGB565" in icon and
      ".header.w = 32" in icon and ".header.h = 32" in icon, "RGB565 32x32 icon")
check('"src/apps/app_hub_icons.c",' in build, "GN links icon source")
check("s_hub_items[8]" not in hub and "LV_FLEX_FLOW_COLUMN" not in hub, "single focus")
check('"Installed"' in hub and '"Coming soon"' in hub, "honest status labels")
check("msg_recv != eKey_Wakeup_LongPressed_5S" in main, "preserve Wi-Fi reset")
check(disp.index("if (cmd >= eDisp_AppHub_KeyBase)") <
      disp.index("if (cmd >= eDisp_Welcome)"), "early event routing")
check(hub.index("lv_scr_load(s_hub_screen);", hub.index("static void hub_exit_app")) <
      hub.index("lv_obj_del(s_app_container);", hub.index("static void hub_exit_app")),
      "switch to Hub before deleting running app screen")
check(hub.index("lv_scr_load(s_orig_screen);", hub.index("void xh_app_hub_close")) <
      hub.index("hub_destroy_ui();", hub.index("void xh_app_hub_close")),
      "switch to AI before deleting Hub screen")
check(snake.index("if (s_snake_len >= SNAKE_MAX_LEN)", snake.index("static void snake_step")) <
      snake.index("snake_place_food();", snake.index("static void snake_step")),
      "snake full-board edge case")
print("Source checks passed. Actual WS63 build and device testing are still pending.")
