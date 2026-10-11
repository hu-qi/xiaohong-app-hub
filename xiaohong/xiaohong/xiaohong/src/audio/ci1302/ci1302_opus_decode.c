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

#include "ci1302_opus_decode.h"

#if CI1302_TTS_DECODE_OPUS_TO_PCM

#include "opus.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "log.h"

/*
 * opus_decoder_create 内部会 opus_alloc；在堆很小的 LiteOS 上易返回 OPUS_ALLOC_FAIL(-7)。
 * 使用静态缓冲 + opus_decoder_init，避免解码器状态占用堆。
 * 单声道解码器约 14–18KB；勿盲目用过大 BSS，否则会挤占堆导致 WiFi 等任务 LOS_TaskCreate 失败。
 */
#if defined(__GNUC__) || defined(__clang__)
#define CI1302_OPUS_DEC_ALIGN __attribute__((aligned(16)))
#else
#define CI1302_OPUS_DEC_ALIGN
#endif

/* 20KiB：覆盖典型 mono 的 opus_decoder_get_size(1)；若日志 need>buf 再略增 */
#define CI1302_OPUS_DEC_STORAGE_BYTES (20 * 1024)

static unsigned char s_dec_storage[CI1302_OPUS_DEC_STORAGE_BYTES] CI1302_OPUS_DEC_ALIGN;
static OpusDecoder *s_dec;
static int s_sr;
static int s_ch;

static void ensure_decoder(int sample_rate_hz, int channels)
{
    if (s_dec != NULL && s_sr == sample_rate_hz && s_ch == channels) {
        return;
    }
    int need = opus_decoder_get_size(channels);
    if (need < 0 || need > (int)sizeof(s_dec_storage)) {
        log_error("[ci1302_opus] opus_decoder_get_size(%d)=%d, buf=%u\r\n",
            channels, need, (unsigned)sizeof(s_dec_storage));
        s_dec = NULL;
        s_sr = 0;
        s_ch = 0;
        return;
    }
    OpusDecoder *st = (OpusDecoder *)(void *)s_dec_storage;
    int err = opus_decoder_init(st, (opus_int32)sample_rate_hz, channels);
    if (err != OPUS_OK) {
        log_error("[ci1302_opus] opus_decoder_init(%d, %d) failed err=%d\r\n",
            sample_rate_hz, channels, err);
        s_dec = NULL;
        s_sr = 0;
        s_ch = 0;
        return;
    }
    s_dec = st;
    s_sr = sample_rate_hz;
    s_ch = channels;
}

void ci1302_opus_decode_reset(void)
{
    if (s_dec != NULL) {
        (void)memset(s_dec_storage, 0, sizeof(s_dec_storage));
    }
    s_dec = NULL;
    s_sr = 0;
    s_ch = 0;
}

int ci1302_opus_decode_pcm(int sample_rate_hz, int channels, const uint8_t *opus, int opus_len,
    int16_t *pcm_out, int max_samples_per_ch)
{
    if (opus == NULL || opus_len <= 0 || pcm_out == NULL || max_samples_per_ch <= 0 ||
        channels <= 0 || sample_rate_hz <= 0) {
        return -1;
    }
    ensure_decoder(sample_rate_hz, channels);
    if (s_dec == NULL) {
        return -1;
    }
    opus_int32 n = opus_decode(s_dec, opus, (opus_int32)opus_len, pcm_out, max_samples_per_ch, 0);
    if (n < 0) {
        log_error("[ci1302_opus] opus_decode failed ret=%d len=%d\r\n", (int)n, opus_len);
        return (int)n;
    }
    return (int)n;
}

#endif /* CI1302_TTS_DECODE_OPUS_TO_PCM */
