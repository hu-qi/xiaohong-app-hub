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

#ifndef __LCD_ST7789_DRIVER_H__
#define __LCD_ST7789_DRIVER_H__

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "board_config.h"
#include "disp_driver.h"


#define LCD_PANEL_PIXEL_X  (240)
#define LCD_PANEL_PIXEL_Y  (240)

#define RED      0xF800
#define GREEN    0x07E0
#define BLUE     0x001F
#define WHITE    0xFFFF
#define BLACK    0x0000
#define YELLOW   0xFFE0
#define GRAY0    0xEF7D
#define GRAY1    0x8410
#define GRAY2    0x4208

void lcd_init(void);
void lcd_set_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_set_point(uint16_t x, uint16_t y);
void lcd_draw_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void lcd_clear(uint16_t color);
void lcd_set_rotation(uint8_t rotation);

#endif//__LCD_ST7789_DRIVER_H__