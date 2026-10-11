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
#include <stdbool.h>
#include "disp_driver.h"
#include "log.h"

#define  TAG  "[disp_drv]"

errcode_t lcd_set_cs_pin(uint8_t level)
{
    uint16_t timeout = 0;
    errcode_t ret = spi_cs_pin_race(LCD_CS_PIN, level);
    while (ERRCODE_SUCC != ret) {
        usleep(1000);  // wait 1ms
        timeout++;
        if (timeout >= SPI_CS_PIN_RACE_TIMEOUT) {  // timeout
            printf("lcd_set_cs_pin: spi_cs_pin_race ==>> NG, timeout\r\n");
            return ERRCODE_FAIL;
        }
        ret = spi_cs_pin_race(LCD_CS_PIN, level);
    }
    return ret;
}

errcode_t disp_write_buff(uint8_t cmd, uint8_t* buff, uint32_t len)
{
    spi_xfer_data_t spi_data = {
        .tx_buff  = buff,
        .tx_bytes = len,
    };

    if (ERRCODE_SUCC != lcd_set_cs_pin(GPIO_LEVEL_LOW)) {
        return ERRCODE_FAIL;
    }
    lcd_set_dc_pin(cmd);  // DC(cmd): [Lo-Cmd, Hi-Data]
    errcode_t ret = uapi_spi_master_write(SPI_IDX, &spi_data, 0xFFFF);
    lcd_set_cs_pin(GPIO_LEVEL_HIGH);
    return ret;
}

errcode_t disp_write_byte(uint8_t cmd, uint8_t data)
{
    return disp_write_buff(cmd, &data, 1);
}

void disp_reset_panel(void)
{
    lcd_set_cs_pin(GPIO_LEVEL_HIGH);

    lcd_set_reset_pin(GPIO_LEVEL_LOW);
    usleep(20*1000);

    lcd_set_reset_pin(GPIO_LEVEL_HIGH);
    usleep(10*1000);
}

void disp_init_panel(void)
{
    disp_reset_panel();
    lcd_init();
}

void disp_set_point(uint16_t x, uint16_t y)
{
    lcd_set_point(x, y);
}

void disp_set_region(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    lcd_set_region(x0, y0, x1, y1);
}

void disp_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_point(x, y, color);
}

void disp_clear(uint16_t color)
{
    lcd_clear(color);
}

static errcode_t disp_set_backlight(uint8_t port, uint8_t in_duty, unsigned int in_freq)
{
    uint8_t duty = in_duty;
    unsigned int freq = in_freq;

    if (duty > LCD_BLK_PWM_DUTY_MAX) {
        duty = LCD_BLK_PWM_DUTY_MAX;
    } else if (duty <= LCD_BLK_PWM_DUTY_MIN) {
        duty = LCD_BLK_PWM_DUTY_MIN;
    }
    if (freq < LCD_BLK_PWM_FREQ_DEF) {
        freq = LCD_BLK_PWM_FREQ_DEF;
    }

    uint32_t clk_freq = uapi_pwm_get_frequency((uint8_t)port);             // 40000000= 40MHz
    uint32_t div_num = (uint32_t)(clk_freq/freq);    // div_num 个系统时钟周期为一个需要的频率的时钟周期
    if (div_num < LCD_BLK_PWM_MAX_DIV_NUM_MAX) {
        log_error("%s div_num[%d] < MAX[8]\r\n", TAG, div_num);
        return ERRCODE_FAIL;
    }
    uint32_t high_time = div_num * duty / LCD_BLK_PWM_DUTY_MAX;            // 计算高电平时钟个数（向下取整）
    if (high_time * LCD_BLK_PWM_DUTY_MAX / div_num >=
       (uint32_t)(duty - duty * LCD_BLK_PWM_DUTY_STEP / LCD_BLK_PWM_DUTY_MAX)) {  // OK
    } else if ((high_time + 1) * LCD_BLK_PWM_DUTY_MAX / div_num <=
                (uint32_t)(duty + duty * LCD_BLK_PWM_DUTY_STEP / LCD_BLK_PWM_DUTY_MAX)) {
        high_time++;
    } else {
        log_error("%s high_time[%d] error\r\n", TAG, high_time);
        return ERRCODE_FAIL;
    }
    uint32_t low_time = div_num - high_time;
    pwm_config_t cfg = {
        .low_time = low_time,
        .high_time = high_time,  // 高电平持续tick 时间 = tick * (1/32000000)
        .offset_time = 0,        // 相位偏移位
        .cycles = 0,             // 发多少个波形
        .repeat = true           // 是否循环
    };

    log_debug("disp_set_backlight: clk_freq[%d] freq[%d] duty[%d], div_num[%d]=high_time[%d]+low_time[%d]\r\n",
               clk_freq, freq, duty, div_num, high_time, low_time);

    uapi_pwm_close(port);  // close PWM port before open it
    if (uapi_pwm_open((uint8_t)port, &cfg) != ERRCODE_SUCC) {
        log_error("%s uapi_pwm_open port[%d] error\r\n", TAG, port);
        return ERRCODE_FAIL;
    }

    uint8_t group_id = 0;
    switch (port) {
        case 0:
        case 1:
            group_id = 0;
            break;
        case 2:
        case 3:
            group_id = 1;
            break;
        case 4:
        case 5:
            group_id = 2;
            break;
        case 6:
        case 7:
            group_id = 3;
            break;
        default:
            break;
    }

    uapi_pwm_clear_group(group_id);
    if (uapi_pwm_set_group(group_id, &port, 1) != ERRCODE_SUCC) {  // 每个通道对应一个bit位
        log_error("%s uapi_pwm_set_group group_id[%d] error\r\n", TAG, group_id);
        return ERRCODE_FAIL;
    }

    if (uapi_pwm_start((uint8_t)port) != ERRCODE_SUCC) {
        log_error("%s uapi_pwm_start port[%d] error\r\n", TAG, port);
        return ERRCODE_FAIL;
    }

    return ERRCODE_SUCC;
}


errcode_t disp_set_backlight_level(uint8_t level)  // level[0, 100]
{
    return disp_set_backlight(LCD_BLK_PWM_PORT, level, LCD_BLK_PWM_FREQ_DEF);
}
/*
errcode_t disp_set_backlight_default(void)
{
    return disp_set_backlight(LCD_BLK_PWM_PORT, LCD_BLK_PWM_DUTY_DEF, LCD_BLK_PWM_FREQ_DEF);
}

errcode_t disp_set_backlight_off(void)
{
    return disp_set_backlight(LCD_BLK_PWM_PORT, 0, LCD_BLK_PWM_FREQ_DEF);
}
*/
