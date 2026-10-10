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
#include "net_wifi_config.h"

#define  LOG_TAG  "[UartRxUp]"

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
static uint8_t s_small_payload[128];     // 普通小数据帧的载荷长度最大128字节
#define RX_MAX_FRAME_PAYLOAD  (16+4096)  // 音频/文本/二进制数据帧的载荷长度最大4KB
static uint8_t s_audio_payload[RX_MAX_FRAME_PAYLOAD];

/** 当前 VAD 段内累计 0x0105 负载字节数（Opus，仅统计，用于 0x0106 日志） */
static uint32_t s_segment_audio_data_len = 0;
volatile uint32_t g_last_vad_segment_bytes = 0;

static bool ctrl_tail_ok(const uint8_t *h)
{
    return memcmp(h + 12, frame_tail[0], 4) == 0;
}

static void process_ctrl_command(uint16_t cmd_type, const uint8_t *hdr, const uint8_t *payload, uint32_t payload_len)
{
    (void)hdr;
    (void)payload;
    (void)payload_len;

    switch (cmd_type) {
    case 0x0A02:    // P4 QUERY_SYSTEM_STATUS
        log_info("%s Recv cmd[0x0A02]: REPORT_SYSTEM_STATUS[0x0A03]\r\n", LOG_TAG);
        post_msg_event(g_63tx_event_qid, 0x0A03);  // WS63 REPORT_SYSTEM_STATUS
        break;
    case 0x0A03:    // P4 REPORT_SYSTEM_STATUS
        log_info("%s Recv cmd[0x0A03]: payload[0x%02X](0-OK,x-NOT OK)\r\n", LOG_TAG, payload[0]);
        set_peer_status(payload[0]);
        break;

    case 0x0B01:    // P4 GET_WAKE_UP_STATUS
        log_info("%s Recv cmd[0x0B01]: SET_WAKE_UP_STATUS[0x0B02]\r\n", LOG_TAG);
        post_msg_event(g_63tx_event_qid, 0x0B02);  // WS63 SET_WAKE_UP_STATUS
        break;
    case 0x0B02:    // P4 SET_WAKE_UP_STATUS
        log_info("%s Recv cmd[0x0B02]: len[%u],payload[%d]\r\n", LOG_TAG, payload_len, payload[0]);
        if (payload[0] == 1) {
            post_msg_event(g_agent_event_qid, AGENT_EVT_WAKE_WORD_DETECTED);
        } else if (payload[0] == 0) {
            post_msg_event(g_agent_event_qid, AGENT_EVT_INTO_SLEEP_MODE);
        }
        break;
    case 0x0B03:    // P4 SET_DEVICE_ID
        set_device_id(payload, (uint8_t)payload_len);
        break;
    case 0x0B04:    // SET_P4_FW_VERSION
        set_version_p4((char*)payload);
        break;
    case 0x0B05:    // GET_63_FW_VERSION
        post_msg_event(g_63tx_event_qid, 0x0B05);  // WS63 SET_63_FW_VERSION
        break;
    case 0x0B06:    // SET_FONT_VERSION
        set_version_font((char*)payload);
        break;
    case 0x0B07:    // GET_AGENT_STATUS
        post_msg_event(g_63tx_event_qid, 0x0B07);  // WS63 SET_AGENT_STATUS
        break;
    case 0x0B08:    // GET_WS_SERVER_URL
        post_msg_event(g_63tx_event_qid, 0x0B08);  // WS63 SET_WS_SERVER_URL
        break;
    case 0x0B09:    // SET_OTA_URL
        set_ota_address((char*)payload);
        break;
    case 0x0B0A:    // SET_INTERRUPT_MODE
        log_info("%s Recv cmd[0x0B0A]: SET_INTERRUPT_MODE\r\n", LOG_TAG);
        set_voice_interrupt_enabled(payload[0]);
        break;
    case 0x0B0C:    // INTERRUPT_SPEAKING
        log_info("%s Recv cmd[0x0B0C]: INTERRUPT_SPEAKING\r\n", LOG_TAG);
        post_msg_event(g_agent_event_qid, AGENT_EVT_INTERRUPT_SPEAKING);
        break;


    case 0x0D01:  // WIFI_SEND_SSID
        log_info("%s Recv cmd[0x0D01]: WIFI_SEND_SSID[%d][%s]\r\n", LOG_TAG, (uint8_t)payload_len, (char*)payload);
        set_wifi_ssid(payload, (uint8_t)payload_len);
        break;
    case 0x0D02:  // WIFI_SEND_PSWD
        log_info("%s Recv cmd[0x0D02]: WIFI_SEND_PSWD[%d][%s]\r\n", LOG_TAG, (uint8_t)payload_len, (char*)payload);
        set_wifi_pswd(payload, (uint8_t)payload_len);
        break;
    case 0x0D03:  // WIFI_STATUS
        if (eWifi_St_Connected == get_wifi_status()) {
            post_msg_event(g_63tx_event_qid, 0x0D01);
            post_msg_event(g_63tx_event_qid, 0x0D02);
        }
        post_msg_event(g_63tx_event_qid, 0x0D03);
        break;
    case 0x0D04:  // WIFI_INIT_CONNECT
        log_info("%s Recv cmd[0x0D04]: WIFI_INIT_CONNECT\r\n", LOG_TAG);
        post_msg_event(g_main_event_qid, eSettings_WiFi_Init);
        break;
    case 0x0D05:  // WIFI_RE_CONNECT
        log_info("%s Recv cmd[0x0D05]: WIFI_RE_CONNECT\r\n", LOG_TAG);
        post_msg_event(g_main_event_qid, eSettings_WiFi_ReInit);
        break;
    case 0x0D06:  // WIFI_DISCONNECT
        log_info("%s Recv cmd[0x0D06]: WIFI_DISCONNECT\r\n", LOG_TAG);
        post_msg_event(g_main_event_qid, eSettings_WiFi_DisConn);
        break;
    case 0x0D07:  // WIFI_START_SCAN
        log_info("%s Recv cmd[0x0D07]: WIFI_START_SCAN\r\n", LOG_TAG);
        post_msg_event(g_63tx_event_qid, 0x0D07);
        break;

    case 0x0E01:    // SET_VOICE_WAKE_UP
        post_msg_event(g_agent_event_qid, AGENT_EVT_WAKE_WORD_DETECTED);
        break;
    case 0x0E02: {  // SET_VAD_START  => UP_AUDIO_DATA[0x0E21] => SET_FORCE_VAD_END
        log_info("%s Recv cmd[0x0E02]: VAD_START  ===={{{{\r\n", LOG_TAG);
        s_segment_audio_data_len = 0;
        post_msg_event(g_agent_event_qid, AGENT_EVT_VAD_SEGMENT_START);
        //osDelay(200);
        break;
    }
    case 0x0E03:    // SET_FORCE_VAD_END
        // 正常处理，发送音频数据到服务器
        g_last_vad_segment_bytes = s_segment_audio_data_len;
        s_segment_audio_data_len = 0;
        log_info("%s Recv cmd[0x0E03]: VAD_END to send listen/stop len[%u]Bytes  ====}}}}\r\n\r\n", LOG_TAG, g_last_vad_segment_bytes);
        post_msg_event(g_agent_event_qid, AGENT_EVT_VAD_SEGMENT_END);

        break;

///////////////////////////////////////////////////////////////////////////////////////
    case 0x0E20: {  // UP_TEXT_DATA，上行文本，P4发送文本到WS63, WS63再转发到Web
        uint16_t text_plen = (uint16_t)(payload[2]<<8|(uint8_t)payload[1]);
        log_info("%s Recv cmd[0x0E20]: type[%d] len[%u][%s]\r\n",
                 LOG_TAG, (uint8_t)payload[0], text_plen, (char*)payload + 3);
        // 将Text数据发送到服务器
        // post_msg_event(g_agent_event_qid, AGENT_EVT_WAKE_WORD_DETECTED);
        break;
    }
    case 0x0E21: {  // UP_AUDIO_DATA
        // already processed in dispatch_frame()
        break;
    }
    case 0x0E22:    // DOWN_AUDIO_DATA_END
        break;

#if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
    // 这里收到查询传感器数据的命令，先转给SLE Server，由SLE Server先判断对应的传感器是否有连接
    // 有正常连接再去发送TEMP/HUMI数据
    case 0x0F10: {  // REPORT_SENSOR_TEMP_AND_HUMI
        post_msg_event(g_sle_server_event_qid, 0x0F10);
        break;
    }
    case 0x0F20: {
        post_msg_event(g_sle_server_event_qid, 0x0F20);
        break;
    }
    case 0x0F21: {  // QUERY_SENSOR_HUMI
        post_msg_event(g_sle_server_event_qid, 0x0F21);
        break;
    }
#endif
    default:
        break;
    }
}

void dispatch_frame(void)
{
    // 解析音频数据帧，直接将音频数据帧(opus)通过agent上传给服务器
    if (s_cmd_type == 0x0E21) {  // UP_AUDIO_DATA
        if (agent_should_uplink_audio_data() && (s_pl_len > 0U)) {
            s_segment_audio_data_len += s_pl_len;
            //log_info("%s Recv cmd[0x0E21]: UP_AUDIO_DATA: len[%3u]Bytes\r\n", LOG_TAG, s_pl_len);
            agent_feed_audio_data(s_audio_payload, (size_t)s_pl_len);
        }
        return;
    }

    if (!ctrl_tail_ok(s_hdr)) {
        log_error("%s frame cmd[0x%04X]: tail mismatch (non-DEF_FILL)\r\n", LOG_TAG, s_cmd_type);
        return;
    }

    // 解析帧，非音频数据帧，则按普通命令处理
    if (s_pl_len <= sizeof(s_small_payload)) {
        process_ctrl_command(s_cmd_type, s_hdr, s_small_payload, s_pl_len);
    } else {
        process_ctrl_command(s_cmd_type, s_hdr, NULL, s_pl_len);
    }
}

void parse_reset_to_sync0(void)
{
    s_ps = PS_SYNC0;
}

void parse_feed_byte(uint8_t b)
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
            if (s_pl_len > RX_MAX_FRAME_PAYLOAD) {
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
        if (s_cmd_type == 0x0E21) {  // upload audio data
            if (s_pay_i < RX_MAX_FRAME_PAYLOAD) {
                s_audio_payload[s_pay_i] = b;
            } else {
                static uint32_t s_overflow_log;
                if ((s_overflow_log++ % 256U) == 0U) {
                    log_error("%s 0x0E21: payload overflow (payload len[%lu] >= [%lu])\r\n",
                              LOG_TAG, s_pl_len, RX_MAX_FRAME_PAYLOAD);
                }
            }
        } else if (s_pay_i < sizeof(s_small_payload)) {
            s_small_payload[s_pay_i] = b;
        }
        s_pay_i++;
        if (s_pay_i >= (uint32_t)s_pl_len) {
            s_small_payload[s_pl_len] = '\0';
            dispatch_frame();
            parse_reset_to_sync0();
        }
        break;
    default:
        parse_reset_to_sync0();
        break;
    }
}

uint32_t rx_ring_buff_parse(void)
{
    uint8_t b;
    uint32_t n = 0;
    while (uart_ring_pop_byte(&b)) {
        n++;
//      printf("%s rx_ring_buff_parse: [0x%02X]\r\n", LOG_TAG, b);
        parse_feed_byte(b);
    }
    return n;
}
