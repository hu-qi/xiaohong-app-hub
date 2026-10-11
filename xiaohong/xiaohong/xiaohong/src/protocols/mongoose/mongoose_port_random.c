/**
 * @file mongoose_port_random.c
 *
 * 替代 mongoose 内置 mg_random（避免 rand() 非线程安全）。
 * - 不使用 __atomic_*（riscv32-musl 常缺 libatomic）
 * - 不使用 osKernelLock（部分环境下会触发 Illegal instruction / 不可用扩展）
 *
 * 用 volatile + LCG 填充；多线程下可能有字撕裂，仅用于 WS mask/nonce 足够。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mongoose_config.h"
#include "mongoose.h"

bool mg_random(void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    static volatile uint32_t state = 0xC0FFEE42u;

    if (p == NULL || len == 0) {
        return false;
    }

    uint32_t t = (uint32_t)mg_millis();
    for (size_t i = 0; i < len; i++) {
        uint32_t s = state;
        s = s * 1664525u + 1013904223u;
        s ^= t ^ (uint32_t)((unsigned)i * 0x9E37u);
        state = s;
        p[i] = (unsigned char)(s >> 16);
    }
    return true;
}
