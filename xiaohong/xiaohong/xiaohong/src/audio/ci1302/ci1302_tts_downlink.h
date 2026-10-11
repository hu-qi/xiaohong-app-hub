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

#ifndef CI1302_TTS_DOWNLINK_H
#define CI1302_TTS_DOWNLINK_H

#include <stdint.h>
#include <stdbool.h>

/**
 * 单槽合并块 / 单次 0x020B 载荷上限（字节）；须与 CI1302 协议及 UART 对端缓冲一致。
 * 整帧 UART = 16 字节头 + 本长度。
 */
#ifndef CI1302_TTS_CHUNK_MAX_BYTES
#define CI1302_TTS_CHUNK_MAX_BYTES 4096
#endif

/** 云端固定 16 kHz Opus；解码后 16 kHz PCM。0x020B 载荷为 PCM 原始字节流（len 为偶数，即 int16 样本数×2）。 */
#ifndef CI1302_020B_PCM_SAMPLE_RATE_HZ
#define CI1302_020B_PCM_SAMPLE_RATE_HZ 16000
#endif

/**
 * 入队待 0x020B 下发：`data` 为 uint8_t 数组指针，与解码缓冲区字节视图一致，无需另做端序转换（小端 MCU）。
 * CI1302 发 0x020A → AudPlayTask 经 0x020B 下发同一段字节流。
 */
bool ci1302_tts_push(const uint8_t *data, uint32_t len);
/** 将合并缓冲中不足一整块（CI1302_TTS_CHUNK_MAX_BYTES）的尾段推入槽（如服务端 TTS stop 后已无新二进制） */
void ci1302_tts_flush_pending(void);
/**
 * 从队列/当前块中取出下一段待发送数据（长度不超过 max_out），供单次 0x020B 发送。
 * 同一逻辑块可能分多次调用才发完。
 * @param out_all_drained 可选；为 true 表示本段发出后下行缓存已空（应再发 eAud_EndPlay / 0x020C）。
 */
bool ci1302_tts_take_next_tx_block(uint8_t **out, uint16_t *out_len, uint16_t max_out,
                                  bool *out_all_drained);
/** 无待发送数据（槽空且当前块已发完） */
bool ci1302_tts_is_empty(void);
void ci1302_tts_reset(void);

/**
 * 收到服务端 tts/stop 且队列里仍有 PCM 时置位；仅在随后某次拉流「真正发完」
 *（ci1302_tts_take_next_tx_block 的 all_drained）时由 AudPlayTask 发 eAud_EndPlay。
 * 避免流式播放过程中两包之间队列短暂空窗误发 0x020C，导致只播半句。
 */
void ci1302_tts_set_pending_endplay_after_drain(bool pending);
bool ci1302_tts_consume_endplay_after_drain(bool all_drained);

#endif
