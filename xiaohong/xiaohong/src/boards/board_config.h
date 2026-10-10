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

#if defined(BOARD_XH_WS63_V1)
  #include "key_config.h"

  #define BAT_LEVEL_PIN                    (10)   // GPIO10:ADC[3]
  #define BAT_LEVEL_ADC_CHN                (3)    // GPIO10:ADC[3]

  #define KEY_VOLUP_PIN                    (13)   // Volume Up key
  #define KEY_VOLDW_PIN                    (12)   // Volume Down key
  #define KEY_WAKEUP_PIN                   (11)   // Function key
  #define KEY_LONG_PRESS_INTERVAL          (500)  // long press key time interval in ms
  #define KEY_LONG_PRESS_INTERVAL_5S       (5000) // long press key time interval in 5s

  #define FLASH_CS_PIN                     (0)    // NorFlash CS: [Lo-Select, Hi-unSelect]
  #define LCD_RST_PIN                      (9)    // LCD RES/RESET: [Lo-Reset, Hi-Normal]
  #define LCD_DC_PIN                       (4)    // LCD Cmd/Data Enable: [Lo-Cmd, Hi-Data]
  #define LCD_CS_PIN                       (5)    // LCD CS: [Lo-Select, Hi-unSelect]
  #define LCD_BLK_PIN                      (2)    // LCD BackLight ctrl pin

  #define SPI_IDX                          (1)
  #define SPI_GPIO_PIN_MODE                (3)
  #define SPI_TRAN_MODE                    (0)    // 0/1/2/3-SPI transmission mode
  #define SPI_WORKING_FREQUENCY            (40)   // 40MHz

  #define SPI_CLK_PIN                      (6)    // SPI-1-CLK
  #define SPI_MOSI_PIN                     (1)    // SPI-1-MOSI
  #define SPI_MISO_PIN                     (3)    // SPI-1-MISO

  #define UART1_RXD_PIN                    (16)   // I2C1_SCL
  #define UART1_TXD_PIN                    (15)   // I2C1_SDA
  #define UART2_RXD_PIN                    (7)
  #define UART2_TXD_PIN                    (8)
  #define UART2_GPIO_PIN_MODE              (2)

  #define UART_BUS_ID                      (2)
  #define UART_INT_MODE                    (1)
  #define UART_BAUDRATE                    (921600) //(115200)
  #define UART_DATA_BITS                   (8)
  #define UART_STOP_BITS                   (1)
  #define UART_PARITY_BIT                  (0)
  #define UART_RX_BUFF_SIZE                (96)  // Rx Buffer Size
#endif

errcode_t board_hw_init(void);

typedef void (*key_event_callback_t)(uint8_t eKeyMsgId);
uint8_t register_key_event_callback(key_event_callback_t func_cb);

#define  SPI_CS_PIN_RACE_TIMEOUT    (50)  // 50ms timeout
errcode_t spi_cs_pin_race(uint8_t pin, uint8_t level);
errcode_t flash_set_cs_pin(uint8_t level);
errcode_t flash_write_read(uint8_t* write_buff, uint32_t write_len,
                           uint8_t* read_buff, uint32_t read_len);
errcode_t flash_read_id(uint8_t* recv_buff);
#define  FLASH_ID_RCMD_LEN          (5)
#define  FLASH_ID_LEN               (16)

#endif//__BOARD_CONFIG_H__