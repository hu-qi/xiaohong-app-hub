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

#include "board_config.h"

static errcode_t gpio_init(void)
{
    // UART1 管脚配置
    uapi_pin_set_mode(UART1_TXD_PIN, UART1_GPIO_PIN_MODE);
    uapi_pin_set_mode(UART1_RXD_PIN, UART1_GPIO_PIN_MODE);

    return ERRCODE_SUCC;
}

static uint8_t g_uart_rx_buff[UART_RX_BUFF_SIZE] = {0};
static uart_buffer_config_t g_uart_buffer_config = {
    .rx_buffer = g_uart_rx_buff,
    .rx_buffer_size = UART_RX_BUFF_SIZE
};
static errcode_t uart_init(void)
{
    static uint8_t uart1_inited = 0;
    if (uart1_inited) {
        printf("[BoardCfg] uart1_init: uart1_inited[1]\r\n");
        return ERRCODE_SUCC;
    }

    uart_attr_t attr = {
        .baud_rate = UART_BAUDRATE,    // 115200
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = UART1_TXD_PIN,
        .rx_pin = UART1_RXD_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    errcode_t ret = uapi_uart_init(UART_BUS_ID, &pin_config, &attr, NULL, &g_uart_buffer_config);
    if (ret == ERRCODE_SUCC) {
        printf("[BoardCfg] uart1_init: uart1_inited[0->1] [%d]\r\n", UART_BAUDRATE);
        uart1_inited = 1;
    } else {
        printf("[BoardCfg] uart1_init: uart1_inited[0]\r\n");
    }
    return ret;
}

errcode_t board_hw_init(void)
{
    errcode_t ret = ERRCODE_SUCC;
    static uint8_t hw_inited = 0;
    if (hw_inited) {
        printf("[BoardCfg] board_hw_init: hw_inited[1]\r\n");
        return ERRCODE_SUCC;
    }

    ret |= gpio_init();
    ret |= uart_init();

    if (ret == ERRCODE_SUCC) {
        hw_inited = 1;
        printf("[BoardCfg] board_hw_init: hw_inited[0->1]\r\n");
    }

    return ret;
}
