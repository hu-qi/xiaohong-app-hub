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

#ifndef __UART_TX_DATA_DOWN_H__
#define __UART_TX_DATA_DOWN_H__

#include <stdint.h>
#include <stdbool.h>
#include "soc_osal.h"
#include "cmsis_os2.h"

#define TTS_CHUNK_MAX_BYTES  1024  /* 1024 字节以下可一次下发，更长需分多帧 */
#define TTS_SLOT_NUM         8

#define AUD_CHUNK_MAX_BYTES  256  /* 服务器下发的opus压缩语音数据包，每一帧的长度都在100字节上下，见过的最大是155字节 */
#define AUD_SLOT_NUM         40   /* 40 * 256 = 10KB */

/* DOWN_TEXT(0x0E40) payload: type(1) + len_le(2) + utf8[len] */
#define UART_TX_TEXT_HDR_BYTES  3U
#define UART_TX_TEXT_PAYLOAD_MAX  (UART_TX_TEXT_HDR_BYTES + TTS_CHUNK_MAX_BYTES)

enum {
    eTextType_STT = 0,    /* 下行：Speech To Text, 语音识别结果 */
    eTextType_LLM = 1,    /* 下行：Large Language Model, 大模型侧的状态/表情指令 */
    eTextType_TTS = 2,    /* 下行：Text To Speech, 语音合成播放的字幕 */

    eTextType_UP_TX = 3,  /* 上行：P4发送文本到WS63, WS63再转发到Web */
                          /* 可扩展其他类型 */
    eTextType_MAX,
};

typedef struct {     /* 文本数据槽，用于缓存文本数据 */
    uint8_t type;    /* 文本类型: STT, LLM, TTS */
    uint16_t len;    /* data[len] 为有效数据，不包含 type 和 len 字段 */
    uint8_t data[TTS_CHUNK_MAX_BYTES];
} tts_slot_t;

void set_pending_text_utf8(uint8_t type, const char *utf8);
/** 从待发队列取一条文本，写入 out；成功返回 true 且 *out_len 为 payload 字节数 */
bool uart_tx_pop_pending_text(uint8_t *out, uint16_t cap, uint16_t *out_len);


typedef struct {     /* 音频数据槽，用于缓存 Opus 帧 */
    uint16_t len;    /* data[len] 为有效的 Opus 帧字节数 */
    uint8_t data[AUD_CHUNK_MAX_BYTES];
} audio_slot_t;

void set_pending_opus_data(const uint8_t *opus_data, const uint32_t opus_data_len);
/** 从待发队列取一帧 Opus，写入 out；成功返回 true 且 *out_len 为 payload 字节数 */
bool uart_tx_pop_pending_opus(uint8_t *out, uint16_t cap, uint16_t *out_len);

#endif
