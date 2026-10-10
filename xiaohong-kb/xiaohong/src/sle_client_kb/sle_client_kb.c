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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"

#include "log.h"
#include "settings.h"
#include "task_entry.h"
#include "key_config.h"
#include "led_config.h"
#include "disp_driver.h"

#include "sle_client_kb.h"
#include "sle_client.h"
#include "kb_keymap.h"

#define LOG_TAG  "[SleClientTask]"
#define KEY_EVNT_REPORT_BUFF_LEN   (8)

static void sle_client_parse_atomcode_status(uint8_t status)
{
    switch (status) {
        case 0x00:
            log_debug("Recv Report: 0x00[Idle] -> 0x%02X[AtomCode_Idle]\r\n", eLed_AtomCode_Idle);
            set_atomcode_status(eLed_AtomCode_Idle);
            post_msg_event(g_leds_event_qid, eLed_AtomCode_Idle);
            break;
        case 0x01:
            log_debug("Recv Report: 0x01[thinking] -> 0x%02X[AtomCode_Thinking]\r\n", eLed_AtomCode_Thinking);
            set_atomcode_status(eLed_AtomCode_Thinking);
            post_msg_event(g_leds_event_qid, eLed_AtomCode_Thinking);
            break;
        case 0x02:
            log_debug("Recv Report: 0x02[working] -> 0x%02X[AtomCode_Working]\r\n", eLed_AtomCode_Working);
            set_atomcode_status(eLed_AtomCode_Working);
            post_msg_event(g_leds_event_qid, eLed_AtomCode_Working);
            break;
        case 0x03:
            log_debug("Recv Report: 0x03[waiting_confirm] -> 0x%02X[AtomCode_Waiting_Confirm]\r\n", eLed_AtomCode_Waiting_Confirm);
            set_atomcode_status(eLed_AtomCode_Waiting_Confirm);
            post_msg_event(g_leds_event_qid, eLed_AtomCode_Waiting_Confirm);
            break;
        case 0x04:
            log_debug("Recv Report: 0x04[error] -> 0x%02X[AtomCode_Error]\r\n", eLed_AtomCode_Error);
            set_atomcode_status(eLed_AtomCode_Error);
            post_msg_event(g_leds_event_qid, eLed_AtomCode_Error);
            break;
        default:
            log_debug("Recv Report: Atomcode status: 0x%02X[unKnown]\r\n", status);
            set_atomcode_status(0xFF);
            break;
    }

    post_msg_event(g_disp_event_qid, eDisp_All_Update);
}

int sle_client_recv_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    if ((len == REPORT_SIZE) && (data[0] == REPORT_ID)) {
        switch (data[1]) {
            case REPORT_TYPE_ATOMCODE_STATUS: // Atomcode 工作状态
                sle_client_parse_atomcode_status(data[2]);
                break;
//          case xxx:  // Other report msg Type
//              break;
            default:
                break;
        }
    }
}

void SleClientTask(void)
{
    uint32_t ready_count = 0xFF;

    if (get_protocol_type() != eProto_SLE) {
        log_error("%s %s: protocol_type[!SLE] return\r\n", LOG_TAG, __func__);
        return;
    }

    sle_addr_t target_addr = {0};
    get_dongle_mac((uint8_t*)&target_addr.addr, SLE_ADDR_LEN);

    sle_set_server_name(SLE_SERVER_NAME);
    sle_set_server_addr(&target_addr);

    sle_client_init();

#if USE_CUSTOM_MAC_ADDR  // 设置客户端本地地址，如果参数非法，则使用随机地址
    uint8_t client_addr[SLE_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    sle_client_set_local_address(client_addr, SLE_ADDR_LEN);
#else
    sle_client_set_local_address(NULL, 0);
#endif

    while (1) {
        if (get_protocol_type() != eProto_SLE) {
            log_info("%s %s: protocol_type[!SLE] exit\r\n", LOG_TAG, __func__);
            break;
        }

        if (ready_count != sle_client_get_ready_count()) {
            ready_count = sle_client_get_ready_count();
            if (ready_count > 0) {
                // 配对连接Dongle成功，默认回到主页面
                if (eWorkMode_Normal == get_workmode()) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Normal_Page);
                } else if (eWorkMode_Local == get_workmode()) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Local_Page);
                }
            }
            log_info("%s: ready_count[%d]\r\n", LOG_TAG, ready_count);
        }
        sleep(1);
    }

    sle_client_deinit();
    if (get_protocol_type() != eProto_SLE) {
        post_msg_event(g_main_event_qid, (uint8_t)SWITCH_PROTOCOL_TYPE);
    }
}

uint32_t SleClientTaskEntry(void)
{
    task_config_t task_conf = {
        "SCliTask",      // task name
        1024*8,          // stack_size
        24,              // priority
        (void*)SleClientTask  // func
    };

    return create_task(&task_conf, NULL);
}