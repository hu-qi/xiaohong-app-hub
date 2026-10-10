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

#include "ci1302_data_process.h"
#include "ci1302_uart_ring.h"
#include "ci1302_opus_uplink.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "agent_state.h"
#include "audio_config.h"
#include "ci1302_tts_downlink.h"
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include "log.h"

extern osMessageQueueId_t g_disp_event_qid;
extern osMessageQueueId_t g_audx_event_qid;

static void ci1302_post_audio_event_with_retry(uint8_t evt, const char *tag)
{
    if (g_audx_event_qid == NULL) {
        return;
    }
    for (uint8_t i = 0; i < 3U; i++) {
        if (osMessageQueuePut(g_audx_event_qid, (const void *)&evt, 0, 1U) == osOK) {
            return;
        }
    }
    static uint32_t s_drop_cnt = 0U;
    s_drop_cnt++;
    if ((s_drop_cnt % 20U) == 1U) {
        log_warn("[Ci1302UP] %s: aud queue full, drop=%lu\r\n", tag, (unsigned long)s_drop_cnt);
    }
}

uint8_t frame_head[4] = {0xA5, 0xA5, 0x5A, 0x5A};
uint8_t frame_tail[8][4] = {
    {0x78, 0x56, 0x34, 0x12},
    {0x77, 0x56, 0x34, 0x12},
    {0x66, 0x56, 0x34, 0x12},
    {0x99, 0x56, 0x34, 0x12},
    {0x88, 0x56, 0x34, 0x12},
    {0xAA, 0x56, 0x34, 0x12},
    {0xBB, 0x56, 0x34, 0x12},
    {0xAB, 0x56, 0x34, 0x12},
};


/** 当前 VAD 段内累计 0x0105 负载字节数（Opus，仅统计，用于 0x0106 日志） */
static uint32_t s_segment_audio_data_len;
volatile uint32_t g_ci1302_last_vad_segment_bytes;

#define CI1302_MAX_FRAME_PAYLOAD  (80)
static uint8_t s_0105_payload[CI1302_MAX_FRAME_PAYLOAD];

typedef enum {
    PS_SYNC0 = 0,
    PS_SYNC1,
    PS_SYNC2,
    PS_SYNC3,
    PS_HDR,
    PS_PAYLOAD,
} parse_state_t;

static parse_state_t s_ps = PS_SYNC0;
static uint8_t s_hdr[16];   // 16 bytes 帧头信息
static unsigned s_hdr_i;    // 帧头索引
static uint16_t s_cmd_type; // 命令类型
static uint16_t s_pl_len;   // 载荷长度
static uint32_t s_pay_i;    // 载荷索引
static uint8_t s_pay_small[8];    // no need 256 bytes

static bool ctrl_tail_ok(const uint8_t *h)
{
    return memcmp(h + 12, frame_tail[0], 4) == 0;
}

static void process_ctrl_command(uint16_t cmd_type, const uint8_t *hdr, const uint8_t *payload,
                                 uint32_t payload_len)
{
    uint8_t msg_send = 0;

    (void)hdr;
    (void)payload;
    (void)payload_len;

    switch (cmd_type) {
    case 0x0102:
        #if defined(SUPPORT_LVGL)
        msg_send = eDisp_Blk_On;
        if (g_disp_event_qid != NULL) {
            osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);
        }
        #endif
        agent_send_event(AGENT_EVT_WAKE_WORD_DETECTED);
        break;
    case 0x0109:
        #if defined(SUPPORT_LVGL)
        msg_send = eDisp_Blk_Off;
        if (g_disp_event_qid != NULL) {
            osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);
        }
        #endif
        break;
    case 0x0108:
        s_segment_audio_data_len = 0;
        break;
    case 0x0104:
        s_segment_audio_data_len = 0;
        break;
    case 0x0106: {
        uint32_t total = s_segment_audio_data_len;
        s_segment_audio_data_len = 0;
        g_ci1302_last_vad_segment_bytes = total;
        /* Agent：listen/stop 表示「本段 utterance 结束」，供服务端做 ASR；与 total 是否>0 无关 */
        agent_send_event(AGENT_EVT_CI1302_VAD_SEGMENT_END);
        break;
    }
    case 0x0217:
    case 0x0218:
        break;
    case 0x020A:
        msg_send = eAud_SendAudioData;
        ci1302_post_audio_event_with_retry(msg_send, "020A");
        break;
    case 0x020D:
        /* 芯片可能在「整段 TTS 尚未结束」时上报本通知（例如内部一段 PCM 播完）。
         * 若 MCU 仍有待发 PCM，主动走与 0x020A 相同路径发 0x020B，避免对端短暂停拉时剩余音频发不出去。 */
         if (!ci1302_tts_is_empty() && g_audx_event_qid != NULL) {
            msg_send = eAud_SendAudioData;
            ci1302_post_audio_event_with_retry(msg_send, "020D");
        }
        break;
    case 0x0801:
        break;
    default:
        break;
    }
}

static void dispatch_frame(void)
{
    // 解析帧，单独对0105音频数据帧(opus)进行处理
    if (s_cmd_type == 0x0105) {
        if (agent_should_uplink_ci1302_opus() && (s_pl_len > 0U)) {
            s_segment_audio_data_len += (uint32_t)s_pl_len;
            (void)ci1302_uplink_send_opus_frame(s_0105_payload, (size_t)s_pl_len);
        }
        return;
    }

    if (!ctrl_tail_ok(s_hdr)) {
        log_error("[Ci1302UP] frame cmd[0x%04X]: tail mismatch (non-DEF_FILL)\r\n", s_cmd_type);
        return;
    }

    // 解析帧，非0105音频数据帧，则按普通命令处理
    if (s_pl_len <= sizeof(s_pay_small)) {
        process_ctrl_command(s_cmd_type, s_hdr, s_pay_small, s_pl_len);
    } else {
        process_ctrl_command(s_cmd_type, s_hdr, NULL, s_pl_len);
    }
}

static void parse_reset_to_sync0(void)
{
    s_ps = PS_SYNC0;
}

static void parse_feed_byte(uint8_t b)
{
    switch (s_ps) {
    case PS_SYNC0:
        if (b == 0xA5) {
            s_ps = PS_SYNC1;
        }
        break;
    case PS_SYNC1:
        if (b == 0xA5) {
            s_ps = PS_SYNC2;
        } else {
            parse_reset_to_sync0();
            parse_feed_byte(b);
        }
        break;
    case PS_SYNC2:
        if (b == 0x5A) {
            s_ps = PS_SYNC3;
        } else if (b == 0xA5) {
            s_ps = PS_SYNC1;
        } else {
            parse_reset_to_sync0();
        }
        break;
    case PS_SYNC3:
        if (b == 0x5A) {
            s_hdr[0] = 0xA5;
            s_hdr[1] = 0xA5;
            s_hdr[2] = 0x5A;
            s_hdr[3] = 0x5A;
            s_hdr_i = 4;
            s_ps = PS_HDR;
        } else if (b == 0xA5) {
            s_ps = PS_SYNC1;
        } else {
            parse_reset_to_sync0();
        }
        break;
    case PS_HDR:
        s_hdr[s_hdr_i++] = b;
        if (s_hdr_i >= 16U) {
            s_cmd_type = (uint16_t)(s_hdr[6] | (s_hdr[7] << 8));
            s_pl_len   = (uint16_t)(s_hdr[8] | (s_hdr[9] << 8));
            if (s_pl_len > CI1302_MAX_FRAME_PAYLOAD) {
                parse_reset_to_sync0();
                return;
            }
            s_pay_i = 0;
            if (s_pl_len == 0U) {
                dispatch_frame();
                parse_reset_to_sync0();
            } else {
                s_ps = PS_PAYLOAD;
            }
        }
        break;
    case PS_PAYLOAD:
        if (s_cmd_type == 0x0105) {
            if (s_pay_i < CI1302_MAX_FRAME_PAYLOAD) {
                s_0105_payload[s_pay_i] = b;
            } else {
                static uint32_t s_overflow_log;
                if ((s_overflow_log++ % 256U) == 0U) {
                    log_error("[Ci1302UP] 0x0105: payload overflow (payload len[%lu] >= [%lu])\r\n", s_pl_len, CI1302_MAX_FRAME_PAYLOAD);
                }
            }
        } else if (s_pay_i < sizeof(s_pay_small)) {
            s_pay_small[s_pay_i] = b;
        }
        s_pay_i++;
        if (s_pay_i >= (uint32_t)s_pl_len) {
            dispatch_frame();
            parse_reset_to_sync0();
        }
        break;
    default:
        parse_reset_to_sync0();
        break;
    }
}

uint32_t parse_rx_ring_buff(void)
{
    uint8_t b;
    uint32_t n = 0;
    while (ci1302_uart_ring_pop_byte(&b)) {
        n++;
        parse_feed_byte(b);
    }
    return n;
}
