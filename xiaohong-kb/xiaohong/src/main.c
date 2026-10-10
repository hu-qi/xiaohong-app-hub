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

#include "task_entry.h"
#include "settings.h"
#include "kb_keymap.h"
#include "board_config.h"
#include "key_config.h"
#include "led_config.h"
#include "disp_driver.h"
#include "log.h"

#include "cpu_utils.h"
#include "hal_reboot.h"
#include "soc_osal.h"
#include "los_memory.h"

#if defined(SUPPORT_SLE_KEYBOARD)
#include "sle_client_kb.h"
#endif
#if defined(SUPPORT_BLE_KEYBOARD)
#include "ble_client_kb.h"
#endif


#define MSG_QUEUE_SIZE  (16)
osMessageQueueId_t g_main_event_qid = NULL;   // 主事件队列
osMessageQueueId_t g_kdsp_event_qid = NULL;   // KeyDispatch任务事件队列，用于接收按键、解析和分发给SLE/BLE发送给接收端
osMessageQueueId_t g_disp_event_qid = NULL;   // 显示事件队列，用于接收显示事件
osMessageQueueId_t g_leds_event_qid = NULL;   // LED灯任务事件队列，用于接受LED灯控制事件

void BatteryAlarmTaskEntry(void);
extern uint32_t LvglTaskEntry(void);
void MainTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint8_t msg_recv = 0;
    uint8_t msg_send = 0;
    LOS_MEM_POOL_STATUS status;

    log_set_level(DEFAULT_LOG_LEVEL);

    ret = board_hw_init();
    if (ret != ERRCODE_SUCC) {
        log_fatal("[MainTask] board_hw_init NG, abort!!\r\n");
        return;
    }

    read_config_from_nv();
    keymap_read_from_nv();

    g_main_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    g_kdsp_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    g_disp_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);
    g_leds_event_qid = osMessageQueueNew(MSG_QUEUE_SIZE, sizeof(uint8_t), NULL);

    LedsTaskEntry();
    KeysTaskEntry();
    KeyDispatchTaskEntry();
    BatteryAlarmTaskEntry();
    if (0 == LvglTaskEntry()) {
        log_error("[MainTask] LvglTaskEntry NG, No Display\r\n");
    }

    osal_msleep(500);
    if (get_protocol_type() == eProto_SLE) {
        #if defined(SUPPORT_SLE_KEYBOARD)
        SleClientTaskEntry();
        #endif
    } else if (get_protocol_type() == eProto_BLE) {
        #if defined(SUPPORT_BLE_KEYBOARD)
        BleClientTaskEntry();
        #endif
    }

    while (1) {
        ret = osMessageQueueGet(g_main_event_qid, (void *)&msg_recv, NULL, 40);
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }

        if (msg_recv == SWITCH_PROTOCOL_TYPE) {
            post_msg_event(g_leds_event_qid, eLed_AllOff);

            if (get_protocol_type() == eProto_SLE) {
                #if defined(SUPPORT_SLE_KEYBOARD)
                SleClientTaskEntry();
                #endif
            } else if (get_protocol_type() == eProto_BLE) {
                #if defined(SUPPORT_BLE_KEYBOARD)
                BleClientTaskEntry();
                #endif
            }
        }

        sleep(1);
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

// 低电量检测任务：每 5s 读取一次电量，低于阈值时蜂鸣器提示 + 屏幕提示 + LED 红色闪烁
static void BatteryAlarmTask(void)
{
    uint8_t low_battery = 0;

    while (1) {
        sleep(5);   // 每 5s 检测一次

        uint8_t volt = get_voltage_level();
        if (volt <= BATTERY_LOW_THRESHOLD) {
            if (!low_battery) {
                low_battery = 1;
                log_warn("[Battery] low battery: %d%%\r\n", volt);
                buzzer_beep(150);
                post_msg_event(g_disp_event_qid, eDisp_LowBattery);
                post_msg_event(g_leds_event_qid, eLed_LowBattery);
            }
        } else {
            if (low_battery) {
                low_battery = 0;
                post_msg_event(g_disp_event_qid, eDisp_LowBatteryClear);
            }
        }
    }
}

void BatteryAlarmTaskEntry(void)
{
    task_config_t task_conf = {
        "BatteryAlarmTask",   // task name
        1024*2,               // stack_size
        22,                   // priority
        (void*)BatteryAlarmTask  // func
    };

    create_task(&task_conf, NULL);
}
#if SUPPORT_OHOS
SYS_RUN(MainTaskEntry);
#else
app_run(MainTaskEntry);
#endif
