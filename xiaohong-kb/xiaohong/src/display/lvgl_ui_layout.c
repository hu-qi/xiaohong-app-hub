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
#if defined(SUPPORT_BLE_KEYBOARD)
#include "ble_client.h"
#endif
#include "lvgl_ui_layout.h"

lv_obj_t* st_bar_up = NULL;   // 顶部状态栏，固定位置显示
lv_obj_t* main_card = NULL;   // 中间区域
lv_obj_t* st_bar_dm = NULL;   // 底部状态栏，固定位置显示

const lv_font_t* get_system_font(void)
{
    return &lv_font_SourceHanSerifCN_SemiBold_22_Default;
}
const int32_t get_system_font_height(void)
{
    return lv_font_get_line_height(get_system_font());
}

lv_style_t g_common_txt_style;
static void ui_common_text_style_config(void)
{
    lv_style_init(&g_common_txt_style);
    lv_style_set_radius(&g_common_txt_style, 0);
    lv_style_set_bg_opa(&g_common_txt_style, LV_OPA_COVER);
    lv_style_set_bg_color(&g_common_txt_style, lv_color_make(255, 255, 255));
    lv_style_set_border_width(&g_common_txt_style, 0);
    lv_style_set_border_color(&g_common_txt_style, lv_color_make(255, 255, 255));
    lv_style_set_pad_all(&g_common_txt_style, 0);
}
void ui_set_text_line_space(int8_t sp)
{
    lv_style_set_text_line_space(&g_common_txt_style, sp);
}

lv_obj_t *disp_create_common_title(char* name)
{
    lv_obj_t *title = lv_label_create(main_card);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_size(title, UI_MAIN_AREA_WIDTH, UI_COMMON_TITLE_HEIGHT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(title, name);
    lv_obj_set_style_text_font(title, get_system_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(title, lv_color_make(225, 225, 225), 0);

    lv_obj_set_style_pad_top(title, UI_COMMON_TITLE_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(title, UI_COMMON_TITLE_PADDING, LV_PART_MAIN);
    lv_obj_add_style(title, &g_common_txt_style, 0);

    return title;
}


static void ui_status_obj_config(void)
{
    st_bar_up = lv_label_create(lv_scr_act());
    st_bar_dm = lv_label_create(lv_scr_act());

    lv_obj_set_pos(st_bar_up, UI_STATUS_UP_POS_X, UI_STATUS_UP_POS_Y);
    lv_obj_set_size(st_bar_up, UI_STATUS_UP_WIDTH, UI_STATUS_UP_HEIGHT);
    lv_label_set_long_mode(st_bar_up, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(st_bar_up, " ");
    lv_obj_set_style_text_font(st_bar_up, &lv_font_FontAwesom5_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(st_bar_up, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(st_bar_up, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_style(st_bar_up, &g_common_txt_style, 0);

    lv_obj_set_pos(st_bar_dm, UI_STATUS_DM_POS_X, UI_STATUS_DM_POS_Y);
    lv_obj_set_size(st_bar_dm, UI_STATUS_DM_WIDTH, UI_STATUS_DM_HEIGHT);
    lv_label_set_long_mode(st_bar_dm, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(st_bar_dm, " ");
    lv_obj_set_style_text_font(st_bar_dm, &lv_font_FontAwesom5_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(st_bar_dm, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(st_bar_dm, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_add_style(st_bar_dm, &g_common_txt_style, 0);

  //lv_obj_set_style_bg_color(st_bar_up, lv_color_make(100, 200, 100), 0);
  //lv_obj_set_style_bg_color(st_bar_dm, lv_color_make(100, 200, 100), 0);
}

void disp_update_ui_status(void)
{
    static uint8_t g_sle_st  = 0xFF;
//  static uint8_t g_wifi_st = 0xFF;
//  static uint8_t g_bt_st   = 0xFF;
    static uint8_t g_blk = 0xFF;
    static uint8_t g_volt = 0xFF;
    static uint8_t g_kmod = 0xFF;

    uint8_t flush = 0;
    if (g_sle_st != (uint8_t) sle_client_get_ready_count()) {
        g_sle_st = (uint8_t) sle_client_get_ready_count();
        flush = 1;
    }
#if 0
    if (g_wifi_st != get_wifi_status()) {
        g_wifi_st = get_wifi_status();
        flush = 1;
    }
    if (g_wifi_st != get_ble_status()) {
        g_wifi_st = get_ble_status();
        flush = 1;
    }
#endif
    if (g_blk != get_blk_level()) {
        g_blk = get_blk_level();
        flush = 1;
    }
    if (g_kmod != get_keymode()) {
        g_kmod = get_keymode();
        flush = 1;
    }

    uint8_t level = get_voltage_level();

#if STATUS_USE_SYMBOL
    uint8_t volt = level;

    if (volt < 20) {
        volt = 0;
    } else if (volt < 40) {
        volt = 1;
    } else if (volt < 60) {
        volt = 2;
    } else if (volt < 80) {
        volt = 3;
    } else {
        volt = 4;
    }
    if (g_volt != volt) {
        flush = 1;
        g_volt = volt;
    }

    char* battery_symbol[5] = {
        LV_SYMBOL_BATTERY_EMPTY,
        LV_SYMBOL_BATTERY_1,
        LV_SYMBOL_BATTERY_2,
        LV_SYMBOL_BATTERY_3,
        LV_SYMBOL_BATTERY_FULL
    };

    if (flush && (st_bar_up != NULL)) {
        lv_label_set_text_fmt(st_bar_up, "%s %-3d     %s     %3d %s",
                ST_SYMBOL_BRIGHTNESS, g_blk,
                (g_kmod==eKeyMode_Custom)?"Custom":"Default",
                level, battery_symbol[g_volt]);
    }
#else
    g_volt = level;
    if (flush && (st_bar_up != NULL)) {
        lv_label_set_text_fmt(st_bar_up, "Blk:%d         Bat:%d",
            g_blk, g_volt);
    }
#endif

    if (st_bar_dm != NULL) {
#if defined(SUPPORT_SLE_KEYBOARD)
        if (get_protocol_type() == eProto_SLE) {
            /* SLE模式下，状态栏底部显示连接的Dongle的Mac地址 */
            if (sle_client_get_ready_count()) {
                sle_addr_t dongle_addr = {0};
                sle_get_server_addr(&dongle_addr);
                lv_label_set_text_fmt(st_bar_dm, "Dongle: %02X:%02X:%02X:%02X:%02X:%02X",
                    dongle_addr.addr[0], dongle_addr.addr[1],
                    dongle_addr.addr[2], dongle_addr.addr[3],
                    dongle_addr.addr[4], dongle_addr.addr[5]);
            } else {
                lv_label_set_text_fmt(st_bar_dm, "Dongle: DisConnected");
            }
        }
#endif
#if defined(SUPPORT_BLE_KEYBOARD)
        if (get_protocol_type() == eProto_BLE) {
            /* BLE模式下，状态栏底部显示连接的BLE主机(PC)的Mac地址 */
            if (ble_hid_host_connected()) {
                uint8_t ble_addr[6] = {0};
                ble_hid_get_host_addr(ble_addr, sizeof(ble_addr));
                lv_label_set_text_fmt(st_bar_dm, "BLE: %02X:%02X:%02X:%02X:%02X:%02X",
                        ble_addr[0], ble_addr[1], ble_addr[2],
                        ble_addr[3], ble_addr[4], ble_addr[5]);
            } else {
                lv_label_set_text_fmt(st_bar_dm, "BLE: DisConnected");
            }
        }
#endif
    }
}

void disp_create_ui(void)
{
    ui_common_text_style_config();
    ui_status_obj_config();

    main_card = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(main_card, UI_MAIN_AREA_POS_X, UI_MAIN_AREA_POS_Y);
    lv_obj_set_size(main_card, UI_MAIN_AREA_WIDTH,UI_MAIN_AREA_HEIGHT);
    lv_obj_set_style_radius(main_card, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(main_card, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(main_card, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_color(main_card, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(main_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(main_card, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(main_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(main_card, LV_OBJ_FLAG_SCROLLABLE);

    disp_create_main_page();
}
