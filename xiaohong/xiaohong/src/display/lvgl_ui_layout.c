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
#include "lvgl_ui_layout.h"
#include "log.h"

#if defined(SUPPORT_LFS)
#include "lv_font_lfs_binfont.h"
#endif

static lv_style_t g_common_txt_style;
void ui_common_text_style_config(void)
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

void ui_status_obj_config(lv_obj_t* status_obj)
{
    lv_obj_set_pos(status_obj, UI_STATUS_POS_X, UI_STATUS_POS_Y);
    lv_obj_set_size(status_obj, UI_STATUS_WIDTH, UI_STATUS_HEIGHT + UI_GAP_1);
    lv_label_set_long_mode(status_obj, LV_LABEL_LONG_WRAP);

    lv_label_set_text_fmt(status_obj, " ");
    lv_obj_set_style_text_font(status_obj, &lv_font_FontAwesom5_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_obj, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(status_obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_add_style(status_obj, &g_common_txt_style, 0);
}

void ui_emoji_obj_config(lv_obj_t* emoji_obj)
{
#if EMOJI_USE_FONT  // 表情部分使用图片[0]/字体[1]
    lv_obj_set_pos(emoji_obj, UI_EMOJI_TXT_POS_X, UI_EMOJI_TXT_POS_Y);
    lv_obj_set_size(emoji_obj, UI_EMOJI_TXT_WIDTH, UI_EMOJI_TXT_HEIGHT);
    lv_obj_set_style_text_font(emoji_obj, &lv_font_segui_emoji, LV_PART_MAIN);
    lv_label_set_text_fmt(emoji_obj, " ");
    lv_obj_set_style_text_color(emoji_obj, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(emoji_obj, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(emoji_obj, LV_SCROLLBAR_MODE_OFF);

    lv_obj_add_style(emoji_obj, &g_common_txt_style, 0);
#else
    ////lv_img_set_src(img, "L:/test.png");
    //lv_image_set_src(emoji_obj, &img_emoji_0xF600);
    //lv_obj_set_pos(emoji_obj, UI_EMOJI_POS_X, UI_EMOJI_POS_Y);
    //lv_obj_set_size(emoji_obj, UI_EMOJI_WIDTH, UI_EMOJI_HEIGHT);
    ////lv_obj_align(emoji_obj, LV_ALIGN_CENTER, 0, 0);
#endif
}

void ui_full_text_obj_config(lv_obj_t* full_txt_obj, lv_obj_t* full_txt)
{
    lv_obj_set_pos(full_txt_obj, UI_FULL_TEXT_POS_X, UI_FULL_TEXT_POS_Y);
    lv_obj_set_size(full_txt_obj, UI_FULL_TEXT_WIDTH, UI_FULL_TEXT_HEIGHT);
    lv_obj_set_flex_flow(full_txt_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(full_txt_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(full_txt_obj, LV_SCROLLBAR_MODE_OFF);

    lv_obj_set_pos(full_txt, UI_FULL_TEXT_POS_X, UI_FULL_TEXT_POS_Y);
    lv_obj_set_size(full_txt, UI_FULL_TEXT_WIDTH, UI_FULL_TEXT_HEIGHT);

    lv_label_set_text_fmt(full_txt, " ");
    lv_label_set_long_mode(full_txt, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(full_txt, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);
    lv_obj_set_style_text_color(full_txt, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_align(full_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_add_style(full_txt, &g_common_txt_style, 0);
}

void ui_interact_obj_config(lv_obj_t* interact_obj, lv_obj_t* txt_prompt, lv_obj_t* txt_chat)
{
////////////////////////////////////////////////////////////
// 父组件的属性
    lv_obj_set_style_pad_left(interact_obj, 0, 0);
    lv_obj_set_style_pad_right(interact_obj, 0, 0);
    lv_obj_set_style_pad_bottom(interact_obj, 0, 0);
    lv_obj_set_style_bg_opa(interact_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(interact_obj, lv_color_make(255, 255, 255), 0);
    lv_obj_set_style_border_width(interact_obj, 0, 0);
    lv_obj_set_pos(interact_obj, UI_INTERACT_OBJ_POS_X, UI_INTERACT_OBJ_POS_Y);
    lv_obj_set_size(interact_obj, UI_INTERACT_OBJ_WIDTH, UI_INTERACT_OBJ_HEIGHT);
    lv_obj_set_scrollbar_mode(interact_obj, LV_SCROLLBAR_MODE_OFF);  // Never show the scrollbars
////////////////////////////////////////////////////////////
// 子组件的属性：状态提示、半屏对话区域

// 子组件的属性：状态提示、半屏对话区域
    lv_obj_set_pos(txt_prompt, UI_PROMPT_POS_X, UI_PROMPT_POS_Y);
    lv_obj_set_size(txt_prompt, UI_PROMPT_WIDTH, UI_PROMPT_HEIGHT);
    lv_obj_set_style_text_align(txt_prompt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(txt_prompt, lv_color_make(0, 0, 0), 0);
    lv_obj_add_style(txt_prompt, &g_common_txt_style, 0);

    lv_obj_set_pos(txt_chat, UI_CHAT_TXT_POS_X, UI_CHAT_TXT_POS_Y);
    lv_obj_set_size(txt_chat, UI_CHAT_TXT_WIDTH, UI_CHAT_TXT_HEIGHT);
    lv_obj_set_style_text_align(txt_chat, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(txt_chat, lv_color_make(0, 0, 0), 0);
    lv_obj_add_style(txt_chat, &g_common_txt_style, 0);
}

void ui_text_area_set_default_font(lv_obj_t *text_area, uint8_t default_font)
{
    if (text_area == NULL) {
        return;
    }

    if (default_font) {
        lv_obj_set_style_text_font(text_area, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);
    } else {
        #if defined(SUPPORT_LFS)
        if (xh_lv_font_bin_get() == NULL) {
            xh_lv_font_bin_init();
        }
        if (xh_lv_font_bin_get() != NULL) {
            lv_obj_set_style_text_font(text_area, xh_lv_font_bin_get(), LV_PART_MAIN);
        }
        else
        #endif
        {
            lv_obj_set_style_text_font(text_area, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// 更新表情图标
static volatile bool s_disp_emoji_valid;
static char s_disp_emoji_pending[5] = {0};  // 4字节 Unicode 码点 UTF-8 编码最长占 4 字节 + NUL 结尾

void disp_set_emoji_utf8(const char *utf8)
{
    if (utf8 == NULL) {
        s_disp_emoji_pending[0] = '\0';
        s_disp_emoji_valid = false;
        return;
    } else {
        (void)strncpy(s_disp_emoji_pending, utf8, 4);
        s_disp_emoji_pending[4] = '\0';
        s_disp_emoji_valid = true;
    }
}
bool disp_take_emoji_utf8(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0U) {
        return false;
    }
    if (!s_disp_emoji_valid) {
        return false;
    }
    (void)strncpy(out, s_disp_emoji_pending, out_sz - 1U);
    out[out_sz - 1U] = '\0';
    s_disp_emoji_valid = false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// 对话完成后，进入倾听状态，打开定时器，超时后清除对话框的文本，避免上一次对话的文本一直残留
static lv_timer_t *s_clear_text_timer = NULL;
static lv_obj_t *s_text_area = NULL;
static void clear_text_timer_cb(lv_timer_t *timer)
{
    if (timer != NULL) {
        lv_timer_del(timer);
    }
    s_clear_text_timer = NULL;
    if (s_text_area != NULL) {
        lv_label_set_text(s_text_area, " ");
    }
    s_text_area = NULL;
}

void disp_clear_text(lv_obj_t *text_area, uint32_t delay_ms)
{
    if (text_area == NULL) {
        s_text_area = NULL;
        return;
    }
    if (delay_ms == 0) {
        lv_label_set_text(text_area, " ");
        return;
    }
    if (s_clear_text_timer != NULL) {
        lv_timer_del(s_clear_text_timer);
        s_clear_text_timer = NULL;
    }

    s_text_area = text_area;
    s_clear_text_timer = lv_timer_create(clear_text_timer_cb, delay_ms, NULL);
}
// 对话完成后，进入倾听状态，打开定时器，超时后清除对话框的文本，避免上一次对话的文本一直残留
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// AgentTask 一次最多写入 DISP_LVGL_TEXT_PENDING_MAX，等待显示，超过长度部分被丢弃
// LvglTask 在 eDisp_Text_Update 中由 disp_show_pending_text_utf8 消费；短文本一次显示，长文本按行定时滚动
static volatile bool s_disp_lvgl_text_pending_valid;
static char s_disp_lvgl_text_pending[DISP_LVGL_TEXT_PENDING_MAX];

void disp_set_pending_text_utf8(const char *utf8)
{
    if (utf8 == NULL) {
        s_disp_lvgl_text_pending[0] = '\0';
        s_disp_lvgl_text_pending_valid = false;
        return;
    }
    (void)strncpy(s_disp_lvgl_text_pending, utf8, sizeof(s_disp_lvgl_text_pending) - 1U);
    s_disp_lvgl_text_pending[sizeof(s_disp_lvgl_text_pending) - 1U] = '\0';
    s_disp_lvgl_text_pending_valid = true;
}

// 不管服务器给来的文本有多长，一次显示最多两行的文本
bool disp_take_pending_text_utf8(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0U) {
        return false;
    }
    if (!s_disp_lvgl_text_pending_valid) {
        return false;
    }
    (void)strncpy(out, s_disp_lvgl_text_pending, out_sz - 1U);
    out[out_sz - 1U] = '\0';
    s_disp_lvgl_text_pending_valid = false;
    return true;
}

#if 1
/*
 * 半屏对话长文本：显示窗口宽 DISP_SHOW_TEXT_MAX_LEN（60）字节，每次定时器将窗口起点后移
 * DISP_SHOW_TEXT_LINE_LEN（30）字节（UTF-8 安全回退），两帧之间内容重叠 30 字节。
 */
static char disp_chat_text[DISP_SHOW_TEXT_MAX_LEN + 1];
static char *p_show_text = NULL;
static lv_obj_t *s_show_text_area = NULL;
static lv_timer_t *s_show_text_timer = NULL;

static int utf8_safe_rollback(const char *start, const char *current)
{
    int len = (int)(current - start);
    if (len <= 0) {
        return 0;
    }

    const char *p = current;
    while (p > start) {
        p--;
        if ((*p & 0xC0) != 0x80) {
            break;
        }
    }
    return (int)(p - start);
}

/** 仅停掉滚动定时器；不得清除 s_disp_lvgl_text_pending_valid，否则首帧后回调立刻失效 */
static void disp_scroll_timer_stop(void)
{
    if (s_show_text_timer != NULL) {
        lv_timer_del(s_show_text_timer);
        s_show_text_timer = NULL;
    }
}

static void disp_scroll_reset_pointers(void)
{
    p_show_text      = NULL;
    s_show_text_area = NULL;
}

static void disp_scroll_finish(void)
{
    disp_scroll_timer_stop();
    disp_scroll_reset_pointers();
    s_disp_lvgl_text_pending_valid = false;
}

static void show_text_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_obj_t   *area     = s_show_text_area;
    const char *base     = s_disp_lvgl_text_pending;
    char       *text_p   = p_show_text;

    if (!s_disp_lvgl_text_pending_valid || text_p == NULL || area == NULL || !lv_obj_is_valid(area)) {
        disp_scroll_finish();
        return;
    }

    size_t total_len      = strlen(base);
    size_t current_offset = (size_t)(text_p - base);

    size_t target_offset = current_offset + (size_t)DISP_SHOW_TEXT_LINE_LEN+1;
    if (target_offset < total_len) {
        target_offset = (size_t)utf8_safe_rollback(base, base + target_offset);
        p_show_text     = (char *)(base + target_offset);
        text_p          = p_show_text;
        current_offset  = target_offset;
    }

    size_t remain_len = total_len - current_offset;
    size_t want_copy  = 0;
    int    safe_len   = 0;
    if (remain_len > (size_t)DISP_SHOW_TEXT_MAX_LEN) {
        want_copy = (size_t)DISP_SHOW_TEXT_MAX_LEN;
        safe_len  = utf8_safe_rollback(text_p, text_p + want_copy);
    } else {
        want_copy = remain_len;
        safe_len  = want_copy;
    }

    if (safe_len <= 0) {
        disp_scroll_finish();
        log_debug("[disp_show_text] show_text_timer_cb, text show end, safe_len<=0\r\n");
        return;
    }

    memcpy(disp_chat_text, text_p, (size_t)safe_len);
    disp_chat_text[safe_len] = '\0';
    lv_label_set_text(area, disp_chat_text);

    if (current_offset + (size_t)DISP_SHOW_TEXT_MAX_LEN >= total_len) {
        disp_scroll_finish();
        log_debug("[disp_show_text] show_text_timer_cb, text show end\r\n");
    }
}

bool disp_show_pending_text_utf8(lv_obj_t *text_area)
{
    if (!lv_obj_is_valid(text_area)) {
        log_error("[disp_show_text] disp_show_pending_text_utf8, text_area is NULL\r\n");
        return false;
    }
    if (!s_disp_lvgl_text_pending_valid) {
        lv_label_set_text(text_area, " ");
        //log_error("[disp_show_text] disp_show_pending_text_utf8, no text\r\n");
        return false;
    }

    disp_scroll_timer_stop();
    disp_scroll_reset_pointers();

    size_t      total_len = strlen(s_disp_lvgl_text_pending);
    const char *src       = s_disp_lvgl_text_pending;

    if (total_len <= (size_t)DISP_SHOW_TEXT_MAX_LEN) {
        log_debug("[disp_show_text] disp_show_pending_text_utf8, once\r\n");
        lv_label_set_text(text_area, s_disp_lvgl_text_pending);
        s_disp_lvgl_text_pending_valid = false;
        return true;
    }

    p_show_text      = (char *)src;
    s_show_text_area = text_area;

    int first_len = utf8_safe_rollback(src, src + (size_t)DISP_SHOW_TEXT_MAX_LEN);
    if (first_len <= 0) {
        lv_label_set_text(text_area, " ");
        s_disp_lvgl_text_pending_valid = false;
        return false;
    }
    memcpy(disp_chat_text, src, (size_t)first_len);
    disp_chat_text[first_len] = '\0';
    lv_label_set_text(text_area, disp_chat_text);

    s_show_text_timer = lv_timer_create(show_text_timer_cb, DISP_SHOW_TEXT_DELAY_MS, NULL);
    return true;
}
#endif
/////////////////////////////////////////////////////////////////////////////////////

static volatile bool s_disp_activation_hint_valid;
static char s_disp_activation_hint[DISP_ACTIVATION_HINT_MAX_LEN];

void disp_set_activation_hint_utf8(const char *utf8)
{
    if (utf8 == NULL) {
        s_disp_activation_hint[0] = '\0';
        s_disp_activation_hint_valid = false;
        return;
    }
    (void)strncpy(s_disp_activation_hint, utf8, sizeof(s_disp_activation_hint) - 1U);
    s_disp_activation_hint[sizeof(s_disp_activation_hint) - 1U] = '\0';
    s_disp_activation_hint_valid = true;
}

bool disp_take_activation_hint_utf8(char *out, size_t out_sz)
{
    if (out == NULL || out_sz == 0U) {
        return false;
    }
    if (!s_disp_activation_hint_valid) {
        return false;
    }
    (void)strncpy(out, s_disp_activation_hint, out_sz - 1U);
    out[out_sz - 1U] = '\0';
    s_disp_activation_hint_valid = false;
    return true;
}