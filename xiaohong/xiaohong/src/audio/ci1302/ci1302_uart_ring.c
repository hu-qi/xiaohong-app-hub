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

#include "ci1302_uart_ring.h"

#define N  (CI1302_UART_RING_SIZE)  // size 4K

// 环形缓冲区，用于MCU Rx在有中断回调时缓存CI1302向MCU发送的数据帧，含帧头、载荷(命令、状态、音频数据)、校验和、帧尾等数据
// 等待AudioListenTask从中解析载荷，其中的音频数据上传给服务器做ASR识别，其它则是本地处理的命令或状态信息
static uint8_t s_buf[N];      // size 4K
static volatile uint32_t s_w; // write index
static volatile uint32_t s_r; // read index

void ci1302_uart_ring_init(void)
{
    s_w = 0;
    s_r = 0;
}

void ci1302_uart_ring_push(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0) {
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        uint32_t w = s_w;
        uint32_t next_w = (w + 1U) & (N - 1U);  // == (w + 1U) % N;
        if (next_w == s_r) {
            s_r = (s_r + 1U) & (N - 1U);  // == (s_r + 1U) % N;
        }
        s_buf[w] = data[i];
        s_w = next_w;
    }
}

bool ci1302_uart_ring_pop_byte(uint8_t *out)
{
    if (out == NULL) {
        return false;
    }
    uint32_t r = s_r;
    if (r == s_w) {
        return false;
    }
    *out = s_buf[r];
    s_r = (r + 1U) & (N - 1U);  // == (r + 1U) % N;
    return true;
}

uint32_t ci1302_uart_ring_used(void)
{
    uint32_t w = s_w;
    uint32_t r = s_r;
    return (w + N - r) & (N - 1U);  // == (w + N - r) % N;
}
