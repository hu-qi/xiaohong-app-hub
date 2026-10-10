/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <string.h>
#include "log.h"
#include "settings.h"
#include "sle_client.h"
#include "lvgl_ui_layout.h"

extern lv_obj_t* main_card;
extern lv_style_t g_common_txt_style;

/* 主页面两个功能按钮的句柄，供高亮刷新/光标移动使用 */
static lv_obj_t *g_setting_btn = NULL;
static lv_obj_t *g_keyremap_btn = NULL;
static lv_obj_t *g_wifi_btn = NULL;
static lv_obj_t *g_bt_btn = NULL;

static uint8_t  g_hili_select = 0;    // 当前高亮光标：0-setting, 1-wifi


// 显示本地模式的入口页面
void disp_create_local_page(void)
{
    /* 显示新页面之前，先清空 main_card，避免残留上一个页面的内容 */
    lv_obj_clean(main_card);

//  lv_obj_t *title = disp_create_common_title("Local Mode");
    lv_obj_t *title = disp_create_common_title("本地模式");

    char* btn_list[] = { "系统设置", "按键映射", "WiFi", "BT" };
    uint8_t btn_cnt = sizeof(btn_list) / sizeof(btn_list[0]);

    /* 田字格(2x2)布局：四个按钮等宽等高、间距一致，整体在 main_card 内居中 */
    uint8_t btn_width  = 110;
    uint8_t btn_height = 40;
    uint8_t btn_gap    = 10;
    int16_t grid_w = (int16_t)(btn_width * 2 + btn_gap);
    int16_t grid_h = (int16_t)(btn_height * 2 + btn_gap);
    int16_t grid_x = ((int16_t)UI_MAIN_AREA_WIDTH - grid_w) / 2;
    int16_t grid_y = ((int16_t)UI_MAIN_AREA_HEIGHT - grid_h) / 2 + btn_gap;

    lv_obj_t *btn_handles[] = { g_setting_btn, g_keyremap_btn, g_wifi_btn, g_bt_btn };

    for (uint8_t i = 0; i < btn_cnt; i++) {
        int16_t col = (int16_t)(i & 1);
        int16_t row = (int16_t)(i >> 1);
        int16_t bx = grid_x + col * (btn_width + btn_gap);
        int16_t by = grid_y + row * (btn_height + btn_gap);

        lv_obj_t *btn = lv_button_create(main_card);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_size(btn, btn_width, btn_height);
        lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_make(0xEF, 0xEF, 0xEF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);

        lv_label_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, btn_list[i]);
        lv_obj_set_style_text_font(btn_label, get_system_font(), LV_PART_MAIN);
        lv_obj_set_style_text_color(btn_label, lv_color_make(0, 0, 0), LV_PART_MAIN);
        lv_obj_center(btn_label);

        /* 保存按钮句柄，供高亮刷新/光标移动使用 */
        btn_handles[i] = btn;
    }

    g_setting_btn  = btn_handles[0];
    g_keyremap_btn = btn_handles[1];
    g_wifi_btn     = btn_handles[2];
    g_bt_btn       = btn_handles[3];

    /* 默认高亮 setting 按钮 */
    g_hili_select = 0;
    disp_local_page_refresh_highlight();
}

/* 移动主页面上的按钮高亮光标：dir > 0 下移，dir < 0 上移 */
void disp_local_page_cursor_move(int8_t dir)
{
    log_info("[UI] %s: dir[%d]\r\n", __func__, dir);

    if (sle_client_get_ready_count() && (eWorkMode_Normal == get_workmode())) {
        /* 模式0：不提供高亮选项的切换 */
        return;
    }

    /* 模式1：在 Setting(0) / SLE(1) / WiFi(2) / BT(3) 四个按钮间循环移动高亮 */
    if (dir > 0) {
        g_hili_select = (g_hili_select + 1) & 3;
    } else if (dir < 0) {
        g_hili_select = (g_hili_select + 3) & 3;
    } else {
        return;
    }

    disp_local_page_refresh_highlight();
}

/* 确认选中当前高亮项，并调用接口打开对应页面 */
void disp_local_page_confirm(void)
{
    log_info("[UI] %s: g_hili_select[%d]\r\n", __func__, g_hili_select);
    if (g_hili_select == 0) {
        post_msg_event(g_disp_event_qid, eDisp_Show_Setting_Page);
    } else if (g_hili_select == 1) {
        if(enable_key_remap()) {
            post_msg_event(g_disp_event_qid, eDisp_Show_KeyRemap_Page);
        }
    } else if (g_hili_select == 2) {
//      post_msg_event(g_disp_event_qid, eDisp_Show_WiFi_Page);
    } else if (g_hili_select == 3) {
//      post_msg_event(g_disp_event_qid, eDisp_Show_BT_Page);
    }
}

/* 刷新本地模式主页面默认的高亮按钮 */
void disp_local_page_refresh_highlight(void)
{
    lv_obj_t *btns[4] = { g_setting_btn, g_keyremap_btn, g_wifi_btn, g_bt_btn };
    lv_obj_t *btn_hl = NULL;

    if (g_setting_btn == NULL || g_keyremap_btn == NULL ||
        g_wifi_btn == NULL || g_bt_btn == NULL) {
        return;
    }

    if (eWorkMode_Normal == get_workmode()) {
        /* Normal 模式：不给按钮选项加高亮，按钮都恢复默认样式 */
        btn_hl = NULL;
    } else {
        /* 非 Normal 模式：可切换高亮选项，按 g_hili_select 高亮对应按钮 */
        btn_hl = btns[g_hili_select & 3];
    }

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t *label = lv_obj_get_child(btns[i], 0);
        if (btns[i] == btn_hl) {
            /* 高亮按钮：深色背景 + 白字 */
            lv_obj_set_style_bg_color(btns[i], lv_color_make(40, 90, 200), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btns[i], LV_OPA_COVER, LV_PART_MAIN);
            if (label != NULL) {
                lv_obj_set_style_text_color(label, lv_color_make(255, 255, 255), LV_PART_MAIN);
            }
        } else {
            /* 非高亮按钮：恢复默认灰底 + 黑字 */
            lv_obj_set_style_bg_color(btns[i], lv_color_make(0xEF, 0xEF, 0xEF), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btns[i], LV_OPA_COVER, LV_PART_MAIN);
            if (label != NULL) {
                lv_obj_set_style_text_color(label, lv_color_make(0, 0, 0), LV_PART_MAIN);
            }
        }
    }
}
