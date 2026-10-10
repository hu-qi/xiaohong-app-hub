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

/* 小鸿键盘的UI使用到的汉字列表：*/
/* Range: 0x20-0x7E,0x3000-0x303F,0xFF00-0xFFEF
0x20-0x7E: 可显示的ASCII编码字符
0x3000-0x303F: CJK标点
0xFF00-0xFFEF: 全角标点
*/
/* Symbols:
  小鸿键盘本地模式正常模式系统设置显示亮度旋转角度休眠时间列表已配对和连接无扫描结果同步显示工作状态按键重映射手机或连接热点打开浏览器访问在页面上配置自定义键码在页面上点击保存配置
*/

#include "board_config.h"
#include "disp_driver.h"
//#include "net_wifi_config.h"
#include "settings.h"
#include "lvgl.h"

#if defined(LCD_ST7789_240x240)
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
#define UI_SAFE_BORDER                 (4)    // 安全边界(四周留白)宽度(像素)
#define UI_COMMON_WIDTH                (DISP_WIDTH - UI_SAFE_BORDER*2)
// 去除边框的安全显示区域宽度(像素)
////////////////////////////////////////////////////////////////////////////
// 顶部状态栏
#define UI_STATUS_UP_POS_X             (UI_SAFE_BORDER)   // 顶部状态栏位置坐标x
#define UI_STATUS_UP_POS_Y             (UI_SAFE_BORDER)   // 顶部状态栏位置坐标y
#define UI_STATUS_UP_WIDTH             (UI_COMMON_WIDTH)  // 顶部状态栏宽度(像素)
#define UI_STATUS_UP_HEIGHT            (22)   // 顶部状态栏高度(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_GAP_1                       (2)    // 顶部状态栏与中间区域的间距(像素)
////////////////////////////////////////////////////////////////////////////
// 中间区域
#define UI_MAIN_AREA_POS_X             (UI_SAFE_BORDER)   // 中间区域位置坐标x
#define UI_MAIN_AREA_POS_Y             (UI_SAFE_BORDER+UI_STATUS_UP_HEIGHT+UI_GAP_1)   // 中间区域位置坐标y
#define UI_MAIN_AREA_WIDTH             (UI_COMMON_WIDTH)  // 中间区域宽度(像素)
#define UI_MAIN_AREA_HEIGHT            (DISP_HEIGHT-UI_MAIN_AREA_POS_Y*2)
// 中间区域高度(像素)
////////////////////////////////////////////////////////////////////////////
#define UI_GAP_2                       (2)    // 中间区域与底部状态栏的间距(像素)
////////////////////////////////////////////////////////////////////////////
// 底部状态栏
#define UI_STATUS_DM_POS_X             (UI_SAFE_BORDER)       // 底部状态栏位置坐标x
#define UI_STATUS_DM_POS_Y             (UI_MAIN_AREA_POS_Y+UI_MAIN_AREA_HEIGHT+UI_GAP_2) // 底部状态栏位置坐标y
#define UI_STATUS_DM_WIDTH             (UI_COMMON_WIDTH)      // 底部状态栏宽度(像素)
#define UI_STATUS_DM_HEIGHT            (UI_STATUS_UP_HEIGHT)  // 底部状态栏高度(像素)
////////////////////////////////////////////////////////////////////////////
//以上是UI区域布局的宏定义


// 状态栏显示的内容，用图标[1]/用文本[0]来显示WiFi状态、音量、亮度和电池电量
#define STATUS_USE_SYMBOL         (1)

#define ST_SYMBOL_MUTE            "\xEF\x9A\xA9"    // 0xF6A9
#define ST_SYMBOL_BRIGHTNESS      "\xEF\x81\x82"    // 0xf042

// 状态栏文字字体: 
// 优先FontAwesom5_18显示图标
// 其次fallback到SemiBold_18显示英文文字
// 再次fallback到montserrat_18显示缺失的文字
LV_FONT_DECLARE(lv_font_FontAwesom5_18);
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_18_Default);
// 非状态栏文字字体
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_22_Default);

//void ui_common_text_style_config(void);
//void ui_status_obj_config(void);
const lv_font_t* get_system_font(void);
const int32_t get_system_font_height(void);
void ui_set_text_line_space(int8_t sp);
void ui_title_text_center(lv_obj_t *title);

#define UI_COMMON_TITLE_PADDING   (0)
#define UI_COMMON_TITLE_HEIGHT    (get_system_font_height() + 2*UI_COMMON_TITLE_PADDING)
lv_obj_t *disp_create_common_title(char* name);

void disp_create_ui(void);
void disp_update_ui(uint8_t cmd);
void disp_update_ui_status(void);


//////////////////////////////////////////////////////////
// 以下是各个页面的光标移动、刷新等API

/* 显示开机画面主页面 */
void disp_create_main_page(void);


/* 显示本地模式的入口页面 */
void disp_create_local_page(void);
/* 移动页面上的按钮高亮光标：dir > 0 下移，dir < 0 上移 */
void disp_local_page_cursor_move(int8_t dir);
/* 确认选中当前高亮项，并调用接口打开对应页面 */
void disp_local_page_confirm(void);
/* 刷新页面默认的高亮按钮 */
void disp_local_page_refresh_highlight(void);

/* 显示设置页面 */
void disp_create_settings_page(void);
/* 设置页面高亮光标：dir > 0 下移，dir < 0 上移 */
void disp_settings_cursor_move(int8_t dir);
/* 确认选中当前高亮项，读取下拉选中值并写入设置（set_blk_level/set_rotation） */
void disp_settings_confirm(void);
/* 刷新设置页面当前高亮项 */
void disp_settings_refresh_highlight(void);

/* 显示按键重映射配置页面 */
void disp_create_key_remap_page(void);
/* 设置页面上下滚动：dir > 0 下移，dir < 0 上移 */
void disp_key_remap_cursor_move(int8_t dir);



/* 显示正常模式的主页面 */
void disp_create_normal_page(void);

/* 显示扫描结果、设备配对页面 */
void disp_create_sle_scan_pairing_page(void);
/* 配对列表高亮光标：dir > 0 下移，dir < 0 上移 */
void disp_sle_scan_pairing_cursor_move(int8_t dir);
/* 确认选中当前高亮项，并调用 accept_scan_result(idx) */
void disp_sle_scan_pairing_confirm(void);







// 以上是各个页面的光标移动、刷新等API
//////////////////////////////////////////////////////////

#endif//__LVGL_UI_LAYOUT_H__