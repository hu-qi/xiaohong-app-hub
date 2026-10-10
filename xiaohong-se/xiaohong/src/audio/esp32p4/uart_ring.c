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

#include "uart_ring.h"

#define N  (UART_RING_SIZE)  // size 4K

static uint8_t s_buf[N];      // size 4K
static volatile uint32_t s_w; // write index
static volatile uint32_t s_r; // read index

void uart_ring_init(void)
{
    s_w = 0;
    s_r = 0;
}

void uart_ring_push(const uint8_t *data, uint32_t len)
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

bool uart_ring_pop_byte(uint8_t *out)
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

uint32_t uart_ring_used(void)
{
    uint32_t w = s_w;
    uint32_t r = s_r;
    return (w + N - r) & (N - 1U);  // == (w + N - r) % N;
}
