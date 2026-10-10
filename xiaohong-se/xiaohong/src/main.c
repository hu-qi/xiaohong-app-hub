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

#include "settings.h"
#include "task_entry.h"
#include "board_config.h"
#include "audio_config.h"
#include "net_wifi_config.h"
#include "protocol.h"
#include "agent_state.h"
#include "ota.h"
#include "ota_fw_upgrade.h"
#include "font_download.h"
#include "cpu_utils.h"
#include "hal_reboot.h"
#include "soc_osal.h"
#include "los_memory.h"
//#include "uart_config.h"

#define MSG_QUEUE_SIZE         (64)
osMessageQueueId_t g_main_event_qid = NULL;      // 主事件队列，用于接收按键事件、网络连接/断开事件、字库下载完成事件等
osMessageQueueId_t g_63tx_event_qid = NULL;      // WS63 Uart Tx事件队列，用于相应UART的TX事件
osMessageQueueId_t g_agent_event_qid = NULL;     // Agent事件队列，用于接收Agent事件
#if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
osMessageQueueId_t g_sle_server_event_qid = NULL;   // SLE Server事件队列，用于SLE Server接收事件
extern uint32_t SleServerTaskEntry(void);
#endif

extern uint32_t UartRxTaskEntry(void);
extern uint32_t UartTxTaskEntry(void);

void MainTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    osThreadId_t agent_task_id = NULL;
    osThreadState_t task_state = osThreadInactive;
    uint16_t msg_recv = 0;
    uint8_t reconfig_wifi = 0;

    log_set_level(DEFAULT_LOG_LEVEL);

    ret = board_hw_init();
    if (ret != ERRCODE_SUCC) {
        log_fatal("[MainTask] board_hw_init NG, abort!!\r\n");
        return;
    }

    g_main_event_qid    = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint16_t), NULL);
    g_63tx_event_qid    = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint16_t), NULL);
    g_agent_event_qid   = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint16_t), NULL);
    #if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
    g_sle_server_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint16_t), NULL);
    #endif
    log_info("[MainTask] WS63 init UART Rx/Tx task:\r\n");
    if (UartRxTaskEntry() == 0) {
        log_fatal("[MainTask] UartRxTaskEntry NG, restart system ........\r\n");
        restart_system(1);
    }
    if (UartTxTaskEntry() == 0) {
        log_fatal("[MainTask] UartTxTaskEntry NG, restart system ........\r\n");
        restart_system(1);
    }

    #if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
    log_info("[MainTask] Init SLE Server task ......\r\n");
    SleServerTaskEntry();
    #endif

    set_local_status(eSysSt_READY);  // 放在这里还不是很准确，但先放在这里。
    // 每秒发一次查询对端状态的消息，直到对端回复Ready为止，同时也报告一次自身状态给对端
    while (get_peer_status() != eSysSt_READY) {
        post_msg_event(g_63tx_event_qid, 0x0A02);  // QUERY_SYSTEM_STATUS
        usleep(500*1000);
        post_msg_event(g_63tx_event_qid, 0x0A03);  // REPORT_SYSTEM_STATUS
        usleep(500*1000);
    }
    log_info("[MainTask] WS63 <==> P4 communication channel established successful ......\r\n");

    // UART 通信正常，向P4查询必要的系统信息，保存到settings中的全局变量中备用
    post_msg_event(g_63tx_event_qid, 0x0B05);  // SET_63_FW_VERSION
    post_msg_event(g_63tx_event_qid, 0x0B03);  // GET_DEVICE_ID
    // 只要WS63向P4查询DEVICE_ID，P4除了会回复DEVICE_ID，还会主动上报其他信息
    // 所以不需要再查询P4的FW版本、字库版本、WebSocket服务器URL、OTA地址等

    while (1) {
        if (osOK != osMessageQueueGet(g_main_event_qid, (void *)&msg_recv, NULL, 40)) {
            continue;
        }

        switch(msg_recv) {
            case eSettings_WiFi_Init: {
                WiFiTaskEntry();
                break;
            }
            case eSettings_WiFi_ReInit: {
                reconfig_wifi = 1;
                clear_wifi_config();
                log_debug("[MainTask] [%d]eSettings_WiFi_ReInit ...\r\n", eSettings_WiFi_ReInit);
                WiFiReConfgTaskEntry();
                break;
            }
            case eSettings_WiFi_DisConn: {
                disconnect_with_hotspot();
                break;
            }
            case eSettings_Wifi_DisConnected: {
                log_debug("[MainTask] [%d]eSettings_Wifi_DisConnected ...\r\n", eSettings_Wifi_DisConnected);
                if (agent_task_id != NULL) {
                    task_state = osThreadGetState(agent_task_id);
                    if ((task_state == osThreadRunning) ||
                        (task_state == osThreadReady) ||
                        (task_state == osThreadBlocked)) {
                        log_info("[MainTask] Agent is running, send AGENT_EVT_WIFI_DISCONNECTED to Agent\r\n");
                        post_msg_event(g_agent_event_qid, AGENT_EVT_WIFI_DISCONNECTED);
                    }
                }
                break;
            }
            case eSettings_Wifi_Connected: {
                log_debug("[MainTask] [%d]eSettings_Wifi_Connected ...\r\n", eSettings_Wifi_Connected);
#if 1
                if (reconfig_wifi == 1) {
                    reconfig_wifi = 0;
                    restart_system(2);
                }
#endif
                ret = wifi_sta_wait_ready_for_tcp(15000U);
                if (ret != ERRCODE_SUCC) {
                    log_error("[MainTask] wifi_sta_wait_ready_for_tcp NG, ret = %d\r\n", ret);
                } else {
                    log_info("[MainTask] wifi_sta_wait_ready_for_tcp OK\r\n");
                }

                ota_check_after_wifi_config_success();
                break;
            }
            case eSettings_Start_Font_If_Needed: {
                //font_download_on_wifi_connected();
                break;
            }
            case eSettings_Font_Downloaded: {
                log_info("[MainTask] Font download completed, restart system ......\r\n");
                restart_system(3);
                break;
            }
            case eSettings_Font_Not_Downloaded: {
                break;
            }
            case eSettings_Ota_FW_Update: {
                //ota_fw_update_start();
                break;
            }
            case eSettings_Ota_FW_Not_Update: {
                break;
            }
            case eSettings_Run_Ready: {
                osal_msleep(2500U);

                agent_task_id = (osThreadId_t)(uintptr_t)AgentTaskEntry();
                if (agent_task_id == NULL) {
                    log_fatal("[MainTask] AgentTaskEntry NG, restart system ........\r\n");
                    restart_system(1);
                    break;
                }
                break;
            }
            default: {
                break;
            }
        }
    }
}

static void MainTaskEntry(void)
{
    task_config_t task_conf = {
        "MainTask",      // task name
        1024*4,          // stack_size
        20,              // priority
        (void*)MainTask  // func
    };

    create_task(&task_conf, NULL);
}
#if SUPPORT_OHOS
SYS_RUN(MainTaskEntry);
#else
app_run(MainTaskEntry);
#endif
