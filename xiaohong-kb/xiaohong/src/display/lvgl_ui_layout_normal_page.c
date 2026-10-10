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


// 显示正常模式的主页面
void disp_create_normal_page(void)
{
    /* 显示新页面之前，先清空 main_card，避免残留上一个页面的内容 */
    lv_obj_clean(main_card);

//  lv_obj_t *title = disp_create_common_title("Normal Mode");
    if (get_protocol_type() == eProto_BLE) {
        lv_obj_t *title = disp_create_common_title("正常模式(BLE)");
    } else {
        lv_obj_t *title = disp_create_common_title("正常模式(SLE)");
    }

    lv_obj_t *text = lv_label_create(main_card);
    lv_obj_set_pos(text, 0, UI_COMMON_TITLE_HEIGHT);
    lv_obj_set_size(text, UI_MAIN_AREA_WIDTH, UI_MAIN_AREA_HEIGHT - UI_COMMON_TITLE_HEIGHT);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
//  lv_label_set_text_fmt(text, "sync/show\nAtomcode Status");
    if (get_protocol_type() == eProto_BLE) {
        lv_label_set_text_fmt(text, "AtomCode\n工作状态:\n%s", get_atomcode_status_str());
    } else {
        lv_label_set_text_fmt(text, "AtomCode\n工作状态:\n%s", get_atomcode_status_str());
    }
    lv_obj_set_style_text_font(text, get_system_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(text, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    //lv_obj_set_style_bg_color(text, lv_color_make(235, 235, 235), 0);

    lv_obj_set_style_pad_top(text, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(text, 5, LV_PART_MAIN);

    lv_obj_add_style(text, &g_common_txt_style, 0);
}
