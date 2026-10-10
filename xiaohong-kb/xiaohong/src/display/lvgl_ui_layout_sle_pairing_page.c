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

/* 配对列表（可配对的Dongle列表）相关状态 */
#define PAIRING_LIST_MAX_ITEMS    (10)   // 与 sle_client.c 的 SLE_SCAN_RESULT_MAX_COUNT 保持一致
static lv_obj_t *g_pairing_items[PAIRING_LIST_MAX_ITEMS] = {NULL};
static uint8_t  g_pairing_count = 0;
static uint8_t  g_pairing_sel   = 0;    // 当前高亮光标所在的 item index

static void disp_pairing_refresh_highlight(void);

// 显示扫描结果、设备配对页面
void disp_create_sle_scan_pairing_page(void)
{
    uint8_t count = 0;
    sle_addr_t *scan_list = NULL;
    lv_obj_t *item = NULL;
    lv_color_t parent_bg;
    lv_coord_t item_pad;

    /* 显示新页面之前，先清空 main_card，避免残留上一个页面的内容 */
    lv_obj_clean(main_card);

    /* 每次重建配对页面时，重置配对列表状态 */
    memset(g_pairing_items, 0, sizeof(g_pairing_items));
    g_pairing_count = 0;
    g_pairing_sel   = 0;

//  lv_obj_t *title = disp_create_common_title("Dongle List:");
    lv_obj_t *title = disp_create_common_title("Dongle 列表:");

    // 创建列表，选项数量通过 get_scan_result 的 count 参数获取
    lv_obj_t *list = lv_list_create(main_card);
    lv_obj_set_pos(list, 0, UI_COMMON_TITLE_HEIGHT);
    lv_obj_set_size(list, UI_MAIN_AREA_WIDTH, UI_MAIN_AREA_HEIGHT - UI_COMMON_TITLE_HEIGHT);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    /* item 背景与父容器 main_card 的背景颜色一致 */
    parent_bg = lv_obj_get_style_bg_color(main_card, LV_PART_MAIN);
    /* item 高度在内容基础上调大 20%（上下各加 10% 行高内边距），保证行间距清晰 */
    item_pad = lv_font_get_line_height(get_system_font()) / 10;

    scan_list = get_scan_result(&count);
    log_info("[UI] %s: scan count[%d]\r\n", __func__, count);
    if (sle_client_get_ready_count() > 0) {
        item = lv_list_add_text(list, "Already Connected");  // "已配对和连接"
        lv_obj_set_style_pad_top(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, parent_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        return;
    } else if (scan_list == NULL || count == 0) {
        item = lv_list_add_text(list, "No Scan Result");  // "无扫描结果"
        lv_obj_set_style_pad_top(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, parent_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        return;
    }

    /* 每一行显示一个 Dongle 的 MAC 地址 addr[0~5]，并保存到配对列表，供高亮光标移动 */
    g_pairing_count = count;
    for (uint8_t i = 0; i < count; i++) {
        item = lv_list_add_text(list, "");
        lv_label_set_text_fmt(item, "%02X:%02X:%02X:%02X:%02X:%02X",
            scan_list[i].addr[0], scan_list[i].addr[1], scan_list[i].addr[2],
            scan_list[i].addr[3], scan_list[i].addr[4], scan_list[i].addr[5]);
        /* item 高度调大 40%（上下各 20% 行高内边距），背景与父容器一致 */
        lv_obj_set_style_pad_top(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, item_pad, LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, parent_bg, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        g_pairing_items[i] = item;
    }

    /* 默认高亮 index 0 的 item */
    disp_sle_pairing_refresh_highlight();
}

/* 根据 g_pairing_sel 刷新配对列表中所有 item 的高亮样式 */
void disp_sle_pairing_refresh_highlight(void)
{
    lv_color_t parent_bg = lv_obj_get_style_bg_color(main_card, LV_PART_MAIN);

    for (uint8_t i = 0; i < g_pairing_count; i++) {
        if (g_pairing_items[i] == NULL) {
            continue;
        }
        if (i == g_pairing_sel) {
            /* 高亮项：深色背景 + 白字（颜色与其他页面保持一致） */
            lv_obj_set_style_bg_color(g_pairing_items[i], lv_color_make(40, 90, 200), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(g_pairing_items[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(g_pairing_items[i], lv_color_make(255, 255, 255), 0);
        } else {
            /* 非高亮项：恢复为父容器背景 + 黑字 */
            lv_obj_set_style_bg_color(g_pairing_items[i], parent_bg, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(g_pairing_items[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(g_pairing_items[i], lv_color_make(0, 0, 0), 0);
        }
    }
}

/* 移动配对列表高亮光标：dir > 0 下移，dir < 0 上移 */
void disp_sle_scan_pairing_cursor_move(int8_t dir)
{
    if (g_pairing_count == 0) {
        return;
    }

    int16_t sel = (int16_t)g_pairing_sel + dir;
    if (sel < 0) {
        sel = 0;
    }
    if (sel >= (int16_t)g_pairing_count) {
        sel = (int16_t)g_pairing_count - 1;
    }

    if ((uint8_t)sel != g_pairing_sel) {
        g_pairing_sel = (uint8_t)sel;
        disp_sle_pairing_refresh_highlight();
    }
}

/* 确认选中当前高亮项，并调用 accept_scan_result(idx) 设置对应的 Dongle */
void disp_sle_scan_pairing_confirm(void)
{
    if (g_pairing_count == 0) {
        return;
    }

    log_info("[UI] disp_sle_scan_pairing_confirm: select idx[%d]\r\n", g_pairing_sel);
    accept_scan_result(g_pairing_sel);
}
