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
#include <unistd.h>
#include "disp_driver.h"
#include "lcd_st7789_driver.h"

void lcd_init(void)
{
    uint8_t buff[16] = {0};

    printf("[lcd_init] st7789\r\n");
    disp_write_byte(LCD_CMD, 0x11); //Sleep exit
    usleep(100*1000);

//--------------------------------ST7789S Frame rate setting----------------------------------// 
    // set Column address
    disp_write_byte(LCD_CMD, 0x2A);
    buff[0] = 0x00;
    buff[1] = 0x00;  //start column 0x0000
    buff[2] = 0x00;
    buff[3] = 0xEF;  //end   column 0x00EF = 239
    disp_write_buff(LCD_DAT, buff, 4);

    // set Row address
    disp_write_byte(LCD_CMD, 0x2B);
    buff[0] = 0x00;
    buff[1] = 0x00;  //start row 0x0028 = 40
    buff[2] = 0x00;
    buff[3] = 0xEF;  //end   row 0x0117 = 279 (-40=239)
    disp_write_buff(LCD_DAT, buff, 4);

    // Porch control
    disp_write_byte(LCD_CMD, 0xB2);
    buff[0] = 0x0C;
    buff[1] = 0x0C;
    buff[2] = 0x00;
    buff[3] = 0x33;
    buff[4] = 0x33;
    disp_write_buff(LCD_DAT, buff, 5);

    // Display Inversion Off
    disp_write_byte(LCD_CMD, 0x20);

    // Gate control
    disp_write_byte(LCD_CMD, 0xB7);
    disp_write_byte(LCD_DAT, 0x56);

//---------------------------------ST7789S Power setting--------------------------------------// 
    disp_write_byte(LCD_CMD, 0xBB);  // VCOMS Setting
    disp_write_byte(LCD_DAT, 0x18);

    disp_write_byte(LCD_CMD, 0xC0);  // LCM Control
    disp_write_byte(LCD_DAT, 0x2C);

    disp_write_byte(LCD_CMD, 0xC2);  // VDV and VRH Command Enable
    disp_write_byte(LCD_DAT, 0x01);

    disp_write_byte(LCD_CMD, 0xC3);  // VRH Set
    disp_write_byte(LCD_DAT, 0x1F);

    disp_write_byte(LCD_CMD, 0xC4);  // VDV Setting
    disp_write_byte(LCD_DAT, 0x20);

    disp_write_byte(LCD_CMD, 0xC6);  // FR Control 2
    disp_write_byte(LCD_DAT, 0x0F);

    disp_write_byte(LCD_CMD, 0xD0);  // Power Control 1
    disp_write_byte(LCD_DAT, 0xA6);
    disp_write_byte(LCD_DAT, 0xA1);
//--------------------------------ST7789S gamma setting---------------------------------------// 
    disp_write_byte(LCD_CMD, 0xE0);
    buff[0]  = 0xD0;
    buff[1]  = 0x0D;
    buff[2]  = 0x14;
    buff[3]  = 0x0B;
    buff[4]  = 0x0B;
    buff[5]  = 0x07;
    buff[6]  = 0x3A;
    buff[7]  = 0x44;
    buff[8]  = 0x50;
    buff[9]  = 0x08;
    buff[10] = 0x13;
    buff[11] = 0x13;
    buff[12] = 0x2D;
    buff[13] = 0x32;
    disp_write_buff(LCD_DAT, buff, 14);

    disp_write_byte(LCD_CMD, 0xE1);        // Negative Voltage Gamma Contro
    disp_write_buff(LCD_DAT, buff, 14);

    disp_write_byte(LCD_CMD, 0x36);     // Memory data access control
    disp_write_byte(LCD_DAT, 0x00);

    disp_write_byte(LCD_CMD, 0x3A);     // Column Mode/Interface pixel format
    disp_write_byte(LCD_DAT, 0x55);     // 65K: 0x55[565] // 262K: 0x66[RGB 6 6 6]

    disp_write_byte(LCD_CMD, 0xE7);     // enable data channel 2
    disp_write_byte(LCD_DAT, 0x00);

    disp_write_byte(LCD_CMD, 0x21);     // Display inversion on
    disp_write_byte(LCD_CMD, 0x29);     // Display on
}

void lcd_set_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buff[4] = {0};

    buff[0] = (x0 & 0xFF00) >> 8;
    buff[1] = (x0 & 0x00FF) >> 0;
    buff[2] = (x1 & 0xFF00) >> 8;
    buff[3] = (x1 & 0x00FF) >> 0;
    disp_write_byte(LCD_CMD, 0x2A);
    disp_write_buff(LCD_DAT, buff, 4);

    buff[0] = (y0 & 0xFF00) >> 8;
    buff[1] = (y0 & 0x00FF) >> 0;
    buff[2] = (y1 & 0xFF00) >> 8;
    buff[3] = (y1 & 0x00FF) >> 0;
    disp_write_byte(LCD_CMD, 0x2B);
    disp_write_buff(LCD_DAT, buff, 4);

    disp_write_byte(LCD_CMD, 0x2C);
}

void lcd_set_point(uint16_t x, uint16_t y)
{
    lcd_set_region(x, y, x, y);
}

void lcd_draw_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    uint16_t i, j;
    uint8_t buff[2] = {0};
    buff[0] = (color&0xFF00)>>8;
    buff[1] = (color&0x00FF)>>0;

    lcd_set_region(x0, y0, x1-1, y1-1);
    disp_write_byte(LCD_CMD, 0x2C);
    for(i=x0; i<x1; i++) {
        for(j=y0; j<y1; j++) {
            disp_write_buff(LCD_DAT, buff, 2);
        }
    }
}

void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t buff[2] = {0};
    buff[0] = (color & 0xFF00) >> 8;
    buff[1] = (color & 0x00FF) >> 0;

    lcd_set_region(x, y, x+1, y+1);
    disp_write_buff(LCD_DAT, buff, 2);
}

void lcd_clear(uint16_t color)
{
    lcd_draw_region(0, 0, LCD_PANEL_PIXEL_X, LCD_PANEL_PIXEL_Y, color);
}


//显示屏画面旋转配置，需要结合 lvgl_task.c 的 disp_flush_cb() 来旋转画面:
//       正向 0°: 原始坐标 + MADCTL[0x00] OK  无旋转
// 顺时针旋转 90°: 原始坐标 + MADCTL[0x60] OK  硬件旋转正常
// 顺时针旋转180°: 原始坐标 + MADCTL[0xC0] NG, 坐标旋转 + MADCTL[0xC0] NG ==>> 硬件旋转异常
// 顺时针旋转180°: 坐标旋转 + 像素数据顺序旋转 + MADCTL[0x00] OK ==>> 放弃硬件旋转，使用软件旋转
// 顺时针旋转270°: 原始坐标 + MADCTL[0xA0] NG, 坐标旋转 + MADCTL[0xA0] NG ==>> 硬件旋转异常
// 顺时针旋转270°: 坐标旋转 + 像素数据顺序旋转 + MADCTL[0x60] OK ==>> 软件旋转180度 + 硬件旋转90度
void lcd_set_rotation(uint8_t rotation)
{
    uint8_t madctl;
    switch (rotation) {
        case LCD_PANEL_ROTATION_90:     /*  1 → 90° CW   (MV=1, MX=1) */
            madctl = 0x60;
            break;
        case LCD_PANEL_ROTATION_180:    /*  2 → 180°     (MY=1, MX=1) */
            madctl = 0x00;              //0xC0;
            break;
        case LCD_PANEL_ROTATION_270:    /*  3 → 90° CCW  (MV=1, MY=1) */
            madctl = 0x60;              //0xA0;
            break;
        default:                        /*  0 → 0° (正常) */
            madctl = 0x00;
            break;
    }

    disp_write_byte(LCD_CMD, 0x36);
    disp_write_byte(LCD_DAT, madctl);
}
