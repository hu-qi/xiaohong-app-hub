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
#include "net_wifi_config.h"
#include "log.h"
#include "uart_config.h"
#include "uart_tx_data_down.h"

#define LOG_TAG  "[UartTxDm]"
#define UART_WRITE_TIMEOUT_MS    (5U)
#define UART_WRITE_RETRY_MAX     (8U)

static void uart_send_cmd_process(uint16_t cmd, const uint8_t *payload, uint16_t len)
{
    if ((len > 0) && (payload == NULL)) {
        log_error("%s uart_send_cmd_process: len[%d] & payload[NULL] is invalid\r\n", LOG_TAG, len);
        return;
    }
    //if (len > CI1302_UART_PAYLOAD_MAX) {  // 音频数据长度超过4096字节，则截取前4096字节
    //    len = CI1302_UART_PAYLOAD_MAX;
    //}
    uint8_t tx_buff[16] = {
        0xA5, 0xA5, 0x5A, 0x5A,  // head[4]
        0x00, 0x00, 0x00, 0x00,  // checksum[lo,hi] + cmd_type[lo,hi]
        0x00, 0x00, 0x00, 0x00,  // data_len[lo,hi] + version[lo,hi]
        0x78, 0x56, 0x34, 0x12   // tail[4]
    };
    tx_buff[6] = (uint8_t)(cmd & 0xFF);
    tx_buff[7] = (uint8_t)((cmd >> 8) & 0xFF);
    if (len > 0) {
        tx_buff[8] = (uint8_t)(len & 0xFF);
        tx_buff[9] = (uint8_t)((len >> 8) & 0xFF);
    }

    // 1. 先下发帧头16字节
    uint16_t written = 0;
    uint16_t off = 0;
    uint8_t retry = 0;
    while (off < 16U) {
        written = uapi_uart_write(UART_BUS_ID, tx_buff + off, (uint16_t)(16U - off),
                                  UART_WRITE_TIMEOUT_MS);
        if (written == 0U) {
            log_error("%s 63 ==>> P4: cmd[0x%04X] NG: off[%d]-> retry\r\n", LOG_TAG, cmd, off);
            if (++retry >= UART_WRITE_RETRY_MAX) {
                log_error("%s 63 ==>> P4: cmd[0x%04X] NG: off[%d]-> retry[break]\r\n", LOG_TAG, cmd, off);
                break;
            }
            usleep(UART_WRITE_TIMEOUT_MS);
            continue;
        }
        off = (uint16_t)(off + written);
        retry = 0U;
    }
    if (off != 16U) {
        log_error("%s 63 ==>> P4: cmd[0x%04X] NG: [%d]->[%d] => abort\r\n", LOG_TAG, cmd, 16, written);
    } else {
    // 2. 如有必要，再下发payload：命令参数或音频数据
        if (len > 0) {
            uint16_t pay_off = 0;
            retry = 0U;
            while (pay_off < len) {
                written = uapi_uart_write(UART_BUS_ID, payload + pay_off, (uint16_t)(len - pay_off),
                                          UART_WRITE_TIMEOUT_MS);
                if (written == 0U) {
                    if (++retry >= UART_WRITE_RETRY_MAX) {
                        break;
                    }
                    usleep(UART_WRITE_TIMEOUT_MS);
                    continue;
                }
                pay_off = (uint16_t)(pay_off + written);
                retry = 0U;
            }
        }
    }
}


void UartTxTask(void)
{
    #define PAYLOAD_BUFF_LEN  (256)
    uint16_t evt = 0;
    uint8_t payload_len = 0;
    uint8_t payload[PAYLOAD_BUFF_LEN] = {0};
    static uint8_t s_text_tx_payload[UART_TX_TEXT_PAYLOAD_MAX];
    static uint8_t s_aud_tx_payload[AUD_CHUNK_MAX_BYTES];

    while (1) {
        if (osOK != osMessageQueueGet(g_63tx_event_qid, (void *)&evt, NULL, 40)) {
            continue;
        }

        //if ((evt == 0x0A02) || (evt == 0x0A03)) {
        //} else {
        //  log_info("%s UartTxTask: Recv msg[0x%04X]\r\n", LOG_TAG, (uint16_t)evt);
        //}

        switch (evt) {
            case 0x0A02: {  // QUERY_SYSTEM_STATUS
                log_info("%s Send cmd[0x0A02]: QUERY_SYSTEM_STATUS\r\n", LOG_TAG);
                uart_send_cmd_process(0x0A02, NULL, 0);
                break;
            }
            case 0x0A03: {  // REPORT_SYSTEM_STATUS
                payload[0] = get_local_status();
                log_info("%s Send cmd[0x0A03]: REPORT_SYSTEM_STATUS[%d](0-Ready,x-NOT ready)\r\n", LOG_TAG, payload[0]);
                uart_send_cmd_process(0x0A03, payload, 1);
                break;
            }

///////////////////////////////////////////////////////////////////////////////////////
            case 0x0B02: {  // P4 SET_WAKE_UP_STATUS
                log_info("%s Send cmd[0x0B02]: payload[%s]\r\n", LOG_TAG, (char*)payload);
                payload[0] = (agent_get_state()>0)?1:0;    // 0-Idle,1-Wakeup,2-Listening,3-Speaking
                uart_send_cmd_process(0x0B02, payload, 1);
                break;
            }
            case 0x0B03: {  // GET_DEVICE_ID
                log_info("%s Send cmd[0x0B03]: GET_DEVICE_ID\r\n", LOG_TAG);
                uart_send_cmd_process(0x0B03, NULL, 0);
                break;
            }
            case 0x0B04: {  // GET_P4_FW_VERSION
                log_info("%s Send cmd[0x0B04]: GET_P4_FW_VERSION\r\n", LOG_TAG);
                uart_send_cmd_process(0x0B04, NULL, 0);
                break;
            }
            case 0x0B05: {  // SET_63_FW_VERSION
                uart_send_cmd_process(0x0B05, get_version_ws63_str(), strlen(get_version_ws63_str()));
                break;
            }
            case 0x0B06: {  // GET_FONT_VERSION
                log_info("%s Send cmd[0x0B06]: GET_FONT_VERSION\r\n", LOG_TAG);
                uart_send_cmd_process(0x0B06, NULL, 0);
                break;
            }
            case 0x0B07: {  // SET_AGENT_STATUS
                agent_state_t state = agent_get_state();
                if (state == AGENT_STATE_WAKEUP) {
                    payload[0] = 1;
                } else if (state == AGENT_STATE_LISTENING) {
                    payload[0] = 2;
                } else if (state == AGENT_STATE_SPEAKING) {
                    payload[0] = 3;
                } else {
                    payload[0] = 0;
                }
                log_info("%s Send cmd[0x0B07]: SET_AGENT_STATUS: %d[0-Idle,1-Wakeup,2-Listening,3-Speaking]\r\n", LOG_TAG, payload[0]);
                uart_send_cmd_process(0x0B07, payload, 1);
                break;
            }
            case 0x0B08: {  // SET_WS_SERVER_URL
                memset(payload, 0, PAYLOAD_BUFF_LEN);
                get_websocket_server_url((char*)payload, PAYLOAD_BUFF_LEN-1);
                log_info("%s Send cmd[0x0B08]: SET_WS_SERVER_URL: %s\r\n", LOG_TAG, (char*)payload);
                uart_send_cmd_process(0x0B08, payload, strlen((char*)payload));
                break;
            }
            case 0x0B09: {  // SET_OTA_URL
                memset(payload, 0, PAYLOAD_BUFF_LEN);
                get_ota_address((char*)payload, PAYLOAD_BUFF_LEN-1);
                log_info("%s Send cmd[0x0B09]: SET_OTA_URL: %s\r\n", LOG_TAG, (char*)payload);
                uart_send_cmd_process(0x0B09, payload, strlen((char*)payload));
                break;
            }
            case 0x0B0A: {  // SET_INTERRUPT_MODE
                payload[0] = (uint8_t) get_voice_interrupt_enabled();
                log_info("%s Send cmd[0x0B0A]: SET_INTERRUPT_MODE: %d\r\n", LOG_TAG, (uint8_t)payload[0]);
                uart_send_cmd_process(0x0B0A, payload, 1);
                break;
            }
            case 0x0B0B: {  // SET_ACTIVATE_CODE
                char* activation_hint = get_activation_hint();
                log_info("%s Send cmd[0x0B0B]: SET_ACTIVATE_CODE: type[%d]/len[%d][%s]\r\n",
                    LOG_TAG, activation_hint[0], strlen(activation_hint), activation_hint+1);
                uart_send_cmd_process(0x0B0B, activation_hint, strlen(activation_hint));
                set_activation_hint(0, NULL, 0);  // clear last activation_hint
                break;
            }
///////////////////////////////////////////////////////////////////////////////////////

            case 0x0D01: {  // WIFI_SEND_SSID
                payload_len = get_wifi_ssid(payload, WIFI_SSID_LEN_MAX);
                if (PAYLOAD_BUFF_LEN > 0) {
                    log_info("%s Send cmd[0x0D01]: WIFI_SEND_SSID: %s\r\n", LOG_TAG, (char*)payload);
                    uart_send_cmd_process(0x0D01, payload, strlen((char*)payload));
                }
                break;
            }
            case 0x0D02: {  // WIFI_SEND_PSWD
                payload_len = get_wifi_pswd(payload, WIFI_PSWD_LEN_MAX);
                if (PAYLOAD_BUFF_LEN > 0) {
                    log_info("%s Send cmd[0x0D02]: WIFI_SEND_PSWD: %s\r\n", LOG_TAG, (char*)payload);
                    uart_send_cmd_process(0x0D02, payload, strlen((char*)payload));
                }
                break;
            }
            case 0x0D03: { // WIFI_STATUS => P4
                uint8_t wifi_status = get_wifi_status();
                if (wifi_status == eWifi_St_Connected) {
                    log_info("%s Send cmd[0x0D03]: WIFI_STATUS: Connected[%d]:[%s]\r\n", LOG_TAG, wifi_status, get_wifi_ip());
                    payload[0] = wifi_status;
                    uint8_t ip_len = strlen(get_wifi_ip());
                    if (ip_len > 0) {
                        (void)memcpy_s(payload + 1, IP_ADDR_LEN_MAX, get_wifi_ip(), ip_len);
                        payload[ip_len + 1] = '\0';
                    }
                    uart_send_cmd_process(0x0D03, payload, ip_len + 1);
                } else {
                    log_info("%s Send cmd[0x0D03]: WIFI_STATUS: %d(0-DisConnected,1/2-Connecting,3-Connected)\r\n", LOG_TAG, wifi_status);
                    payload[0] = wifi_status;
                    uart_send_cmd_process(0x0D03, payload, 1);
                }
                break;
            }
            case 0x0D07: { // WIFI_SCAN_RESULT
                uint16_t list_len = SCAN_SSID_LIST_COUNT*WIFI_SSID_LEN_MAX;
                char* scan_ssid_list = (char*)osal_kmalloc(list_len, OSAL_GFP_ATOMIC);
                if (scan_ssid_list != NULL) {
                    memset_s(scan_ssid_list, list_len, 0, list_len);

                    if (get_wifi_scan_result(scan_ssid_list, list_len) == ERRCODE_SUCC) {
                        list_len = (uint8_t)strlen((char*)scan_ssid_list);
                        log_info("%s Send cmd[0x0D07]: WIFI_SCAN_RESULT[%d]:[%s]\r\n",
                            LOG_TAG, list_len, (char*)scan_ssid_list);
                        uart_send_cmd_process(0x0D07, scan_ssid_list, list_len);
                    } else {
                        log_error("%s Send cmd[0x0D07]: WIFI_SCAN_RESULT: NG\r\n", LOG_TAG);
                        uart_send_cmd_process(0x0D07, NULL, 0);
                    }
                    osal_kfree(scan_ssid_list);
                } else {
                    log_error("%s Send cmd[0x0D07]: WIFI_SCAN_RESULT: NG: osal_kmalloc failed\r\n", LOG_TAG);
                    uart_send_cmd_process(0x0D07, NULL, 0);
                }
                break;
            }

///////////////////////////////////////////////////////////////////////////////////////
            case 0x0E04: {  // SET_DOWN_AUDIO_DATA_START
                log_info("%s Send cmd[0x0E04]: SET_DOWN_AUDIO_DATA_START\r\n", LOG_TAG);
                uart_send_cmd_process(0x0E04, NULL, 0);
                break;
            }
            case 0x0E05: {  // SET_DOWN_AUDIO_DATA_STOP
                log_info("%s Send cmd[0x0E05]: SET_DOWN_AUDIO_DATA_STOP\r\n", LOG_TAG);
                uart_send_cmd_process(0x0E05, NULL, 0);
                break;
            }
            case 0x0E40: {  // DOWN_TEXT_DATA
                uint16_t text_plen = 0;
                while (uart_tx_pop_pending_text(s_text_tx_payload,
                                                (uint16_t)sizeof(s_text_tx_payload),
                                                &text_plen)) {
                    s_text_tx_payload[text_plen] = '\0';  // 添加字符串结束符
                    log_info("%s Send cmd[0x0E40]: type[%u] len[%u][%s]\r\n",
                             LOG_TAG,
                             (uint8_t)s_text_tx_payload[0],
                             (uint16_t)(text_plen - UART_TX_TEXT_HDR_BYTES),
                             (char*)s_text_tx_payload + UART_TX_TEXT_HDR_BYTES);

                    uart_send_cmd_process(0x0E40, s_text_tx_payload, text_plen);
                }
                break;
            }
            case 0x0E41: {  /* DOWN_AUDIO_DATA：payload 为裸 Opus 帧，与上行 0x0E11 对称 */
                uint16_t aud_plen = 0;
                while (uart_tx_pop_pending_opus(s_aud_tx_payload,
                                                (uint16_t)sizeof(s_aud_tx_payload),
                                                &aud_plen)) {
                    #if 0
                    uint16_t i = 0;
                    for(i = 0; i < aud_plen; i++) {
                        printf("  %02X", s_aud_tx_payload[i]);
                        if ((i + 1) % 16 == 0) {
                            printf("\r\n");
                        }
                    }
                    if (aud_plen % 16 != 0) {
                        printf("\r\n");
                    }
                    #endif

                    //log_debug("%s Send cmd[0x0E41]: opus_len[%3u]Bytes ==>> P4: opus->pcm->play\r\n", LOG_TAG, (unsigned)aud_plen);
                    uart_send_cmd_process(0x0E41, s_aud_tx_payload, aud_plen);
                }
                break;
            }
///////////////////////////////////////////////////////////////////////////////////////

#if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
            case 0x0F10: {  // REPORT_SENSOR_TEMP_AND_HUMI
                int32_t temp = get_sensors_temp();
                payload[0] = (uint8_t)(temp & 0xFF);
                payload[1] = (uint8_t)((temp >> 8) & 0xFF);
                payload[2] = (uint8_t)((temp >> 16) & 0xFF);
                payload[3] = (uint8_t)((temp >> 24) & 0xFF);
                int32_t humi = get_sensors_humi();
                payload[4] = (uint8_t)(humi & 0xFF);
                payload[5] = (uint8_t)((humi >> 8) & 0xFF);
                payload[6] = (uint8_t)((humi >> 16) & 0xFF);
                payload[7] = (uint8_t)((humi >> 24) & 0xFF);

                log_info("%s Send cmd[0x0F10]: REPORT_SENSOR_TEMP_AND_HUMI: %d[0x%08X][0x%02X 0x%02X 0x%02X 0x%02X]\r\n",
                        LOG_TAG, temp, (uint32_t)(temp & 0xFFFFFFFF), payload[0], payload[1], payload[2], payload[3]);
                log_info("%s Send cmd[0x0F10]: REPORT_SENSOR_TEMP_AND_HUMI: %d[0x%08X][0x%02X 0x%02X 0x%02X 0x%02X]\r\n",
                        LOG_TAG, humi, (uint32_t)(humi & 0xFFFFFFFF), payload[4], payload[5], payload[6], payload[7]);
                uart_send_cmd_process(0x0F10, payload, 8);
                break;
            }
            case 0x0F20: {  // REPORT_SENSOR_TEMP
                int32_t temp = get_sensors_temp();
                payload[0] = (uint8_t)(temp & 0xFF);
                payload[1] = (uint8_t)((temp >> 8) & 0xFF);
                payload[2] = (uint8_t)((temp >> 16) & 0xFF);
                payload[3] = (uint8_t)((temp >> 24) & 0xFF);
                log_info("%s Send cmd[0x0F20]: REPORT_SENSOR_TEMP: %d[0x%08X][0x%02X 0x%02X 0x%02X 0x%02X]\r\n",
                        LOG_TAG, temp, (uint32_t)(temp & 0xFFFFFFFF), payload[0], payload[1], payload[2], payload[3]);
                uart_send_cmd_process(0x0F20, payload, 4);
                break;
            }
            case 0x0F21: {  // REPORT_SENSOR_HUMI
                int32_t humi = get_sensors_humi();
                payload[0] = (uint8_t)(humi & 0xFF);
                payload[1] = (uint8_t)((humi >> 8) & 0xFF);
                payload[2] = (uint8_t)((humi >> 16) & 0xFF);
                payload[3] = (uint8_t)((humi >> 24) & 0xFF);
                log_info("%s Send cmd[0x0F21]: REPORT_SENSOR_HUMI: %d[0x%08X][0x%02X 0x%02X 0x%02X 0x%02X]\r\n",
                        LOG_TAG, humi, (uint32_t)(humi & 0xFFFFFFFF), payload[0], payload[1], payload[2], payload[3]);
                uart_send_cmd_process(0x0F21, payload, 4);
                break;
            }
#endif
            default:
                log_error("%s UartTxTask: Recv msg[0x%04X] is invalid, case default\r\n", LOG_TAG, (uint16_t)evt);
                break;
        }
    }
}

uint32_t UartTxTaskEntry(void)
{
    task_config_t task_conf = {
        "63TxTask",        // task name
        1024 * 8,          // stack_size
        24,                // priority
        (void*)UartTxTask  // func
    };

    return create_task(&task_conf, NULL);
}