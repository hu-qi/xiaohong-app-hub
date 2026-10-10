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

static inline void lcd_set_reset_pin(uint8_t level)
{
    uapi_gpio_set_val(LCD_RST_PIN, level);
}
static inline void lcd_set_dc_pin(uint8_t level)
{
    uapi_gpio_set_val(LCD_DC_PIN, level);
}
errcode_t lcd_set_cs_pin(uint8_t level);


enum {
    eDisp_Blk_On,         // 亮屏, 与UI布局无关
    eDisp_Blk_Off,        // 息屏, 与UI布局无关
    eDisp_Bar_Update,     // 状态栏, 一直存在，只是内容更新，与UI布局无关

    // 以下是互动文本区域, 与UI布局有关
    eDisp_Emoji_Update,   // 表情更新, 与UI布局有关
    eDisp_Prompt_Update,  // 提示语更新, 与UI布局有关
    eDisp_Text_Update,    // 文本更新, 与UI布局有关

    // 以下是全屏显示的文本, 与UI布局有关
    eDisp_Welcome,
    eDisp_Hand_Shaking,
    eDisp_Activation_Hint,
    eDisp_WiFi_St_Update,
    eDisp_WiFi_ReConfig_Success,
    eDisp_Font_Dl_ReleaseBin, /* 先释放流式字库 lv_fs 句柄，再允许覆盖 /system/font.bin */
    eDisp_Font_Dl_Progress,
    eDisp_Font_Dl_Complete,
    eDisp_Font_Dl_Already,
    eDisp_Font_Dl_Restore,

    eDisp_Ota_FW_Update,
    eDisp_Ota_FW_Update_Progress,
    eDisp_Ota_FW_Update_Complete,
    eDisp_Ota_FW_Update_Failed,

    // debug use only
    eDisp_Debug_Font_Delete,

    // app hub events
    eDisp_AppHub_Open,
    eDisp_AppHub_Close,
    eDisp_AppHub_Tick,
    eDisp_AppHub_KeyBase,    /* + eKey_VolUp_ShortPressed(0) .. eKey_Wakeup_LongPressed_5S(5) */
    eDisp_Max,
};

errcode_t disp_write_buff(uint8_t cmd, uint8_t* buff, uint32_t len);
errcode_t disp_write_byte(uint8_t cmd, uint8_t data);

errcode_t disp_set_backlight_level(uint8_t level);  // level[0, 100]
//errcode_t disp_set_backlight_default(void);
//errcode_t disp_set_backlight_off(void);

void disp_init_panel(void);
void disp_reset_panel(void);
void disp_set_point(uint16_t x, uint16_t y);
void disp_set_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void disp_draw_point(uint16_t x, uint16_t y, uint16_t color);
void disp_clear(uint16_t color);

#endif//__DISP_DRIVER_H__
