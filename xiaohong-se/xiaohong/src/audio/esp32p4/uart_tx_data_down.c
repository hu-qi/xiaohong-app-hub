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
#include "agent_state.h"
#include "log.h"
#include "uart_config.h"
#include "uart_tx_data_down.h"

#define LOG_TAG  "[TxDwData]"

////////////////////////////////////////////////////////////////////////////////////////
// Text Data
static tts_slot_t g_tts_text_slots[TTS_SLOT_NUM];
static uint8_t g_text_wp;
static uint8_t g_text_rp;
static uint8_t g_text_count;
static osMutexId_t g_text_mu;
static uint64_t s_text_mu_cb[20];
static const osMutexAttr_t s_text_mu_attr = {
    .name = "txt_dl_mu",
    .attr_bits = 0U,
    .cb_mem = s_text_mu_cb,
    .cb_size = sizeof(s_text_mu_cb),
};

static void tts_text_ensure_mutex(void)
{
    if (g_text_mu == NULL) {
        g_text_mu = osMutexNew(&s_text_mu_attr);
        if (g_text_mu == NULL) {
            log_error("%s tts_text_ensure_mutex: osMutexNew failed\r\n", LOG_TAG);
        }
    }
}

void set_pending_text_utf8(const uint8_t type, const char *utf8)
{
    if (type >= eTextType_MAX || utf8 == NULL) {
        log_error("%s set_pending_text_utf8: invalid type[%u] or utf8 NULL\r\n", LOG_TAG, (unsigned)type);
        return;
    }

    size_t n = strlen(utf8);
    if (n == 0U) {
        return;
    }
    if (n > TTS_CHUNK_MAX_BYTES) {
        log_error("%s set_pending_text_utf8: text too long (%u), truncate to %u\r\n",
                  LOG_TAG, (unsigned)n, (unsigned)TTS_CHUNK_MAX_BYTES);
        n = TTS_CHUNK_MAX_BYTES;
    }

    tts_text_ensure_mutex();
    if (g_text_mu == NULL) {
        return;
    }
    (void)osMutexAcquire(g_text_mu, osWaitForever);

    if (g_text_count >= TTS_SLOT_NUM) {
        log_error("%s set_pending_text_utf8: queue full, drop oldest\r\n", LOG_TAG);
        g_text_rp = (uint8_t)((g_text_rp + 1U) % TTS_SLOT_NUM);
        g_text_count--;
    }

    tts_slot_t *slot = &g_tts_text_slots[g_text_wp];
    slot->type = type;
    slot->len = (uint16_t)n;
    memcpy(slot->data, utf8, n);

    g_text_wp = (uint8_t)((g_text_wp + 1U) % TTS_SLOT_NUM);
    g_text_count++;

    const uint8_t queued = g_text_count;
    (void)osMutexRelease(g_text_mu);

//  log_debug("%s set_pending_text_utf8: type[%u] len[%u] queued[%u]\r\n",
//            LOG_TAG, (unsigned)type, (unsigned)n, (unsigned)queued);
}

bool uart_tx_pop_pending_text(uint8_t *out, uint16_t cap, uint16_t *out_len)
{
    if (out == NULL || out_len == NULL) {
        return false;
    }
    *out_len = 0;

    tts_text_ensure_mutex();
    if (g_text_mu == NULL) {
        return false;
    }
    (void)osMutexAcquire(g_text_mu, osWaitForever);

    if (g_text_count == 0U) {
        (void)osMutexRelease(g_text_mu);
        return false;
    }

    const tts_slot_t *slot = &g_tts_text_slots[g_text_rp];
    const uint16_t plen = (uint16_t)(UART_TX_TEXT_HDR_BYTES + slot->len);
    if (cap < plen) {
        log_error("%s uart_tx_pop_pending_text: cap[%u] < need[%u]\r\n",
                  LOG_TAG, (unsigned)cap, (unsigned)plen);
        (void)osMutexRelease(g_text_mu);
        return false;
    }

    out[0] = slot->type;
    out[1] = (uint8_t)(slot->len & 0xFFU);
    out[2] = (uint8_t)((slot->len >> 8) & 0xFFU);
    memcpy(out + UART_TX_TEXT_HDR_BYTES, slot->data, slot->len);
    *out_len = plen;

    g_text_rp = (uint8_t)((g_text_rp + 1U) % TTS_SLOT_NUM);
    g_text_count--;

    (void)osMutexRelease(g_text_mu);
    return true;
}
// Text Data
////////////////////////////////////////////////////////////////////////////////////////
// Audio Data
static audio_slot_t g_aud_opus_slots[AUD_SLOT_NUM];
static uint8_t g_aud_wp;
static uint8_t g_aud_rp;
static uint8_t g_aud_count;
static osMutexId_t g_aud_mu;
static uint64_t s_aud_mu_cb[20];
static const osMutexAttr_t s_aud_mu_attr = {
    .name = "aud_dl_mu",
    .attr_bits = 0U,
    .cb_mem = s_aud_mu_cb,
    .cb_size = sizeof(s_aud_mu_cb),
};

static void aud_opus_ensure_mutex(void)
{
    if (g_aud_mu == NULL) {
        g_aud_mu = osMutexNew(&s_aud_mu_attr);
        if (g_aud_mu == NULL) {
            log_error("%s aud_opus_ensure_mutex: osMutexNew failed\r\n", LOG_TAG);
        }
    }
}

void set_pending_opus_data(const uint8_t *opus_data, const uint32_t opus_data_len)
{
    if (opus_data == NULL || opus_data_len == 0U) {
        log_error("%s set_pending_opus_data: invalid opus_data[%p] len[%u]\r\n",
                  LOG_TAG, (const void *)opus_data, (unsigned)opus_data_len);
        return;
    }

    uint32_t n = opus_data_len;
    if (n > AUD_CHUNK_MAX_BYTES) {
        log_error("%s set_pending_opus_data: frame too long (%u), truncate to %u\r\n",
                  LOG_TAG, (unsigned)n, (unsigned)AUD_CHUNK_MAX_BYTES);
        n = AUD_CHUNK_MAX_BYTES;
    }

    aud_opus_ensure_mutex();
    if (g_aud_mu == NULL) {
        return;
    }
    (void)osMutexAcquire(g_aud_mu, osWaitForever);

    if (g_aud_count >= AUD_SLOT_NUM) {
        log_error("%s set_pending_opus_data: queue full, drop oldest\r\n", LOG_TAG);
        g_aud_rp = (uint8_t)((g_aud_rp + 1U) % AUD_SLOT_NUM);
        g_aud_count--;
    }

    audio_slot_t *slot = &g_aud_opus_slots[g_aud_wp];
    slot->len = (uint16_t)n;
    memcpy(slot->data, opus_data, n);

    g_aud_wp = (uint8_t)((g_aud_wp + 1U) % AUD_SLOT_NUM);
    g_aud_count++;

    const uint8_t queued = g_aud_count;
    (void)osMutexRelease(g_aud_mu);

//  log_debug("%s set_pending_opus_data: len[%3u]QueueId[%u]\r\n", LOG_TAG, (unsigned)n, (unsigned)queued);
}

bool uart_tx_pop_pending_opus(uint8_t *out, uint16_t cap, uint16_t *out_len)
{
    if (out == NULL || out_len == NULL) {
        return false;
    }
    *out_len = 0;

    aud_opus_ensure_mutex();
    if (g_aud_mu == NULL) {
        return false;
    }
    (void)osMutexAcquire(g_aud_mu, osWaitForever);

    if (g_aud_count == 0U) {
        (void)osMutexRelease(g_aud_mu);
        return false;
    }

    const audio_slot_t *slot = &g_aud_opus_slots[g_aud_rp];
    if (cap < slot->len) {
        log_error("%s uart_tx_pop_pending_opus: cap[%u] < need[%u]\r\n",
                  LOG_TAG, (unsigned)cap, (unsigned)slot->len);
        (void)osMutexRelease(g_aud_mu);
        return false;
    }

    memcpy(out, slot->data, slot->len);
    *out_len = slot->len;

    g_aud_rp = (uint8_t)((g_aud_rp + 1U) % AUD_SLOT_NUM);
    g_aud_count--;

    (void)osMutexRelease(g_aud_mu);
    return true;
}
// Audio Data
////////////////////////////////////////////////////////////////////////////////////////
// Bin Data

// Bin Data
////////////////////////////////////////////////////////////////////////////////////////
