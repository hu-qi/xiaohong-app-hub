/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#include "app_hub.h"
#include "app_registry.h"
#include "app_installer.h"
#include "lvgl_ui_layout.h"   /* DISP_WIDTH, DISP_HEIGHT, 字体声明 */
#include "key_config.h"       /* eKey_* */
#include "log.h"

#include <string.h>

/* ===== 内部状态 ===== */
static uint8_t s_hub_state = XH_HUB_STATE_HIDDEN;
static int8_t  s_selected  = 0;          /* 当前选中项索引 */
static uint8_t s_app_count = 0;
static const xh_app_entry_t *s_apps = NULL;
static const xh_app_entry_t *s_running_app = NULL;

/* The default system application is the pre-existing AI screen, managed by
 * MainTask/LvglTask; the App Hub only switches focus, not AI task ownership. */
static int8_t hub_default_index(void)
{
    if (s_apps != NULL) {
        for (uint8_t i = 0; i < s_app_count; i++) {
            if (s_apps[i].available && s_apps[i].system_app &&
                s_apps[i].default_app) {
                return (int8_t)i;
            }
        }
    }
    return 0;
}

/* ===== 1.54-inch TFT / 240 x 240 LVGL UI =====
 * Reference layout is measured in physical SCREEN PIXELS (not desktop CSS).
 * Screen: #0B1424; tile: #19453E; primary action: #315ECA.
 * Only ONE app is visible. No false install or uninstall controls.
 */
static lv_obj_t *s_hub_screen = NULL;
static lv_obj_t *s_orig_screen = NULL;
static lv_obj_t *s_app_container = NULL;
static lv_obj_t *s_counter = NULL;
static lv_obj_t *s_tile = NULL;
/* Icons rendered as solid LVGL shapes: no external file/decoder/font dependency. */
#define HUB_ICON_PARTS 14
static lv_obj_t *s_icon_parts[HUB_ICON_PARTS];
static lv_obj_t *s_app_name = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_check_ring = NULL;
static lv_obj_t *s_check_dot = NULL;
static lv_obj_t *s_action_caption = NULL;

/* Main application title can use the optional Montserrat 16 if built in.
 * Existing base firmware's default LVGL font is the safe fallback. */
static const lv_font_t *hub_title_font(void)
{
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
    return &lv_font_montserrat_16;
#else
    return LV_FONT_DEFAULT;
#endif
}

static lv_obj_t *hub_label(lv_obj_t *parent, const char *value,
                            int16_t x, int16_t y, int16_t w,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, w);
    lv_label_set_text(obj, value);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    if (font != NULL) lv_obj_set_style_text_font(obj, font, 0);
    return obj;
}

/* The status icons are intentionally drawn from simple opaque LVGL rectangles.
 * On the WS63, RGB565 image descriptors may require decoder/color-format options
 * that differ from the Linux simulator. Rectangles are always supported and
 * use only 14 preallocated objects. Coordinates are relative to the 76px tile. */
typedef struct { uint8_t x, y, w, h; uint8_t color; uint8_t radius; } hub_icon_part_t;
#define P(x,y,w,h,c,r) {x,y,w,h,c,r}
static const hub_icon_part_t s_icon_ai[] = {
    P(17,14,42,37,0,8), P(22,23,10,10,1,5), P(44,23,10,10,1,5),
    P(29,38,18,4,1,2), P(5,28,11,8,0,3), P(60,28,11,8,0,3),
    P(34,4,8,10,0,3), P(36,1,4,4,2,2), P(24,53,28,5,0,2)
};
static const hub_icon_part_t s_icon_snake[] = {
    P(9,18,12,12,0,6), P(15,9,15,13,0,6), P(26,8,19,12,0,6),
    P(41,15,14,12,0,6), P(42,26,12,15,0,6), P(32,36,18,12,0,6),
    P(20,35,16,12,0,6), P(15,45,14,11,0,5), P(25,50,21,11,0,5),
    P(43,45,14,12,0,5), P(18,16,4,4,1,2), P(12,25,8,3,2,1)
};
static const hub_icon_part_t s_icon_tetris[] = {
    P(16,14,17,17,0,3), P(33,14,17,17,0,3),
    P(33,31,17,17,0,3), P(50,31,17,17,0,3),
    P(12,53,17,11,1,3), P(30,53,17,11,1,3), P(48,53,17,11,1,3)
};
static const hub_icon_part_t s_icon_breakout[] = {
    P(9,11,17,9,0,2), P(29,11,17,9,0,2), P(49,11,17,9,0,2),
    P(16,23,17,9,0,2), P(36,23,17,9,0,2),
    P(45,36,9,9,1,5), P(24,57,30,7,2,4)
};
static const hub_icon_part_t s_icon_duck[] = {
    P(19,27,41,28,0,12), P(42,17,18,22,0,8),
    P(54,28,16,8,1,3), P(52,23,5,5,2,3),
    P(11,52,54,5,3,3), P(17,61,42,4,3,2), P(28,36,18,11,1,7)
};
#undef P

static void hub_draw_icon(uint8_t app_id)
{
    const hub_icon_part_t *parts = s_icon_ai;
    unsigned count = sizeof(s_icon_ai) / sizeof(s_icon_ai[0]);
    switch(app_id) {
        case XH_APP_ID_SNAKE:
            parts=s_icon_snake; count=sizeof(s_icon_snake)/sizeof(parts[0]); break;
        case XH_APP_ID_TETRIS:
            parts=s_icon_tetris; count=sizeof(s_icon_tetris)/sizeof(parts[0]); break;
        case XH_APP_ID_BREAKOUT:
            parts=s_icon_breakout; count=sizeof(s_icon_breakout)/sizeof(parts[0]); break;
        case XH_APP_ID_LIQUID_DUCK:
            parts=s_icon_duck; count=sizeof(s_icon_duck)/sizeof(parts[0]); break;
        default: break;
    }
    static const uint8_t rgb[4][3] = {
        {104,211,132}, {246,203,83}, {21,40,48}, {88,183,219}
    };
    for (unsigned i = 0; i < HUB_ICON_PARTS; i++) {
        if (i < count) {
            const hub_icon_part_t *p = &parts[i];
            lv_obj_set_pos(s_icon_parts[i],p->x,p->y);
            lv_obj_set_size(s_icon_parts[i],p->w,p->h);
            lv_obj_set_style_radius(s_icon_parts[i],p->radius,0);
            lv_obj_set_style_bg_color(s_icon_parts[i],
                lv_color_make(rgb[p->color][0],rgb[p->color][1],rgb[p->color][2]),0);
            lv_obj_remove_flag(s_icon_parts[i],LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_icon_parts[i],LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void hub_update_selection(void)
{
    if (s_counter == NULL || s_apps == NULL || s_app_count == 0U) return;

    const xh_app_entry_t *app = &s_apps[(uint8_t)s_selected];
    const bool built_in = app->available;
    lv_label_set_text_fmt(s_counter, "%u / %u",
                          (unsigned)(s_selected + 1U), (unsigned)s_app_count);
    lv_label_set_text(s_app_name, app->name ? app->name : "Unknown");
    lv_label_set_text(s_state_label,
                      app->system_app ? "System" :
                      built_in ? "Installed" : "Coming soon");
    lv_label_set_text(s_action_caption,
                      app->system_app ? "M  Open" :
                      built_in ? "M  Start" : "M  Soon");

    lv_obj_set_style_bg_color(
        s_tile, built_in ? lv_color_make(25, 69, 62)
                         : lv_color_make(34, 55, 75), 0);
    hub_draw_icon(app->id);
    if (built_in) {
        lv_obj_remove_flag(s_check_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_check_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_state_label, lv_color_make(125, 205, 174), 0);
    } else {
        lv_obj_add_flag(s_check_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_check_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(s_state_label, lv_color_make(161, 172, 191), 0);
    }
}

static void hub_create_ui(void)
{
    if (s_hub_screen != NULL) return;
    s_orig_screen = lv_scr_act();
    s_hub_screen = lv_obj_create(NULL);
    if (s_hub_screen == NULL) return;
    lv_obj_set_size(s_hub_screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(s_hub_screen, lv_color_make(11, 20, 36), 0);
    lv_obj_set_style_bg_opa(s_hub_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_hub_screen, 0, 0);
    lv_obj_set_style_border_width(s_hub_screen, 0, 0);
    lv_obj_clear_flag(s_hub_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Header: y=17; counter right aligned, as in screenshot. */
    hub_label(s_hub_screen, "App Hub", 12, 15, 75, LV_FONT_DEFAULT,
              lv_color_make(246, 249, 255));
    s_counter = hub_label(s_hub_screen, "", 199, 18, 32,
                          LV_FONT_DEFAULT, lv_color_make(143, 158, 181));

    /* Center: 76x76px colored tile x82/y47, sprite independent of font.bin. */
    s_tile = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(s_tile, 82, 47);
    lv_obj_set_size(s_tile, 76, 76);
    lv_obj_set_style_bg_color(s_tile, lv_color_make(25, 69, 62), 0);
    lv_obj_set_style_bg_opa(s_tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tile, 0, 0);
    lv_obj_set_style_pad_all(s_tile, 0, 0);
    lv_obj_set_style_radius(s_tile, 13, 0);
    lv_obj_clear_flag(s_tile, LV_OBJ_FLAG_SCROLLABLE);

    for (unsigned i = 0; i < HUB_ICON_PARTS; ++i) {
        s_icon_parts[i] = lv_obj_create(s_tile);
        lv_obj_set_style_bg_opa(s_icon_parts[i],LV_OPA_COVER,0);
        lv_obj_set_style_border_width(s_icon_parts[i],0,0);
        lv_obj_set_style_pad_all(s_icon_parts[i],0,0);
        lv_obj_clear_flag(s_icon_parts[i],LV_OBJ_FLAG_SCROLLABLE);
    }

    /* One large name and compact status, centered. */
    s_app_name = hub_label(s_hub_screen, "", 0, 136, 240,
                           hub_title_font(), lv_color_make(255, 255, 255));
    s_check_ring = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(s_check_ring, 94, 177);
    lv_obj_set_size(s_check_ring, 10, 10);
    lv_obj_set_style_bg_opa(s_check_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_check_ring, 1, 0);
    lv_obj_set_style_border_color(s_check_ring, lv_color_make(83, 222, 173), 0);
    lv_obj_set_style_radius(s_check_ring, 5, 0);
    lv_obj_set_style_pad_all(s_check_ring, 0, 0);
    s_check_dot = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(s_check_dot, 97, 180);
    lv_obj_set_size(s_check_dot, 4, 4);
    lv_obj_set_style_radius(s_check_dot, 2, 0);
    lv_obj_set_style_bg_color(s_check_dot, lv_color_make(83, 222, 173), 0);
    lv_obj_set_style_border_width(s_check_dot, 0, 0);
    lv_obj_set_style_pad_all(s_check_dot, 0, 0);
    s_state_label = hub_label(s_hub_screen, "", 102, 174, 94,
                              LV_FONT_DEFAULT, lv_color_make(125, 205, 174));

    /* Physical controls, without nested menus: - previous / M / + next. */
    hub_label(s_hub_screen, "-", 18, 202, 26, hub_title_font(),
              lv_color_make(244, 248, 255));
    hub_label(s_hub_screen, "Prev", 7, 224, 45, LV_FONT_DEFAULT,
              lv_color_make(131, 147, 170));
    hub_label(s_hub_screen, "+", 198, 202, 26, hub_title_font(),
              lv_color_make(244, 248, 255));
    hub_label(s_hub_screen, "Next", 188, 224, 45, LV_FONT_DEFAULT,
              lv_color_make(131, 147, 170));

    lv_obj_t *button = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(button, 89, 202);
    lv_obj_set_size(button, 62, 25);
    lv_obj_set_style_bg_color(button, lv_color_make(49, 94, 202), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    s_action_caption = hub_label(button, "", 0, 4, 62,
                                 LV_FONT_DEFAULT, lv_color_make(255, 255, 255));

    hub_update_selection();
    lv_scr_load(s_hub_screen);
}

static void hub_destroy_ui(void)
{
    /* Call only after activating the original AI screen. */
    if (s_hub_screen != NULL) lv_obj_del(s_hub_screen);
    s_hub_screen = NULL;
    s_counter = s_tile = NULL;
    memset(s_icon_parts,0,sizeof(s_icon_parts));
    s_app_name = s_state_label = s_check_ring = s_check_dot = NULL;
    s_action_caption = NULL;
}

static void hub_launch_app(const xh_app_entry_t *app)
{
    if (app == NULL || !app->available || app->on_create == NULL) return;
    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) return;
    lv_obj_set_size(screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Do not switch screens until app creation has succeeded. */
    if (!app->on_create(screen)) {
        lv_obj_del(screen);
        log_error("[AppHub] create failed: %s\r\n", app->name);
        return;
    }
    s_app_container = screen;
    s_running_app = app;
    s_hub_state = XH_HUB_STATE_RUNNING;
    lv_scr_load(screen);
}

static void hub_exit_app(void)
{
    if (s_running_app != NULL && s_running_app->on_destroy != NULL) {
        s_running_app->on_destroy();
    }
    s_running_app = NULL;
    if (s_hub_screen != NULL) lv_scr_load(s_hub_screen);
    if (s_app_container != NULL) {
        lv_obj_del(s_app_container);
        s_app_container = NULL;
    }
    s_hub_state = XH_HUB_STATE_LIST;
}

/* ===== 公共接口实现 ===== */

void xh_app_hub_init(void)
{
    s_apps = xh_app_registry_get_all(&s_app_count);
    s_selected = hub_default_index();
    s_hub_state = XH_HUB_STATE_HIDDEN;
    log_info("[AppHub] init: %u apps registered\r\n", s_app_count);

    /* 初始化安装器接口（预留） */
    xh_app_installer_init();
}

void xh_app_hub_open(void)
{
    if (s_hub_state != XH_HUB_STATE_HIDDEN) {
        return;
    }

    s_selected = hub_default_index();
    s_hub_state = XH_HUB_STATE_LIST;
    hub_create_ui();
    if (s_hub_screen == NULL) {
        /* OOM: never trap input in a Hub that failed to allocate its screen. */
        s_hub_state = XH_HUB_STATE_HIDDEN;
        s_orig_screen = NULL;
        log_error("[AppHub] no memory for App Hub screen\r\n");
        return;
    }
    log_info("[AppHub] opened\r\n");
}

void xh_app_hub_close(void)
{
    if (s_hub_state == XH_HUB_STATE_HIDDEN) {
        return;
    }

    if (s_hub_state == XH_HUB_STATE_RUNNING) {
        hub_exit_app();
    }

    /* First activate AI, then delete App Hub screen. */
    if (s_orig_screen != NULL) lv_scr_load(s_orig_screen);
    hub_destroy_ui();
    s_orig_screen = NULL;
    s_hub_state = XH_HUB_STATE_HIDDEN;
    log_info("[AppHub] closed\r\n");
}

bool xh_app_hub_is_active(void)
{
    return s_hub_state != XH_HUB_STATE_HIDDEN;
}

bool xh_app_hub_handle_key(uint8_t key_event)
{
    if (s_hub_state == XH_HUB_STATE_HIDDEN) {
        return false;   /* 不消费任何按键 */
    }

    if (s_hub_state == XH_HUB_STATE_LIST) {
        /* 应用列表模式 */
        switch (key_event) {
            case eKey_VolUp_ShortPressed:
                /* Physical + : next application, matching the 240px footer. */
                if (s_app_count == 0) return true;
                s_selected++;
                if (s_selected >= (int8_t)s_app_count) s_selected = 0;
                hub_update_selection();
                return true;

            case eKey_VolDw_ShortPressed:
                /* Physical - : previous application. */
                if (s_app_count == 0) return true;
                if (s_selected > 0) s_selected--;
                else s_selected = (int8_t)(s_app_count - 1);
                hub_update_selection();
                return true;

            case eKey_Wakeup_ShortPressed:
                /* 启动选中应用 */
                if (s_selected >= 0 && s_selected < (int8_t)s_app_count) {
                    const xh_app_entry_t *app = &s_apps[s_selected];
                    if (app->system_app &&
                        app->id == XH_APP_ID_ASSISTANT) {
                        /* Restore the existing AI UI. Keep audio/agent running.
                         * Provisioning and assistant startup remain in MainTask. */
                        xh_app_hub_close();
                    } else if (app->available) {
                        hub_launch_app(app);
                    } else {
                        log_info("[AppHub] app not available: %s\r\n", app->name);
                    }
                }
                return true;

            case eKey_Wakeup_LongPressed:
                /* M long: close App Hub. Brightness keys remain system-owned. */
                xh_app_hub_close();
                return true;

            default:
                return true;   /* 消费其他按键，不传递给 AI */
        }
    }

    if (s_hub_state == XH_HUB_STATE_RUNNING && s_running_app != NULL) {
        /* 应用运行模式 — 转发按键 */
        uint8_t app_key = XH_APP_KEY_MAX;

        switch (key_event) {
            case eKey_VolUp_ShortPressed:
                app_key = XH_APP_KEY_UP;
                break;
            case eKey_VolDw_ShortPressed:
                app_key = XH_APP_KEY_DOWN;
                break;
            case eKey_Wakeup_ShortPressed:
                app_key = XH_APP_KEY_CONFIRM;
                break;
            case eKey_Wakeup_LongPressed:
                app_key = XH_APP_KEY_BACK;
                break;
            default:
                break;
        }

        if (app_key < XH_APP_KEY_MAX && s_running_app->on_key != NULL) {
            s_running_app->on_key(app_key);
        }

        /* BACK 键退出应用 */
        if (app_key == XH_APP_KEY_BACK) {
            hub_exit_app();
        }

        return true;   /* 应用运行时消费所有按键 */
    }

    return true;
}

void xh_app_hub_tick(void)
{
    if (s_hub_state == XH_HUB_STATE_RUNNING && s_running_app != NULL) {
        if (s_running_app->on_tick != NULL) {
            s_running_app->on_tick();
        }
    }
}
