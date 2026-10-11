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

#ifndef CI1302_OPUS_DECODE_H
#define CI1302_OPUS_DECODE_H

#include <stdint.h>

/*
 * TTS 下行：WS63 解码 Opus→PCM 后经 ci1302_tts 队列，在 CI1302 0x020A 节奏下用 0x020B 发 PCM；
 * 不把服务器 Opus 裸流发给 CI1302。置 1 并链接 libopus；置 0 时 on_audio_data 不下发音频（需改 BUILD.gn）。
 */
#ifndef CI1302_TTS_DECODE_OPUS_TO_PCM
#define CI1302_TTS_DECODE_OPUS_TO_PCM 1
#endif

#if CI1302_TTS_DECODE_OPUS_TO_PCM

void ci1302_opus_decode_reset(void);

int ci1302_opus_decode_pcm(int sample_rate_hz, int channels, const uint8_t *opus, int opus_len,
    int16_t *pcm_out, int max_samples_per_ch);

#endif

#endif
