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

#ifndef __UART_RING_H__
#define __UART_RING_H__

#include <stdint.h>
#include <stdbool.h>
#include "soc_osal.h"
#include "cmsis_os2.h"


#define UART_RING_SIZE (4 * 1024)

void uart_ring_init(void);

/** ISR 中调用：写入一批 RX 字节；满时丢弃最旧字节 */
void uart_ring_push(const uint8_t *data, uint32_t len);

/** 任务中调用：弹出一字节，空则返回 false */
bool uart_ring_pop_byte(uint8_t *out);

uint32_t uart_ring_used(void);

#endif
