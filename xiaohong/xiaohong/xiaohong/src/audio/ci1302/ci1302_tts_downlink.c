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

#include "ci1302_tts_downlink.h"
#include "ci1302_opus_decode.h"
#include <string.h>
#include "cmsis_os2.h"
#include "log.h"

/*
 * 静态占用 ≈ (TTS_SLOT_NUM * (TTS_CHUNK_MAX + 2)) + 3 * TTS_CHUNK_MAX。
 * TTS_CHUNK_MAX 与 ci1302_tts_downlink.h 中 CI1302_TTS_CHUNK_MAX_BYTES 一致。
 */
#define TTS_CHUNK_MAX     CI1302_TTS_CHUNK_MAX_BYTES    // 4K
#if SUPPORT_OHOS
//#define TTS_SLOT_NUM    4    // OH6.1轻量系统
#define TTS_SLOT_NUM      8    // OH7.0轻量系统小型化优化提升
#else
#define TTS_SLOT_NUM      12
#endif

#if !defined(SUPPORT_LVGL)
#undef  TTS_SLOT_NUM
#define TTS_SLOT_NUM      24   // 没有显示屏(没有lvgl任务及显示缓存), 增大TTS缓存
#endif

typedef struct {
    uint16_t len;
    uint8_t data[TTS_CHUNK_MAX];
} tts_slot_t;

static tts_slot_t g_slots[TTS_SLOT_NUM];
static uint8_t g_wp;
static uint8_t g_rp;
static uint8_t g_count;
static osMutexId_t g_mu;
/* 避免运行时堆分配失败：为互斥锁提供静态控制块内存。 */
static uint64_t s_tts_mu_cb[20];
static const osMutexAttr_t s_tts_mu_attr = {
    .name = "tts_dl_mu",
    .attr_bits = 0U,
    .cb_mem = s_tts_mu_cb,
    .cb_size = sizeof(s_tts_mu_cb),
};

/* 将 WS 侧任意长度二进制流合并为 TTS_CHUNK_MAX 字节再入槽；不足一整槽的暂存于此 */
static uint8_t s_asm[TTS_CHUNK_MAX];  // 4096
static uint16_t s_asm_len;

/* 当前正在按 CI1302 的 0x020A 请求节奏发送的块（PCM 字节；可拆成多帧 0x020B，按 max_out 切片） */
uint8_t s_tx_cur[TTS_CHUNK_MAX];  // 4096
static uint16_t s_tx_len;
static uint16_t s_tx_off;

/** 仅当 tts/stop 后仍需把队列里 PCM 发完时再置 true；reset 时清 */
static bool s_pending_endplay_after_drain;

static void tts_ensure_mutex(void)
{
    if (g_mu == NULL) {
        g_mu = osMutexNew(&s_tts_mu_attr);
        if (g_mu == NULL) {
            log_error("[DownLink] tts_ensure_mutex: osMutexNew failed\r\n");
        }
    }
}

/** 必须在已持锁下调用 */
static bool tts_enqueue_slot_locked(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U || len > TTS_CHUNK_MAX) {
        return false;
    }
    if (g_count >= TTS_SLOT_NUM) {
        g_rp = (uint8_t)((g_rp + 1U) % TTS_SLOT_NUM);
        g_count--;
    }
    memcpy(g_slots[g_wp].data, data, len);
    g_slots[g_wp].len = len;
    g_wp = (uint8_t)((g_wp + 1U) % TTS_SLOT_NUM);
    g_count++;
    return true;
}

bool ci1302_tts_push(const uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0U) {
        return true;
    }
    tts_ensure_mutex();
    if (g_mu == NULL) {
        return false;
    }
    (void)osMutexAcquire(g_mu, osWaitForever);

    uint32_t off = 0U;
    while (off < len) {
        uint32_t room = (uint32_t)(TTS_CHUNK_MAX - s_asm_len);
        uint32_t take = len - off;
        if (take > room) {
            take = room;
        }
        memcpy(s_asm + s_asm_len, data + off, take);
        s_asm_len = (uint16_t)(s_asm_len + take);
        off += take;
        if (s_asm_len == TTS_CHUNK_MAX) {
            if (!tts_enqueue_slot_locked(s_asm, TTS_CHUNK_MAX)) {
                (void)osMutexRelease(g_mu);
                return false;
            }
            s_asm_len = 0;
        }
    }

    (void)osMutexRelease(g_mu);
    return true;
}

void ci1302_tts_flush_pending(void)
{
    tts_ensure_mutex();
    if (g_mu == NULL) {
        return;
    }
    (void)osMutexAcquire(g_mu, osWaitForever);
    if (s_asm_len > 0U) {
        (void)tts_enqueue_slot_locked(s_asm, s_asm_len);
        s_asm_len = 0;
    }
    (void)osMutexRelease(g_mu);
}

bool ci1302_tts_take_next_tx_block(uint8_t **out, uint16_t *out_len, uint16_t max_out, bool *out_all_drained)
{
    if (/*out == NULL ||*/ out_len == NULL || max_out == 0U) {
        log_error("[DownLink] ci1302_tts_take_next_tx_block: out[%p], out_len[%p], max_out[%d] is NULL\r\n", out, out_len, max_out);
        return false;
    }
    if (out_all_drained != NULL) {
        *out_all_drained = false;
    }
    tts_ensure_mutex();
    if (g_mu == NULL) {
        log_error("[DownLink] ci1302_tts_take_next_tx_block: g_mu is NULL\r\n");
        return false;
    }
    (void)osMutexAcquire(g_mu, osWaitForever);

    if (s_tx_off >= s_tx_len) {
        s_tx_off = 0;
        s_tx_len = 0;
        if (g_count > 0U) {
            tts_slot_t *s = &g_slots[g_rp];
            memcpy(s_tx_cur, s->data, s->len);
            s_tx_len = s->len;
            g_rp = (uint8_t)((g_rp + 1U) % TTS_SLOT_NUM);
            g_count--;
        } else if (s_asm_len > 0U) {
            /*
             * 槽已空，但合并缓冲里还有不足一整槽（CI1302_TTS_CHUNK_MAX_BYTES）的尾段：必须立即下发。
             * 旧逻辑此处 return false 等「攒满一整槽」，会让 CI1302 连续多次 0x020A 都拉不到数据
             *（日志里连续多行 Partial Data Block），表现为 TTS 播放卡顿/截断。
             * 0x020B 的载荷长度本就按实际字节下发，短包合法。
             */
            memcpy(s_tx_cur, s_asm, s_asm_len);
            s_tx_len = s_asm_len;
            s_asm_len = 0;
        } else {
            /* 槽与合并缓冲皆空：无可发送，视为 all_drained */
            if (out_all_drained != NULL) {
                *out_all_drained = true;
            }
            (void)osMutexRelease(g_mu);
            return false;
        }
    }
    {
        uint16_t remain = (uint16_t)(s_tx_len - s_tx_off);
        uint16_t n = remain > max_out ? max_out : remain;

        *out = s_tx_cur + s_tx_off;

        s_tx_off = (uint16_t)(s_tx_off + n);
        *out_len = n;
        if (out_all_drained != NULL) {
            *out_all_drained = (g_count == 0U) && (s_tx_off >= s_tx_len) && (s_asm_len == 0U);
        }
    }
    /*
     * 不在此打印 out_len/s_asm_len：本函数每次 0x020A 都会被调用，且此处仍持 g_mu，
     * printf 会把解码线程（mg_poll 的 ci1302_tts_push）堵在同一把锁上，直接放大播放卡顿。
     */
    (void)osMutexRelease(g_mu);
    return true;
}

bool ci1302_tts_is_empty(void)
{
    tts_ensure_mutex();
    if (g_mu == NULL) {
        return true;
    }
    (void)osMutexAcquire(g_mu, osWaitForever);
    bool empty = (g_count == 0U) && ((s_tx_len == 0U) || (s_tx_off >= s_tx_len)) && (s_asm_len == 0U);
    (void)osMutexRelease(g_mu);
    return empty;
}

void ci1302_tts_set_pending_endplay_after_drain(bool pending)
{
    s_pending_endplay_after_drain = pending;
}

bool ci1302_tts_consume_endplay_after_drain(bool all_drained)
{
    if (!all_drained || !s_pending_endplay_after_drain) {
        return false;
    }
    s_pending_endplay_after_drain = false;
    return true;
}

void ci1302_tts_reset(void)
{
    tts_ensure_mutex();
    if (g_mu == NULL) {
        return;
    }
    (void)osMutexAcquire(g_mu, osWaitForever);
    g_wp = 0;
    g_rp = 0;
    g_count = 0;
    s_tx_len = 0;
    s_tx_off = 0;
    s_asm_len = 0;
    (void)memset(s_asm, 0, sizeof(s_asm));
    s_pending_endplay_after_drain = false;
    (void)osMutexRelease(g_mu);
#if CI1302_TTS_DECODE_OPUS_TO_PCM
    ci1302_opus_decode_reset();
#endif
}
