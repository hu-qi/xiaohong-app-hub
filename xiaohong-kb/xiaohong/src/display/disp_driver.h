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

#ifndef __DISP_DRIVER_H__
#define __DISP_DRIVER_H__

#include "board_config.h"
#include <stdbool.h>
#include <stddef.h>

#define LCD_CMD                        (0)     // LCD Cmd/Data: [Lo-Cmd, Hi-Data]
#define LCD_DAT                        (1)     // LCD Cmd/Data: [Lo-Cmd, Hi-Data]

#define LCD_BLK_PWM_PORT               (2)     // LCD Backlight PWM port
#define LCD_BLK_PWM_MAX_DIV_NUM_MAX    (8)
#define LCD_BLK_PWM_DUTY_MIN           (0)     // Duty[1,99]:[1-Min, 50-Half Brightness, 99-Max]
#define LCD_BLK_PWM_DUTY_MAX           (100)   // Duty[1,99]:[1-Min, 50-Half Brightness, 99-Max]
#define LCD_BLK_PWM_DUTY_DEF           (40)    // Duty[1,99]:[1-Min, 50-Half Brightness, 99-Max]
#define LCD_BLK_PWM_DUTY_STEP          (2)     // Duty Step
#define LCD_BLK_PWM_FREQ_STEP          (10)    // Freq Step
#define LCD_BLK_PWM_FREQ_DEF           (20000)  // PWM Port can NOT output a signal lower than 1230hz, or else using gpio analog a pwm signal

// 注意：显示屏正常的角度是LCD_PANEL_ROTATION_0[0],
// 但小鸿键盘的显示屏默认倒装，烧录固件后，nv被覆盖，读取到的旋转角度是0
// 为使默认情况下显示屏看上去显示"正常"，这里定义LCD_PANEL_ROTATION_180[0]
#define LCD_PANEL_ROTATION_0     (2)
#define LCD_PANEL_ROTATION_90    (3)
#define LCD_PANEL_ROTATION_180   (0)
#define LCD_PANEL_ROTATION_270   (1)

static inline void lcd_set_reset_pin(uint8_t level)
{
    uapi_gpio_set_val(LCD_RST_PIN, level);
}
static inline void lcd_set_dc_pin(uint8_t level)
{
    uapi_gpio_set_val(LCD_DC_PIN, level);
}
static inline void lcd_set_cs_pin(uint8_t level)
{
    uapi_gpio_set_val(LCD_CS_PIN, level);
}

enum {
    eDisp_Blk_On,         // 亮屏, 与UI布局无关
    eDisp_Blk_Off,        // 息屏, 与UI布局无关
    eDisp_Set_Rotation,   // 旋转屏幕
    eDisp_Bar_Update,     // 状态栏, 一直存在，只是内容更新，与UI布局无关
    eDisp_All_Update,     // 刷新整页画面

    eDisp_Show_Main_Page,
    eDisp_Show_Local_Page,
    eDisp_Show_Setting_Page,
    eDisp_Show_KeyRemap_Page,
    eDisp_Show_WiFi_Page,

    eDisp_Show_Normal_Page,
    eDisp_Show_Sle_Pairing_Page,

    eDisp_HiLi_Up,        // 高亮光标上移
    eDisp_HiLi_Down,      // 高亮光标下移
    eDisp_HiLi_Confirm,   // 确认选中高亮项

    eDisp_LowBattery,     // 低电量提示
    eDisp_LowBatteryClear,// 电量恢复，清除低电量提示
    eDisp_Max,
};

errcode_t disp_write_buff(uint8_t cmd, uint8_t* buff, uint32_t len);
errcode_t disp_write_byte(uint8_t cmd, uint8_t data);

errcode_t disp_set_backlight_level(uint8_t level);  // level[0, 100]

void disp_init_panel(void);
void disp_reset_panel(void);
void disp_set_point(uint16_t x, uint16_t y);
void disp_set_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void disp_draw_point(uint16_t x, uint16_t y, uint16_t color);
void disp_clear(uint16_t color);
void disp_set_rotation(uint8_t rotation);

#endif//__DISP_DRIVER_H__
