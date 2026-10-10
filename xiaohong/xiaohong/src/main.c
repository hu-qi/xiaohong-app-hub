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
#include "key_config.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "net_wifi_config.h"
#include "protocol.h"
#include "agent_state.h"
#if defined(SUPPORT_LVGL)
#include "app_hub.h"
#endif
#include "ota.h"
#include "ota_fw_upgrade.h"
#include "font_download.h"
#include "cpu_utils.h"
#include "hal_reboot.h"
#include "soc_osal.h"
#include "los_memory.h"
#if SUPPORT_OHOS
#include "parameter.h"
#endif

#define MSG_QUEUE_SIZE         (8)
#define AUD_MSG_QUEUE_SIZE     (64)  /** 音频下行事件（0x020A）频率高，8 槽易满导致漏喂包卡顿 */
#define AGENT_MSG_QUEUE_SIZE   (32)  /** Agent 事件队列：音频每帧会投递 SEND_AUDIO，队列过小会导致 Put 失败并丢包 */

osMessageQueueId_t g_main_event_qid = NULL;   // 主事件队列，用于接收按键事件、网络连接/断开事件、字库下载完成事件等
osMessageQueueId_t g_audx_event_qid = NULL;   // 音频事件队列，用于接收音频事件
osMessageQueueId_t g_disp_event_qid = NULL;   // 显示事件队列，用于接收显示事件
osMessageQueueId_t g_agent_event_qid = NULL;  // Agent事件队列，用于接收Agent事件

uint32_t key_event_callback_cb(uint8_t event)
{
    if (g_main_event_qid) {
        osMessageQueuePut(g_main_event_qid, (const void *) &event, 0, 0);
    }
    return 0;
}

extern uint32_t LvglTaskEntry(void);
void MainTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    osThreadId_t agent_task_id = NULL;
    osThreadState_t task_state = osThreadInactive;
    uint8_t msg_recv = 0;
    uint8_t msg_send = 0;
    uint8_t reconfig_wifi = 0;
    uint8_t is_downloading = 0;
    LOS_MEM_POOL_STATUS status;

    log_set_level(DEFAULT_LOG_LEVEL);

    ret = board_hw_init();
    if (ret != ERRCODE_SUCC) {
        log_fatal("[MainTask] board_hw_init NG, abort!!\r\n");
        return;
    }

#if SUPPORT_OHOS
    const char *osName = GetOSFullName();
    if (osName != NULL) {
        printf("[MainTask] OsFullName: %s\r\n", osName);
    } else {
        log_error("[MainTask] OsFullName: NULL\r\n");
    }
#endif

#ifdef SUPPORT_LFS
    ret = fs_adapt_mount();
    if (ret != ERRCODE_SUCC) {
        log_fatal("[MainTask] fs_adapt_mount NG, ret = %d, abort!!\r\n", ret);
        return;
    }
#endif

    set_blk_level_off();  // off screen before read config, will on in LvglTaskEntry after creat ui
    ret = read_config_from_lfs();
    if (ret != ERRCODE_SUCC) {
        log_error("[MainTask] read_config_from_lfs NG, use default settings\r\n");
    }

    register_key_event_callback((key_event_callback_t)& key_event_callback_cb);
    g_audx_event_qid = osMessageQueueNew(AUD_MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    #if defined(SUPPORT_LVGL)
    g_disp_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    #endif
    g_main_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    g_agent_event_qid = osMessageQueueNew(AGENT_MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);

#if defined(SUPPORT_LVGL)
    if (0 == LvglTaskEntry()) {
        log_error("[MainTask] LvglTaskEntry NG, No Display\r\n");
    }
#endif
#if defined(SUPPORT_LVGL)
    msg_send = eDisp_Welcome;
    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
    sleep(1);
#endif

    if (has_wifi_config() == ERRCODE_SUCC) {
        WiFiTaskEntry();
    } else {
        reconfig_wifi = 1;
        WiFiReConfgTaskEntry();
    }

    while (1) {
        ret = osMessageQueueGet(g_main_event_qid, (void *)&msg_recv, NULL, 40);
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }

        if (is_downloading && (msg_recv < eKey_MaxCount)) {
            msg_recv = 0;
            log_error("[MainTask] is_downloading[1]: msg[%d] ignore\r\n", msg_recv);
            continue;
        }

        log_debug("[MainTask] osMessageQueueGet: msg[%d]\r\n", msg_recv);
        /* App Hub active: route all key events to display queue for LVGL processing */
        if (msg_recv < eKey_MaxCount && xh_app_hub_is_active()) {
            msg_send = eDisp_AppHub_KeyBase + msg_recv;
            osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
            continue;
        }

        switch(msg_recv) {
            case eKey_VolUp_ShortPressed: {
                if (get_wifi_status() == eWifi_St_ReConfiguring) {
                    #if defined(SUPPORT_LVGL)
                    msg_send = eDisp_WiFi_St_Update;
                    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                    #endif
                    break;
                }

                increase_volume();

                msg_send = eAud_VolUp;
                osMessageQueuePut(g_audx_event_qid, (const void *) &msg_send, 0, 0);
                break;
            }
            case eKey_VolUp_LongPressed: {
                increase_blk_level();
                break;
            }
            case eKey_VolDw_ShortPressed: {
                if (get_wifi_status() == eWifi_St_ReConfiguring) {
                    #if SUPPORT_DELETE_FONT   // 调试用功能，按下音量加键，清除版本信息，删除字库，重新下载字库
                    check_font_bin_exists(true);
                    msg_send = eDisp_Debug_Font_Delete;
                    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                    break;
                    #endif

                    #if defined(SUPPORT_LVGL)
                    msg_send = eDisp_WiFi_St_Update;
                    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                    #endif
                    break;
                }

                decrease_volume();

                msg_send = eAud_VolDw;
                osMessageQueuePut(g_audx_event_qid, (const void *) &msg_send, 0, 0);

                break;
            }
            case eKey_VolDw_LongPressed: {
                #if defined(SUPPORT_LVGL)
                /* VolDw long press: open App Hub (replaces brightness decrease) */
                msg_send = eDisp_AppHub_Open;
                osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                #else
                decrease_blk_level();
                #endif
                break;
            }
            case eKey_Wakeup_ShortPressed: {
                LOS_MemInfoGet(m_aucSysMem0, &status);
                printf("[MainTask] system memory: used[%u][%uKB], free[%u][%uKB]\r\n",
                    status.uwTotalUsedSize, status.uwTotalUsedSize / 1024,
                    status.uwTotalFreeSize, status.uwTotalFreeSize / 1024);

                #if defined(SUPPORT_LVGL)
                msg_send = eDisp_Blk_On;
                osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                #endif

                msg_send = eAud_VolInit;
                osMessageQueuePut(g_audx_event_qid, (const void *) &msg_send, 0, 0);

                msg_send = eAud_WakeUp;
                osMessageQueuePut(g_audx_event_qid, (const void *) &msg_send, 0, 0);
                break;
            }
            case eKey_Wakeup_LongPressed: {
                int vi = get_voice_interrupt_enabled();
                log_info("[MainTask] eKey_Wakeup_LongPressed: toggle voice_interrupt[%d] -> [%d] (persist)\r\n", vi, vi ? 0 : 1);
                (void)set_voice_interrupt_enabled(vi ? 0 : 1);
                break;
            }
            case eKey_Wakeup_LongPressed_5S: {
                log_debug("[MainTask] eKey_Wakeup_LongPressed_5S: ReConfig NetWork\r\n");
                clear_wifi_config();
                restart_system(1);
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
                        agent_send_event(AGENT_EVT_WIFI_DISCONNECTED);
                    }
                }
                break;
            }
            case eSettings_Wifi_Connected: {
                log_debug("[MainTask] [%d]eSettings_Wifi_Connected ...\r\n", eSettings_Wifi_Connected);
                #if 1
                if (reconfig_wifi == 1) {
                    reconfig_wifi = 0;
                    #if defined(SUPPORT_LVGL)
                    msg_send = eDisp_WiFi_ReConfig_Success;
                    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
                    #endif
                    restart_system(2);
                    break;
                }
                #endif
                #if defined(SUPPORT_LVGL)
                msg_send = eDisp_Hand_Shaking;
                osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
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
                is_downloading = 1;
                font_download_on_wifi_connected();
                break;
            }
            case eSettings_Font_Downloaded: {
                log_info("[MainTask] Font download completed, restart system ......\r\n");
                is_downloading = 0;
                restart_system(3);
                break;
            }
            case eSettings_Font_Not_Downloaded: {
                break;
            }
            case eSettings_Ota_FW_Update: {
                is_downloading = 1;
                ota_fw_update_start();
                break;
            }
            case eSettings_Ota_FW_Not_Update: {
                break;
            }
            case eSettings_Run_Ready: {
                osal_msleep(2500U);

                if (AudioListenTaskEntry() == 0) {
                    log_fatal("[MainTask] AudioListenTaskEntry NG, restart system ........\r\n");
                    restart_system(1);
                    break;
                }

                if (AudioPlayTaskEntry() == 0) {
                    log_fatal("[MainTask] AudioPlayTaskEntry NG, restart system ........\r\n");
                    restart_system(1);
                    break;
                }

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
