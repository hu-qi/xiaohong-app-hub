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

/* ===== LVGL 对象 ===== */
static lv_obj_t *s_hub_screen = NULL;    /* 应用大厅独立屏幕 */
static lv_obj_t *s_hub_title  = NULL;    /* 标题 "应用大厅" */
static lv_obj_t *s_hub_list   = NULL;    /* 列表容器 */
static lv_obj_t *s_hub_items[8] = {NULL}; /* 列表项（最多 8 个应用） */
static lv_obj_t *s_hub_hint   = NULL;    /* 底部提示 */

static lv_obj_t *s_app_container = NULL;  /* 运行中应用的容器 */
static lv_obj_t *s_orig_screen = NULL;   /* 保存原始屏幕，退出大厅时恢复 */

/* 外部字体引用（来自 lvgl_ui_layout.h） */
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_22_Default);

/* ===== 内部函数 ===== */

static void hub_create_ui(void)
{
    if (s_hub_screen != NULL) {
        return;
    }

    s_orig_screen = lv_scr_act();  /* 保存当前屏幕 */
    s_hub_screen = lv_obj_create(NULL);   /* 创建独立屏幕 */
    lv_obj_set_size(s_hub_screen, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(s_hub_screen, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_hub_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_hub_screen, 0, 0);
    lv_obj_set_style_border_width(s_hub_screen, 0, 0);
    lv_obj_clear_flag(s_hub_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题栏 */
    s_hub_title = lv_label_create(s_hub_screen);
    lv_label_set_text(s_hub_title, "应用大厅");
    lv_obj_set_pos(s_hub_title, 0, 2);
    lv_obj_set_width(s_hub_title, DISP_WIDTH);
    lv_obj_set_style_text_align(s_hub_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hub_title, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(s_hub_title, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);

    /* 列表容器 */
    s_hub_list = lv_obj_create(s_hub_screen);
    lv_obj_set_pos(s_hub_list, 4, 26);
    lv_obj_set_size(s_hub_list, DISP_WIDTH - 8, DISP_HEIGHT - 26 - 16);
    lv_obj_set_style_bg_color(s_hub_list, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_hub_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_hub_list, 0, 0);
    lv_obj_set_style_pad_all(s_hub_list, 2, 0);
    lv_obj_set_flex_flow(s_hub_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_hub_list, LV_OBJ_FLAG_SCROLLABLE);

    /* 列表项 */
    for (uint8_t i = 0; i < s_app_count && i < 8; i++) {
        s_hub_items[i] = lv_label_create(s_hub_list);
        lv_obj_set_width(s_hub_items[i], DISP_WIDTH - 16);
        lv_obj_set_style_text_font(s_hub_items[i], &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(s_hub_items[i], 2, 0);

        char buf[48];
        if (s_apps[i].available) {
            lv_snprintf(buf, sizeof(buf), "  %s %s", s_apps[i].icon, s_apps[i].name);
        } else {
            lv_snprintf(buf, sizeof(buf), "  %s %s [敬请期待]", s_apps[i].icon, s_apps[i].name);
        }
        lv_label_set_text(s_hub_items[i], buf);
    }

    /* 底部提示 */
    s_hub_hint = lv_label_create(s_hub_screen);
    lv_label_set_text(s_hub_hint, "Vol+/-选择 Wakeup确认");
    lv_obj_set_pos(s_hub_hint, 0, DISP_HEIGHT - 14);
    lv_obj_set_width(s_hub_hint, DISP_WIDTH);
    lv_obj_set_style_text_align(s_hub_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_hub_hint, lv_color_make(0x80, 0x80, 0x80), 0);

    hub_update_selection();
    lv_scr_load(s_hub_screen);
}

static void hub_destroy_ui(void)
{
    if (s_hub_screen == NULL) {
        return;
    }
    lv_obj_del(s_hub_screen);
    s_hub_screen = NULL;
    s_hub_title  = NULL;
    s_hub_list   = NULL;
    s_hub_hint   = NULL;
    for (uint8_t i = 0; i < 8; i++) {
        s_hub_items[i] = NULL;
    }
}

static void hub_update_selection(void)
{
    for (uint8_t i = 0; i < s_app_count && i < 8; i++) {
        if (s_hub_items[i] == NULL) {
            continue;
        }
        if ((int8_t)i == s_selected) {
            /* 选中项：高亮反白 */
            lv_obj_set_style_bg_color(s_hub_items[i], lv_color_make(0xFF, 0x82, 0x14), 0);
            lv_obj_set_style_bg_opa(s_hub_items[i], LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(s_hub_items[i], lv_color_make(0x00, 0x00, 0x00), 0);
        } else {
            lv_obj_set_style_bg_opa(s_hub_items[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_text_color(s_hub_items[i], lv_color_make(0xFF, 0xFF, 0xFF), 0);
        }
    }
}

static void hub_launch_app(const xh_app_entry_t *app)
{
    if (app == NULL || !app->available) {
        return;
    }

    /* 创建应用运行屏幕 */
    s_app_container = lv_obj_create(NULL);
    lv_obj_set_size(s_app_container, DISP_WIDTH, DISP_HEIGHT);
    lv_obj_set_style_bg_color(s_app_container, lv_color_make(0x00, 0x00, 0x00), 0);
    lv_obj_set_style_bg_opa(s_app_container, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_app_container, 0, 0);
    lv_obj_set_style_border_width(s_app_container, 0, 0);
    lv_obj_clear_flag(s_app_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_scr_load(s_app_container);

    if (app->on_create != NULL) {
        if (app->on_create(s_app_container)) {
            s_running_app = app;
            s_hub_state = XH_HUB_STATE_RUNNING;
            log_info("[AppHub] launched app: %s (id=%d)\r\n", app->name, app->id);
        } else {
            log_error("[AppHub] app on_create failed: %s\r\n", app->name);
            lv_obj_del(s_app_container);
            s_app_container = NULL;
        }
    }
}

static void hub_exit_app(void)
{
    if (s_running_app != NULL && s_running_app->on_destroy != NULL) {
        s_running_app->on_destroy();
        log_info("[AppHub] exited app: %s\r\n", s_running_app->name);
    }
    s_running_app = NULL;

    if (s_app_container != NULL) {
        lv_obj_del(s_app_container);
        s_app_container = NULL;
    }

    s_hub_state = XH_HUB_STATE_LIST;
    lv_scr_load(s_hub_screen);   /* 回到应用列表 */
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

    hub_destroy_ui();
    s_hub_state = XH_HUB_STATE_HIDDEN;

    /* 恢复原始屏幕（AI 对话界面） */
    if (s_orig_screen != NULL) {
        lv_scr_load(s_orig_screen);
        s_orig_screen = NULL;
    }
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
                /* 向上选择 */
                if (s_selected > 0) {
                    s_selected--;
                } else {
                    s_selected = s_app_count - 1;
                }
                hub_update_selection();
                return true;

            case eKey_VolDw_ShortPressed:
                /* 向下选择 */
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
