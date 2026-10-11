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

/* ===== 240x240 单焦点布局：一屏只显示一个应用 =====
 * 请避免将 240px 屏幕当作手机应用商店：保持大字号、少文字、
 * 仅呈现当前应用的名称、状态及三个物理按键的操作提示。
 */
static lv_obj_t *s_hub_screen = NULL;
static lv_obj_t *s_orig_screen = NULL;
static lv_obj_t *s_app_container = NULL;
static lv_obj_t *s_hub_counter = NULL;
static lv_obj_t *s_icon_panel = NULL;
static lv_obj_t *s_icon_label = NULL;
static lv_obj_t *s_name_label = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_action_label = NULL;

/* 使用固件自带中文字库的 ASCII 子集；不依赖额外的 font.bin 下载。 */
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_22_Default);

static lv_obj_t *hub_add_label(lv_obj_t *parent, const char *text,
                               int16_t x, int16_t y, int16_t width,
                               const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, color, 0);
    if (font != NULL) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return label;
}

/* 切换选中项时仅更新少量标签，不重新创建 LVGL 对象。 */
static void hub_update_selection(void)
{
    if (s_hub_counter == NULL || s_name_label == NULL) {
        return;
    }
    if (s_apps == NULL || s_app_count == 0) {
        lv_label_set_text(s_hub_counter, "0 / 0");
        lv_label_set_text(s_name_label, "No Apps");
        lv_label_set_text(s_state_label, "EMPTY");
        lv_label_set_text(s_action_label, "M: -");
        lv_label_set_text(s_icon_label, "-");
        return;
    }

    const xh_app_entry_t *app = &s_apps[(uint8_t)s_selected];
    lv_label_set_text_fmt(s_hub_counter, "%u / %u",
                          (unsigned)(s_selected + 1), (unsigned)s_app_count);
    lv_label_set_text(s_icon_label, (app->icon != NULL) ? app->icon : "*");
    lv_label_set_text(s_name_label, (app->name != NULL) ? app->name : "Unknown");
    lv_label_set_text(s_state_label, app->available ? "BUILT IN" : "COMING SOON");
    lv_label_set_text(s_action_label, app->available ? "M: Play" : "M: Soon");
    lv_obj_set_style_bg_color(
        s_icon_panel,
        app->available ? lv_color_make(0x16, 0x65, 0x5D)
                       : lv_color_make(0x36, 0x45, 0x59), 0);
}

static void hub_create_ui(void)
{
    if (s_hub_screen != NULL) {
        return;
    }

    s_orig_screen = lv_scr_act();
    s_hub_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_hub_screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(s_hub_screen, lv_color_make(0x0B, 0x14, 0x24), 0);
    lv_obj_set_style_bg_opa(s_hub_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_hub_screen, 0, 0);
    lv_obj_set_style_border_width(s_hub_screen, 0, 0);
    lv_obj_clear_flag(s_hub_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶栏：应用大厅名称 + 索引。 */
    hub_add_label(s_hub_screen, "APP HUB", 8, 5, 130,
                  &lv_font_SourceHanSerifCN_SemiBold_22_Default,
                  lv_color_make(0xF1, 0xF5, 0xFA));
    s_hub_counter = hub_add_label(s_hub_screen, "", 169, 10, 63,
                                  NULL, lv_color_make(0xB0, 0xC0, 0xD2));

    /* 焦点区：68px 图标 + 22px 名称 + 状态，纵向阅读。 */
    s_icon_panel = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(s_icon_panel, (DISP_WIDTH - 68) / 2, 47);
    lv_obj_set_size(s_icon_panel, 68, 68);
    lv_obj_set_style_pad_all(s_icon_panel, 0, 0);
    lv_obj_set_style_border_width(s_icon_panel, 0, 0);
    lv_obj_set_style_radius(s_icon_panel, 14, 0);
    lv_obj_clear_flag(s_icon_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_icon_label = hub_add_label(s_icon_panel, "S", 0, 20, 68,
                                 &lv_font_SourceHanSerifCN_SemiBold_22_Default,
                                 lv_color_make(0xFF, 0xFF, 0xFF));
    s_name_label = hub_add_label(s_hub_screen, "", 8, 122, DISP_WIDTH - 16,
                                 &lv_font_SourceHanSerifCN_SemiBold_22_Default,
                                 lv_color_make(0xFF, 0xFF, 0xFF));
    s_state_label = hub_add_label(s_hub_screen, "", 8, 155, DISP_WIDTH - 16,
                                  NULL, lv_color_make(0xA5, 0xC9, 0xBC));

    /* 底栏：三键操作，无翻页菜单、详情跳转或虚假的安装按钮。 */
    hub_add_label(s_hub_screen, "[-] Prev", 5, 195, 70,
                  NULL, lv_color_make(0xD1, 0xDE, 0xEF));
    s_action_label = hub_add_label(s_hub_screen, "", 76, 195, 88,
                                   NULL, lv_color_make(0xE4, 0xF1, 0xFF));
    hub_add_label(s_hub_screen, "[+] Next", 165, 195, 70,
                  NULL, lv_color_make(0xD1, 0xDE, 0xEF));
    hub_add_label(s_hub_screen, "Hold M: Back", 12, 218, DISP_WIDTH - 24,
                  NULL, lv_color_make(0x86, 0xA3, 0xC0));

    hub_update_selection();
    lv_scr_load(s_hub_screen);
}

static void hub_destroy_ui(void)
{
    if (s_hub_screen != NULL) {
        /* LVGL 不能销毁当前活动屏幕：先恢复原屏幕。 */
        if (lv_scr_act() == s_hub_screen && s_orig_screen != NULL) {
            lv_scr_load(s_orig_screen);
        }
        lv_obj_del(s_hub_screen);
    }
    s_hub_screen = NULL;
    s_hub_counter = NULL;
    s_icon_panel = NULL;
    s_icon_label = NULL;
    s_name_label = NULL;
    s_state_label = NULL;
    s_action_label = NULL;
}

static void hub_launch_app(const xh_app_entry_t *app)
{
    if (app == NULL || !app->available || app->on_create == NULL) {
        return;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) {
        log_error("[AppHub] failed to allocate app screen\r\n");
        return;
    }
    lv_obj_set_size(screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 构造成功后才切屏；构造失败时释放的不是活动屏幕。 */
    if (!app->on_create(screen)) {
        lv_obj_del(screen);
        log_error("[AppHub] on_create failed: %s\r\n", app->name);
        return;
    }
    s_app_container = screen;
    s_running_app = app;
    s_hub_state = XH_HUB_STATE_RUNNING;
    lv_scr_load(s_app_container);
    log_info("[AppHub] launched: %s\r\n", app->name);
}

static void hub_exit_app(void)
{
    if (s_running_app != NULL && s_running_app->on_destroy != NULL) {
        s_running_app->on_destroy();
    }
    s_running_app = NULL;

    /* 先切回大厅，再销毁游戏屏幕，避免释放活动屏幕。 */
    if (s_hub_screen != NULL) {
        lv_scr_load(s_hub_screen);
    }
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
    s_selected = 0;
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

    s_selected = 0;
    s_hub_state = XH_HUB_STATE_LIST;
    hub_create_ui();
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

    /* 先切回原有 AI 界面，再删除大厅屏幕。 */
    if (s_orig_screen != NULL) {
        lv_scr_load(s_orig_screen);
    }
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
                /* 上一个应用 */
                if (s_app_count == 0) return true;
                if (s_selected > 0) {
                    s_selected--;
                } else {
                    s_selected = s_app_count - 1;
                }
                hub_update_selection();
                return true;

            case eKey_VolDw_ShortPressed:
                /* 下一个应用 */
                if (s_app_count == 0) return true;
                s_selected++;
                if (s_selected >= (int8_t)s_app_count) {
                    s_selected = 0;
                }
                hub_update_selection();
                return true;

            case eKey_Wakeup_ShortPressed:
                /* 启动选中应用 */
                if (s_selected >= 0 && s_selected < (int8_t)s_app_count) {
                    const xh_app_entry_t *app = &s_apps[s_selected];
                    if (app->available) {
                        hub_launch_app(app);
                    } else {
                        log_info("[AppHub] app not available: %s\r\n", app->name);
                    }
                }
                return true;

            case eKey_Wakeup_LongPressed:
            case eKey_VolDw_LongPressed:
                /* 退出应用大厅 */
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
