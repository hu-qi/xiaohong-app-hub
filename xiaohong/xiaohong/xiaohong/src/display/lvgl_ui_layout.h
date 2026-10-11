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

#ifndef __LVGL_UI_LAYOUT_H__
#define __LVGL_UI_LAYOUT_H__

#include "board_config.h"
#include "disp_driver.h"
#include "net_wifi_config.h"
#include "settings.h"
#include "lvgl.h"

#if defined(LCD_ST7789S_240x240)
  #include "lcd_st7789_driver.h"
#endif

#if defined(LCD_PANEL_PIXEL_X)
  #define DISP_WIDTH  LCD_PANEL_PIXEL_X
#else
  #error LCD_PANEL_PIXEL_X unknown
#endif

#if defined(LCD_PANEL_PIXEL_Y)
  #define DISP_HEIGHT  LCD_PANEL_PIXEL_Y
#else
  #error LCD_PANEL_PIXEL_Y unknown
#endif

//以下是UI区域布局的宏定义
////////////////////////////////////////////////////////////////////////////
#define UI_SAFE_BORDER                 (6)    // 安全边界(四周留白)宽度(像素)
#define UI_COMMON_WIDTH                (DISP_WIDTH - UI_SAFE_BORDER*2)    // 边框宽度(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_STATUS_POS_X                (UI_SAFE_BORDER)   // 状态栏位置坐标x
#define UI_STATUS_POS_Y                (UI_SAFE_BORDER)   // 状态栏位置坐标y
#define UI_STATUS_WIDTH                (UI_COMMON_WIDTH)  // 状态栏宽度(像素)
#define UI_STATUS_HEIGHT               (20)   // 状态栏高度(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_GAP_1                       (12)    // 状态栏与表情图标的间距(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_EMOJI_POS_X                 ((DISP_WIDTH-UI_EMOJI_WIDTH)/2)   // 表情图标位置坐标x
#define UI_EMOJI_POS_Y                 (UI_STATUS_POS_Y + UI_STATUS_HEIGHT + UI_GAP_1)   // 表情图标位置坐标y
#define UI_EMOJI_WIDTH                 (72)   // emoji图标宽度(像素)
#define UI_EMOJI_HEIGHT                (72)   // emoji图标高度(像素)

#define UI_EMOJI_TXT_POS_X             (UI_SAFE_BORDER)   // 表情图标位置坐标x
#define UI_EMOJI_TXT_POS_Y             (UI_STATUS_POS_Y + UI_STATUS_HEIGHT + UI_GAP_1)   // 表情图标位置坐标y
#define UI_EMOJI_TXT_WIDTH             (UI_COMMON_WIDTH)   // emoji图标宽度(像素)
#define UI_EMOJI_TXT_HEIGHT            (72)   // emoji图标高度(像素)

////////////////////////////////////////////////////////////////////////////
#define UI_GAP_2                       (2)    // 表情图标与提示栏的间距(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_PROMPT_POS_X                (0)  //(UI_SAFE_BORDER)   // 提示栏位置坐标x
#define UI_PROMPT_POS_Y                (0)  //(UI_EMOJI_POS_Y + UI_EMOJI_HEIGHT + UI_GAP_2)   // 提示栏位置坐标y
#define UI_PROMPT_WIDTH                (UI_COMMON_WIDTH)  // 提示栏宽度(像素)
#define UI_PROMPT_HEIGHT               (40)   // 提示栏高度(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_GAP_3                       (2)    // 提示栏与对话文本的间距(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_CHAT_TXT_POS_X              (0) // (UI_SAFE_BORDER)   // 对话框位置坐标x
#define UI_CHAT_TXT_POS_Y              (UI_PROMPT_POS_Y + UI_PROMPT_HEIGHT + UI_GAP_3)   // 对话框位置坐标y
#define UI_CHAT_TXT_WIDTH              (UI_COMMON_WIDTH)  // 对话框宽度(像素)
#define UI_CHAT_TXT_HEIGHT             (DISP_HEIGHT - UI_SAFE_BORDER*2 - UI_STATUS_HEIGHT - UI_EMOJI_HEIGHT - UI_PROMPT_HEIGHT - UI_GAP_1 - UI_GAP_2 - UI_GAP_3 - UI_GAP_3)   // 对话框高度(像素)
////////////////////////////////////////////////////////////////////////////
//全屏文本区域，用于显示配网步骤说明、字库下载等提示信息
#define UI_FULL_TEXT_POS_X             (UI_SAFE_BORDER)   // 全屏文本位置坐标x
#define UI_FULL_TEXT_POS_Y             (UI_STATUS_POS_Y + UI_STATUS_HEIGHT + UI_GAP_1)   // 全屏文本位置坐标y
#define UI_FULL_TEXT_WIDTH             (UI_COMMON_WIDTH)  // 全屏文本宽度(像素)
#define UI_FULL_TEXT_HEIGHT            (DISP_HEIGHT - UI_SAFE_BORDER*2 - UI_STATUS_HEIGHT - UI_GAP_1)    // 全屏文本高度(像素)
////////////////////////////////////////////////////////////////////////////
//互动文本区域，包含状态提示、半屏对话区域两个子组件, 跟全屏文本区域完全重叠
#define UI_INTERACT_OBJ_POS_X          (UI_SAFE_BORDER)   // 互动文本区域位置坐标x
#define UI_INTERACT_OBJ_POS_Y          (UI_EMOJI_POS_Y + UI_EMOJI_HEIGHT)   // 互动文本区域位置坐标y
#define UI_INTERACT_OBJ_WIDTH          (UI_COMMON_WIDTH)  // 互动文本区域宽度(像素)
#define UI_INTERACT_OBJ_HEIGHT         (DISP_HEIGHT - UI_SAFE_BORDER*2 - UI_STATUS_HEIGHT - UI_EMOJI_HEIGHT - UI_GAP_1)  // 互动文本区域高度(像素)
////////////////////////////////////////////////////////////////////////////
//以上是UI区域布局的宏定义


// 状态栏显示的内容，用图标[1]/用文本[0]来显示WiFi状态、音量、亮度和电池电量
#define STATUS_USE_SYMBOL    (1)
// 表情部分使用字体[1]/图片[0], 若用图片，需要增加图片资源和消耗系统资源，用字体则只需增加对应的字体资源即可。
#define EMOJI_USE_FONT       (1)

#define ST_SYMBOL_MUTE            "\xEF\x9A\xA9"    // 0xF6A9
#define ST_SYMBOL_BRIGHTNESS      "\xEF\x81\x82"    // 0xf042

LV_FONT_DECLARE(lv_font_FontAwesom5_18);  // 状态栏字体
LV_FONT_DECLARE(lv_font_segui_emoji);  // 表情字体
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_22_Default);
void ui_text_area_set_default_font(lv_obj_t *text_area, uint8_t default_font);

void ui_common_text_style_config(void);
void ui_status_obj_config(lv_obj_t* status_obj);
void ui_emoji_obj_config(lv_obj_t* emoji_obj);
void ui_full_text_obj_config(lv_obj_t* full_txt_obj, lv_obj_t* full_txt);
void ui_interact_obj_config(lv_obj_t* interact_obj, lv_obj_t* txt_prompt, lv_obj_t* txt_chat);
void ui_set_text_line_space(int8_t sp);

void disp_create_ui(void);
void disp_update_ui(uint8_t cmd);
void disp_update_ui_status(lv_obj_t* label_st);
void disp_clear_text(lv_obj_t *text_area, uint32_t delay_ms);

/* 对话框缓存的对话文字的大小, 最大缓存128个字符或64个汉字 */
#define DISP_LVGL_TEXT_PENDING_MAX     (256)   // 128个字符或64个汉字
/** 长文本滚动：每次将显示窗口起点后移的字节数（UTF-8 安全），与下一帧重叠 DISP_SHOW_TEXT_MAX_LEN - 本值 */
#define DISP_SHOW_TEXT_LINE_LEN        (30)
/** 长文本滚动：单帧 label 文本最大字节数（窗口宽度）；亦为其它模块行缓冲参考（如激活码） */
#define DISP_SHOW_TEXT_MAX_LEN         (DISP_SHOW_TEXT_LINE_LEN * 2 + 1)
#define DISP_SHOW_TEXT_DELAY_MS        (150)

void disp_set_emoji_utf8(const char *utf8);
bool disp_take_emoji_utf8(char *out, size_t out_sz);

void disp_set_pending_text_utf8(const char *utf8);
bool disp_take_pending_text_utf8(char *out, size_t out_sz);
bool disp_show_pending_text_utf8(lv_obj_t *text_area);

#define DISP_ACTIVATION_HINT_MAX_LEN    (8)  // 设备激活码最大长度为6位数字
void disp_set_activation_hint_utf8(const char *utf8);
bool disp_take_activation_hint_utf8(char *out, size_t out_sz);


#endif//__LVGL_UI_LAYOUT_H__