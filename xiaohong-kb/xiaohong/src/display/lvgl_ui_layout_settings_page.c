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

#define SETTINGS_ITEMS_CNT   (4)
/* 设置页面的两个下拉选择列表句柄，供 confirm 读取选中值使用 */
static lv_obj_t *g_settings_proto_dd = NULL;   // Protocol Type 下拉
static lv_obj_t *g_settings_blk_dd = NULL;   // Brightness 下拉
static lv_obj_t *g_settings_rot_dd = NULL;   // Rotation 下拉
static lv_obj_t *g_settings_sleep_dd = NULL; // SleepTime 下拉

static uint8_t  g_settings_sel = 0;          // 当前高亮光标：0-ProtocType, 1-Brightness, 2-Rotation, 3-SleepTime
static uint8_t  g_settings_dd_open = 0;      // 当前高亮的dropdown是否已展开：0-未展开, 1-已展开
static lv_obj_t *g_settings_list = NULL;     // 设置菜单列表（菜单行超出可视区时用于滚动）

/*
static void setting_btn_event(lv_event_t* evt)    // 设置按钮
{
    if (lv_event_get_code(evt) != LV_EVENT_CLICKED) {
        return;
    }
}
*/

// 显示设置页面
void disp_create_settings_page(void)
{
    /* 显示新页面之前，先清空 main_card，避免残留上一个页面的内容 */
    lv_obj_clean(main_card);

//  lv_obj_t *title = disp_create_common_title("Settings");
    lv_obj_t *title = disp_create_common_title("系统设置");

#if 1
    char* settings_list[SETTINGS_ITEMS_CNT] =
    {
        "ProtoType",   // "协议类型",     //
        "Brightness",  // "显示亮度",     //
        "Rotation",    // "旋转角度",     //
        "SleepTime",   // "休眠时间",     //
    };
#else
    char* settings_list[] =
    {
        "协议类型",     // "ProtoType",   // 
        "显示亮度",     // "Brightness",  //
        "旋转角度",     // "Rotation",    //
        "休眠时间",     // "SleepTime",   //
    };
#endif

    /* 协议类型 [SLE, BLE]，与 get/set_protocol_type() 对应 */
    #if defined(SUPPORT_BLE_KEYBOARD)
    const char *protocol_type_options = "SLE\nBLE";
    #else
    const char *protocol_type_options = "SLE";
    #endif

    /* 亮度档位 [1, 10, 20 ... 90, 100]，与 get/set_blk_level() 对应 */
    const char *brightness_options = "1\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100";
    /* 旋转角度 0°/90°/180°/270°，与 get/set_rotation() 的 0~3 对应 */
    const char *rotation_options = "0°\n90°\n180°\n270°";
    /* 休眠时间设置 */
    const char *sleep_time = "never\n10s\n20s\n30s\n60s";

    /* 在 title 下方创建列表组件 */
    lv_obj_t *list = lv_list_create(main_card);
    lv_obj_set_pos(list, 0, UI_COMMON_TITLE_HEIGHT);
    lv_obj_set_size(list, UI_MAIN_AREA_WIDTH, UI_MAIN_AREA_HEIGHT - UI_COMMON_TITLE_HEIGHT);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    /* 菜单行增加到4行后，总高度可能超出可视区：保留列表纵向滚动能力，
       高亮移到最低一行时由 disp_settings_cursor_move() 将列表滚动到该行完整可见 */
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    g_settings_list = list;
    lv_obj_set_style_pad_top(list, 2, LV_PART_MAIN);

    /* 每个 item：左边是设置项名称，右边是下拉选择列表 */
    for (uint8_t i = 0; i < SETTINGS_ITEMS_CNT; i++) {
        lv_obj_t *item = lv_list_add_button(list, NULL, settings_list[i]);
        lv_obj_set_style_pad_top(item, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(item, 4, LV_PART_MAIN);
        /* item 左右边界收窄 50%，使 item 内可用宽度增大 */
        lv_obj_set_style_pad_left(item, lv_obj_get_style_pad_left(item, LV_PART_MAIN) / 2, LV_PART_MAIN);
        lv_obj_set_style_pad_right(item, lv_obj_get_style_pad_right(item, LV_PART_MAIN) / 2, LV_PART_MAIN);
        /* item 整体向下移动 2 像素（flex 布局上边距） */
        lv_obj_set_style_margin_top(item, 2, LV_PART_MAIN);
        /* item 内文字/下拉在垂直方向居中（避免因 item 高度变化导致偏上） */
        lv_obj_set_style_flex_cross_place(item, LV_FLEX_ALIGN_CENTER, LV_PART_MAIN);

        lv_obj_t *dd = lv_dropdown_create(item);
        lv_obj_set_style_text_font(dd, get_system_font(), LV_PART_MAIN);
        lv_obj_set_width(dd, 80);
        /* 调整 dd 组件高度（基于系统字体行高） */
        lv_obj_set_height(dd, get_system_font_height()*3/4);

        if (i == 0) {
            /* 协议类型：选项 SLE, BLE，默认选中 get_protocol_type() 对应的 index */
            g_settings_proto_dd = dd;
            lv_dropdown_set_options(dd, protocol_type_options);
            #if defined(SUPPORT_BLE_KEYBOARD)
            lv_dropdown_set_selected(dd, get_protocol_type() % 2);
            #else
            lv_dropdown_set_selected(dd, 0);  // select 0:SLE
            #endif
        } else if (i == 1) {
            /* Brightness：选项 [1, 10, 20 ... 100]，默认选中 get_blk_level() 对应的档位 */
            g_settings_blk_dd = dd;
            lv_dropdown_set_options(dd, brightness_options);
            uint8_t blk = get_blk_level();
            uint8_t idx = 0;
            if (blk >= 100) {
                idx = 10;
            } else if (blk > 0) {
                idx = (blk + 5) / 10;   // 四舍五入到最近的 10% 档位
                if (idx > 10) {
                    idx = 10;
                }
            }
            lv_dropdown_set_selected(dd, idx);
        } else if (i == 2) {
            /* Rotation：选项 0°/90°/180°/270°，默认选中 get_rotation() 对应的 index */
            g_settings_rot_dd = dd;
            lv_dropdown_set_options(dd, rotation_options);
            lv_dropdown_set_selected(dd, get_rotation() % 4);
        } else if (i == 3) {
            /* SleepTime：选项 never/10s/20s/30s/60s，默认选中 get_sleep_time() 对应的 index */
            g_settings_sleep_dd = dd;
            lv_dropdown_set_options(dd, sleep_time);
            uint8_t sleep_val = get_sleep_time();
            uint8_t sidx = 0;
            if (sleep_val >= 60) {
                sidx = 4;
            } else if (sleep_val >= 30) {
                sidx = 3;
            } else if (sleep_val >= 20) {
                sidx = 2;
            } else if (sleep_val >= 10) {
                sidx = 1;
            } else {
                sidx = 0;   // never
            }
            lv_dropdown_set_selected(dd, sidx);
        }

        /* 下拉选择列表右对齐，配合按钮内 flex 布局的左侧名称 */
        lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    /* 默认高亮 Brightness 项 */
    disp_settings_refresh_highlight();
}

/* 刷新设置页面当前高亮项（Brightness/Rotation/SleepTime 三选一） */
void disp_settings_refresh_highlight(void)
{
    lv_obj_t *btns[SETTINGS_ITEMS_CNT] = { NULL };
    lv_obj_t *btn_hl = NULL;
    lv_obj_t *child = NULL;

    if (g_settings_proto_dd == NULL || g_settings_blk_dd == NULL ||
        g_settings_rot_dd == NULL || g_settings_sleep_dd == NULL) {
        return;
    }

    /* 取四个下拉的父容器（列表按钮），用于高亮背景 */
    btns[0] = lv_obj_get_parent(g_settings_proto_dd);
    btns[1] = lv_obj_get_parent(g_settings_blk_dd);
    btns[2] = lv_obj_get_parent(g_settings_rot_dd);
    btns[3] = lv_obj_get_parent(g_settings_sleep_dd);
    for (uint8_t i=0; i<SETTINGS_ITEMS_CNT; i++) {
        if (btns[i] == NULL) {
            return;
        }
    }

    btn_hl = btns[g_settings_sel % SETTINGS_ITEMS_CNT];

    for (uint8_t i = 0; i < SETTINGS_ITEMS_CNT; i++) {
        if (btns[i] == btn_hl) {
            /* 高亮项：深色背景 + 白字 */
            lv_obj_set_style_bg_color(btns[i], lv_color_make(40, 90, 200), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btns[i], LV_OPA_COVER, LV_PART_MAIN);
            child = lv_obj_get_child(btns[i], 0);
            if (child != NULL) {
                lv_obj_set_style_text_color(child, lv_color_make(255, 255, 255), LV_PART_MAIN);
            }
        } else {
            /* 非高亮项：恢复默认灰底 + 黑字 */
            lv_obj_set_style_bg_color(btns[i], lv_color_make(0xEF, 0xEF, 0xEF), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btns[i], LV_OPA_COVER, LV_PART_MAIN);
            child = lv_obj_get_child(btns[i], 0);
            if (child != NULL) {
                lv_obj_set_style_text_color(child, lv_color_make(0, 0, 0), LV_PART_MAIN);
            }
        }
    }
}

/* 移动设置页面高亮光标：dir > 0 下移，dir < 0 上移（四个设置项间循环） */
void disp_settings_cursor_move(int8_t dir)
{
    lv_obj_t *dd = NULL;
    uint32_t opt_cnt = 0;
    uint32_t sel = 0;

    if (dir == 0) {
        return;
    }

    if (g_settings_dd_open) {
        /* 下拉已展开：Up/Down 改变当前高亮下拉的选中值 */
        if (g_settings_sel == 0) {
            dd = g_settings_proto_dd;
        } else if (g_settings_sel == 1) {
            dd = g_settings_blk_dd;
        } else if (g_settings_sel == 2) {
            dd = g_settings_rot_dd;
        } else {
            dd = g_settings_sleep_dd;
        }
        if (dd == NULL) {
            return;
        }
        opt_cnt = lv_dropdown_get_option_count(dd);
        sel = lv_dropdown_get_selected(dd);
        if (dir > 0) {
            sel++;
            if (sel >= opt_cnt) {
                sel = opt_cnt - 1;
            }
        } else {
            if (sel > 0) {
                sel--;
            }
        }
        lv_dropdown_set_selected(dd, sel);
        return;
    }

    /* 未展开：在 ProtocType(0) / Brightness(1) / Rotation(2) / SleepTime(3) 间循环移动高亮 */
    g_settings_sel = (uint8_t)((g_settings_sel + 1) % SETTINGS_ITEMS_CNT);
    disp_settings_refresh_highlight();

    /* 高亮项若超出列表可视区（如最低一行被视口裁切），滚动列表使其完整可见 */
    if (g_settings_list != NULL) {
        lv_obj_t *btn_hl = NULL;
        lv_area_t list_area;
        lv_area_t item_area;
        if (g_settings_sel == 0) {
            btn_hl = lv_obj_get_parent(g_settings_proto_dd);
        } else if (g_settings_sel == 1) {
            btn_hl = lv_obj_get_parent(g_settings_blk_dd);
        } else if (g_settings_sel == 2) {
            btn_hl = lv_obj_get_parent(g_settings_rot_dd);
        } else {
            btn_hl = lv_obj_get_parent(g_settings_sleep_dd);
        }
        if (btn_hl != NULL) {
            lv_obj_get_coords(g_settings_list, &list_area);
            lv_obj_get_coords(btn_hl, &item_area);
            if (item_area.y2 > list_area.y2) {
                /* 行底部超出可视区：列表内容向上滚动(dy<0)，露出被遮挡的完整一行 */
                lv_obj_scroll_by(g_settings_list, 0, list_area.y2 - item_area.y2, LV_ANIM_OFF);
            } else if (item_area.y1 < list_area.y1) {
                /* 行顶部超出可视区（此前内容已向上滚动过）：列表内容向下滚动(dy>0)，回到可视区 */
                lv_obj_scroll_by(g_settings_list, 0, list_area.y1 - item_area.y1, LV_ANIM_OFF);
            }
        }
    }
}

/* 确认：未展开时展开下拉；已展开时读取选中值写入设置并关闭下拉 */
void disp_settings_confirm(void)
{
    lv_obj_t *dd = NULL;

    /* 第一步：展开当前高亮项的下拉列表 */
    if (!g_settings_dd_open) {
        if (g_settings_sel == 0) {
            dd = g_settings_proto_dd;
        } else if (g_settings_sel == 1) {
            dd = g_settings_blk_dd;
        } else if (g_settings_sel == 2) {
            dd = g_settings_rot_dd;
        } else {
            dd = g_settings_sleep_dd;
        }
        if (dd == NULL) {
            return;
        }
        lv_dropdown_open(dd);
        g_settings_dd_open = 1;
        log_info("[UI] disp_settings_confirm: open dropdown sel[%d]\r\n", g_settings_sel);
        return;
    }

    /* 第二步：读取当前下拉选中值并写入设置 */
    if (g_settings_sel == 0) {
        /* 协议类型：读取档位 index，映射回协议类型值后调用 set_protocol_type() */
        dd = g_settings_proto_dd;
        if (dd == NULL) {
            return;
        }
        uint32_t idx = lv_dropdown_get_selected(dd);
        log_info("[UI] disp_settings_confirm: set_protocol_type(%u)\r\n", (unsigned)idx);
        set_protocol_type((uint8_t)idx);
    } else if (g_settings_sel == 1) {
        /* Brightness：读取档位 index，映射回亮度值后调用 set_blk_level() */
        dd = g_settings_blk_dd;
        if (dd == NULL) {
            return;
        }
        uint32_t idx = lv_dropdown_get_selected(dd);
        uint8_t blk = (idx == 0) ? 1 : (uint8_t)(idx * 10);   // 0->1, 1->10, 2->20 ... 10->100
        log_info("[UI] disp_settings_confirm: set_blk_level(%d)\r\n", blk);
        set_blk_level(blk);
    } else if (g_settings_sel == 2) {
        /* Rotation：读取角度 index（0~3），调用 set_rotation() */
        dd = g_settings_rot_dd;
        if (dd == NULL) {
            return;
        }
        uint32_t idx = lv_dropdown_get_selected(dd);
        log_info("[UI] disp_settings_confirm: set_rotation(%u)\r\n", (unsigned)idx);
        set_rotation((uint8_t)idx);
    } else {
        /* SleepTime：index(0~4) -> never/10/20/30/60，调用 set_sleep_time() */
        static const uint8_t sleep_vals[5] = { 0, 10, 20, 30, 60 };
        dd = g_settings_sleep_dd;
        if (dd == NULL) {
            return;
        }
        uint32_t idx = lv_dropdown_get_selected(dd);
        uint8_t sleep = (idx < 5) ? sleep_vals[idx] : 0;
        log_info("[UI] disp_settings_confirm: set_sleep_time(%d)\r\n", sleep);
        set_sleep_time(sleep);
    }

    /* 关闭下拉，回到高亮移动状态 */
    if (dd != NULL) {
        lv_dropdown_close(dd);
    }
    g_settings_dd_open = 0;

    /* 落盘后刷新状态栏显示 */
    disp_update_ui_status();
}
