#!/usr/bin/env python3
"""Check WS63 App Hub UI geometry and safety using source only (not real LVGL)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
app = root / "xiaohong/xiaohong/xiaohong/src/apps"
hub = (app / "app_hub.c").read_text()
main = (root / "xiaohong/xiaohong/xiaohong/src/main.c").read_text()
disp = (root / "xiaohong/xiaohong/xiaohong/src/display/lvgl_task.c").read_text()
build = (root / "xiaohong/xiaohong/xiaohong/BUILD.gn").read_text()
snake = (app / "snake/snake_game.c").read_text()

def check(cond, title):
    assert cond, title
    print("[PASS]", title)

check('hub_label(s_hub_screen, "App Hub"' in hub, "App Hub header")
check("lv_obj_set_pos(s_tile, 82, 47);" in hub and
      "lv_obj_set_size(s_tile, 76, 76);" in hub, "reference icon tile")
check("lv_obj_set_pos(button, 89, 202);" in hub and
      "lv_obj_set_size(button, 62, 25);" in hub, "reference M button")
check("hub_draw_icon(app->id);" in hub and
      "#define HUB_ICON_PARTS 14" in hub,
      "all five icons use deterministic LVGL shapes")
check("s_icon_snake[]" in hub and "s_icon_tetris[]" in hub and
      "s_icon_breakout[]" in hub and "s_icon_duck[]" in hub and
      "s_icon_ai[]" in hub, "no placeholder letter icons")
check("lv_image_set_src" not in hub, "no RGB565 image decode dependency")
check('"src/apps/tetris/tetris_game.c",' in build and
      '"src/apps/breakout/breakout_game.c",' in build and
      '"src/apps/liquid_duck/liquid_duck.c",' in build,
      "all game implementations linked in WS63 target")
check("s_hub_items[8]" not in hub and "LV_FLEX_FLOW_COLUMN" not in hub, "single focus")
check('"Installed"' in hub and '"Coming soon"' in hub, "honest install state support")
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
registry = (app / "app_registry.c").read_text()
for game_id in ("XH_APP_ID_TETRIS", "XH_APP_ID_BREAKOUT", "XH_APP_ID_LIQUID_DUCK"):
    row = registry[registry.index(".id         = "+game_id):]
    check(".available  = true" in row.split("},", 1)[0],
          game_id + " available with real implementation")
check('"AI Assistant"' in registry and '.system_app = true' in registry and
      '.default_app = true' in registry, "AI registered as built-in default system app")
check('app->id == XH_APP_ID_ASSISTANT' in hub and
      "xh_app_hub_close();" in hub, "opening AI entry returns to resident screen")
driver = (root / "xiaohong/xiaohong/xiaohong/src/boards/xiaohong_ws63_v1/key_config.c").read_text()
key_header = (root / "xiaohong/xiaohong/xiaohong/src/boards/xiaohong_ws63_v1/key_config.h").read_text()
check("eKey_AppHub_Toggle" not in driver + key_header + main,
      "reserved hardware +/- reset chord must not generate App Hub events")
check("volume_chord.c" not in build and "volume_chord" not in driver,
      "firmware must not link a software +/- combination recognizer")
check("eKey_Wakeup_LongPressed_5S" in main and
      "increase_blk_level();" in main and "decrease_blk_level();" in main,
      "preserve original M 5s Wi-Fi setup and brightness controls")
tap_h = (root / "xiaohong/xiaohong/xiaohong/src/m_double_tap.h").read_text()
tap_c = (root / "xiaohong/xiaohong/xiaohong/src/m_double_tap.c").read_text()
disp_header = (root / "xiaohong/xiaohong/xiaohong/src/display/disp_driver.h").read_text()
check('"src/m_double_tap.c"' in build and
      "XH_M_DOUBLE_TAP_WINDOW_MS 350U" in tap_h,
      "double M is built in the actual WS63 GN target")
check("xh_m_double_tap_short(&m_tap" in main and
      "xh_m_double_tap_poll(&m_tap" in main and
      "dispatch_m_single();" in main,
      "single M is deferred but never dropped on an idle queue")
check("eDisp_AppHub_DoubleM" in main + disp + disp_header and
      "xh_app_hub_handle_key(eKey_Wakeup_LongPressed)" in disp,
      "double M switches AI, Hub, and games through LVGL task")
check("volume_chord" not in tap_c and
      "eKey_AppHub_Toggle" not in main,
      "M double tap is independent of hardware +/- RESET")
print("Source checks passed. Actual WS63 build and device testing are still pending.")
