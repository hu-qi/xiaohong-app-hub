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

#include <stdint.h>
#include <string.h>
#include "soc_osal.h"
#include "cmsis_os2.h"
#include "errcode.h"
#include "board_config.h"
#include "task_entry.h"
#include "settings.h"
#include "log.h"
#include "uart_config.h"

#define LOG_TAG  "[63RxTask]"

#if UART_INT_MODE
void uart_read_rx_int_handler(const void *buffer, uint16_t length, bool error)
{
    unused(error);

    if (buffer == NULL || length == 0) {
        log_error("%s uart_read_rx_int_handler: illegal data, ret\r\n", LOG_TAG);
        return;
    }
    if ((uint32_t)length > UART_RING_SIZE) {
        log_error("%s intr recv len[%u]: too long\r\n", LOG_TAG, (unsigned)length);
        return;
    }
    uart_ring_push((const uint8_t *)buffer, (uint32_t)length);
}

errcode_t register_uart_rx_callback(void)
{
    return uapi_uart_register_rx_callback(UART_BUS_ID, 0x07, 1, uart_read_rx_int_handler);
}
#endif

void UartRxTask(void)
{
    uint32_t len = 0;
#if UART_INT_MODE
    uart_ring_init();
    register_uart_rx_callback();
#endif

    while (1) {
        len = rx_ring_buff_parse();
        if (len == 0) {
            usleep(10 * 1000);
        }
    }
}



uint32_t UartRxTaskEntry(void)
{
    task_config_t task_conf = {
        "63RxTask",        // task name
        1024 * 8,          // stack_size
        24,                // priority
        (void*)UartRxTask  // func
    };

    return create_task(&task_conf, NULL);
}