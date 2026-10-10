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

#if defined(SUPPORT_NETWORK)
#include "net_wifi_config.h"
#endif

extern lv_obj_t* main_card;
extern lv_style_t g_common_txt_style;
static lv_obj_t *s_hint_text = NULL;

// 显示按键重映射配置页面
void disp_create_key_remap_page(void)
{
    /* 显示新页面之前，先清空 main_card，避免残留上一个页面的内容 */
    lv_obj_clean(main_card);

//  lv_obj_t *title = disp_create_common_title("Key Remap");
    lv_obj_t *title = disp_create_common_title("按键重映射");

    lv_obj_t *text = lv_label_create(main_card);
    s_hint_text = text;

    lv_obj_set_pos(text, 0, UI_COMMON_TITLE_HEIGHT+8);
    lv_obj_set_size(text, UI_MAIN_AREA_WIDTH, UI_MAIN_AREA_HEIGHT - UI_COMMON_TITLE_HEIGHT-8);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    /* 保存文本对象，并设置为可滚动显示，供光标键上下滚动 */
    lv_obj_add_flag(text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(text, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(text, get_system_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(text, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    //lv_obj_set_style_bg_color(text, lv_color_make(235, 235, 235), 0);
    lv_obj_add_style(text, &g_common_txt_style, 0);
    ui_set_text_line_space(-8);
    lv_label_set_text_fmt(text,
                          "1.手机或PC连接热点:\n"      \
                          "   %s\n"                   \
                          "2.打开浏览器访问:\n"        \
                          "   http://192.168.1.1\n"   \
                          "3.在页面上配置自定义键码\n"  \
                          "4.在页面上点击保存配置\n",
                          get_hotspot_name());
}

/* 设置页面上下滚动：dir > 0 下移，dir < 0 上移 */
void disp_key_remap_cursor_move(int8_t dir)
{
    if (s_hint_text == NULL) {
        return;
    }
    /* 滚动边界处理：已显示第一行(顶部)时不再上滚，已显示最后一行(底部)时不再下滚 */
    if (dir > 0 && lv_obj_get_scroll_y(s_hint_text) <= 0) {
        return;
    }
    if (dir < 0 && lv_obj_get_scroll_bottom(s_hint_text) <= 0) {
        return;
    }
    /* 每次滚动一行文本的高度 */
    lv_obj_scroll_by(s_hint_text, 0, dir * get_system_font_height(), LV_ANIM_OFF);
}