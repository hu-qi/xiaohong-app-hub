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

#ifndef __BOARD_CONFIG_H__
#define __BOARD_CONFIG_H__

#include <stdio.h>
#include <unistd.h>
#if SUPPORT_OHOS
#include "ohos_init.h"
#else
#include "app_init.h"
#endif
#include "cmsis_os2.h"
#include "soc_osal.h"
#include "errcode.h"

#include "pinctrl.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "adc.h"
#include "spi.h"
#include "spi_porting.h"
#include "dma.h"
#include "pwm.h"
#include "tcxo.h"
#include "uart.h"

#if defined(BOARD_XH_WS63_KB)
  #include "key_config.h"

  #define BAT_LEVEL_PIN                    (11)   // GPIO11:ADC[4]
  #define BAT_LEVEL_ADC_CHN                (4)    // GPIO11:ADC[4]

  #define KEY_MIC_PIN                      (7)    // Key MIC: GPIO
  #define KEY_ADC_PIN                      (9)    // Key ADC: ADC[2]: Yes/No/Up/Down/[NO/Off??]
  #define KEY_ADC_PIN_ADC_CHN              (2)    // Key ADC: ADC[2]
  #define KEY_ALWAYS_PIN                   (13)   // Key Always: GPIO
  #define KEY_LONG_PRESS_INTERVAL          (500)  // long press key time interval in ms
  #define KEY_LONG_PRESS_INTERVAL_5S       (5000) // long press key time interval in 5s

  #define BUZZER_PIN                       (8)    // Buzzer GPIO8/PWM0
  #define BUZZER_PWM                       (0)    // Buzzer GPIO8/PWM0 4KHz
  #define BUZZER_FREQ                      (4000) // Buzzer PWM 频率

  #define LED_DI_PIN                       (0)    // LED DI GPIO/WS2812C
  #define LED_NUM                          (3)    // 3xLED WS2812C

  #define LCD_BLK_PIN                      (2)    // LCD BackLight ctrl pin GPIO2/PWM2 20KHz
  #define LCD_RST_PIN                      (3)    // LCD Reset ctrl pin GPIO3
  #define LCD_DC_PIN                       (4)    // LCD Cmd/Data Enable: [Lo-Cmd, Hi-Data]
  #define LCD_CS_PIN                       (5)    // LCD CS: [Lo-Select, Hi-unSelect]

  #define SPI_CLK_PIN                      (6)    // SPI-1-CLK
  #define SPI_MOSI_PIN                     (1)    // SPI-1-MOSI

  #define SPI_IDX                          (1)
  #define SPI_GPIO_PIN_MODE                (3)
  #define SPI_TRAN_MODE                    (0)    // 0/1/2/3-SPI transmission mode
  #define SPI_WORKING_FREQUENCY            (40)   // 40MHz


  #define UART1_RXD_PIN                    (16)   // I2C1_SCL
  #define UART1_TXD_PIN                    (15)   // I2C1_SDA
  #define UART1_GPIO_PIN_MODE              (2)
  #define I2C_IDX                          (1)

#endif

errcode_t board_hw_init(void);
errcode_t buzzer_beep(uint32_t beep_ms);   // 蜂鸣器短鸣 beep_ms 毫秒

#endif//__BOARD_CONFIG_H__