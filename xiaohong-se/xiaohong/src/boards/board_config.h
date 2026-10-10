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

#if defined(BOARD_XH_WS63_V2)
  #define UART1_RXD_PIN                    (16)   // I2C1_SCL
  #define UART1_TXD_PIN                    (15)   // I2C1_SDA
  #define UART1_GPIO_PIN_MODE              (1)

  #define UART2_RXD_PIN                    (7)
  #define UART2_TXD_PIN                    (8)
  #define UART2_GPIO_PIN_MODE              (2)

  #define UART_BUS_ID                      (1)
  #define UART_INT_MODE                    (1)
  #define UART_BAUDRATE                    (921600)  //(921600) //
  #define UART_DATA_BITS                   (8)
  #define UART_STOP_BITS                   (1)
  #define UART_PARITY_BIT                  (0)
  #define UART_RX_BUFF_SIZE                (16+4096)  // Rx Buffer Size
#endif

errcode_t board_hw_init(void);

#endif//__BOARD_CONFIG_H__