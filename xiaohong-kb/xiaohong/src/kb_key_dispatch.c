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

#include "kb_keymap.h"
#include "kb_keymap_config.h"
#include "kb_key_dispatch.h"

#if defined(SUPPORT_SLE_KEYBOARD)
#include "sle_client_kb.h"
#include "sle_client.h"
#endif

#if defined(SUPPORT_BLE_KEYBOARD)
#include "ble_client.h"   // BLE-HID 回退链路: 无Dongle连接时, 按键改走BLE直连PC
#endif

#define LOG_TAG  "[KeyDispatchTask]"
#define KEY_EVNT_REPORT_BUFF_LEN   (8)

// 发送到Dongle/PC端的实际按键编码(优先映射匹配Atomcode的快捷键)
// https://atomcode.atomgit.com/docs/zh/keybindings.html
static uint8_t g_keymaps_cfg[] =
{
    HID_KEY_NONE,       // eKey_Released = 0,
    eKey_Mic,           // eKey_Mic,     左Crtl + 左Windows 的组合键
    HID_KEY_A,          // eKey_Always,  Atomcode: 'a': 本次会话内始终允许这种模式
    HID_KEY_ESC,        // eKey_No,      Atomcode: 'n'|'ESC': 拒绝
    HID_KEY_ENTER,      // eKey_Yes,     Atomcode: 'y'|'Enter': 允许这一次
    HID_KEY_PAGE_UP,    // eKey_Up,      Atomcode: 'PageUp': 向上翻一页(10 行)
    HID_KEY_PAGE_DOWN,  // eKey_Down,    Atomcode: 'PageDown': 向下翻一页(10 行)
    eKey_Always_No,     // 未定义
    eKey_Always_Yes,
    eKey_Always_Up,
    eKey_Always_Down,
    eKey_Mic_No,
    eKey_Mic_Yes,
    eKey_Mic_Up,
    eKey_Mic_Down,      // 未定义
    eKey_Max,
};

/* 任一无线链路就绪(SLE Dongle已连接 / BLE主机已连接)即为"已配对可发送" */
static bool kb_any_link_ready(void)
{
    #if defined(SUPPORT_SLE_KEYBOARD)
    if ((get_protocol_type() == eProto_SLE) && (sle_client_get_ready_count() > 0)) {
        return true;
    }
    #endif

    #if defined(SUPPORT_BLE_KEYBOARD)
    if ((get_protocol_type() == eProto_BLE) && ble_hid_host_connected()) {
        return true;
    }
    #endif

    return false;
}

static int dispatch_hid_report(uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len <= 0)) {
        log_error("%s %s: invalid args\r\n", LOG_TAG, __func__);
        return 0;
    }

#if 0  // debug log
    log_debug("%s %s: [%s]=>[%d]:", LOG_TAG, __func__, get_protocol_type()?"BLE":"SLE", len);
    for (uint16_t i=0; i<len; i++) {
        printf("[0x%02X]", data[i]);
    }
    printf("\r\n");
#endif


    #if defined(SUPPORT_SLE_KEYBOARD)
    if ((get_protocol_type() == eProto_SLE) && (sle_client_get_ready_count() > 0)) {
        return sle_client_broadcast_msg(data, len);
    }
    #endif

    #if defined(SUPPORT_BLE_KEYBOARD)
    if ((get_protocol_type() == eProto_BLE) && ble_hid_host_connected()) {
        return (int)ble_hid_send_input_report(data, len);
    }
    #endif

    return 0;
}


/**
 * 发送自定义键位动作（宏）：按配置的步数逐帧发送 HID 报告，
 * 每步 (modifier + keycode)，步间延迟 50ms，最后发送释放帧。
 * 返回 true 表示已发送自定义动作（调用方应跳过默认逻辑）。
 */
static bool send_custom_key_action(uint8_t ekey)
{
    const keymap_action_t *act = keymap_get_action(ekey);
    uint8_t send_buff[KEY_EVNT_REPORT_BUFF_LEN] = {0};

    if ((act == NULL) || (act->step_count == 0)) {
        return false;   // 未自定义，走默认逻辑
    }

    log_info("%s %s: custom key[%2u] step_count[%u]\r\n", LOG_TAG, __func__, ekey, act->step_count);
    for (uint8_t s = 0; s < act->step_count; s++) {
        memset(send_buff, 0, sizeof(send_buff));
        send_buff[0] = act->steps[s].modifier;
        send_buff[2] = act->steps[s].keycode;
        dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
        usleep(50 * 1000);   // 步间延迟，确保PC端按顺序识别
    }
    memset(send_buff, 0, sizeof(send_buff));   // 释放所有按键
    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
    return true;
}

void KeyDispatchTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint8_t msg_recv = 0;

    /*
     * USB HID Keyboard Report (standard 8-byte format):
     * //Byte-1: ignore Kind byte(will set in server)
     *   Byte[0] - Modifier keys:
     *       bit 0: LCtrl, bit 1: LShift, bit 2: LAlt, bit 3: LGUI (Windows)
     *       bit 4: RCtrl, bit 5: RShift, bit 6: RAlt, bit 7: RGUI
     *   Byte[1] - Reserved (0x00)
     *   Byte[2-7] - Key codes (up to 6 simultaneous keys)
     */
    uint8_t send_buff[KEY_EVNT_REPORT_BUFF_LEN] = {0};

    while (1) {
        ret = osMessageQueueGet(g_kdsp_event_qid, (void *)&msg_recv, NULL, 40);
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }

        memset(send_buff, 0, sizeof(send_buff));

        /* 发送自定义键位映射码的条件：已连接+正常模式+自定义模式下，
           若该键位配置了自定义动作，则发送宏并跳过默认 switch 逻辑；
           未自定义时保持原有功能不变。 */
        if ((msg_recv != eKey_Released)          && 
            (msg_recv != eKey_Mic)               &&
            (kb_any_link_ready())                &&
            (eWorkMode_Normal == get_workmode()) &&
            (eKeyMode_Custom  == get_keymode())  &&
            send_custom_key_action(msg_recv)) {
            continue;
        }

        switch(msg_recv) {
            case eKey_Released: {
                send_buff[0] = 0x00;
                dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                break;
            }
            case eKey_Mic: {
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Normal_Page);

                    send_buff[0] |= 0x01;        /* modifier: Left Ctrl (bit 0) */
                    send_buff[0] |= 0x08;        /* modifier: Left GUI (bit 3) */
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                } else {
                    if (eWorkMode_Normal == get_workmode()) {
                        post_msg_event(g_disp_event_qid, eDisp_Show_Normal_Page);
                    } else {
                        post_msg_event(g_disp_event_qid, eDisp_Show_Local_Page);
                    }
                }
                break;
            }
            case eKey_Always:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    send_buff[2] = g_keymaps_cfg[eKey_Always];
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                }
                break;
            }
            case eKey_No:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    send_buff[2] = g_keymaps_cfg[eKey_No];
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_All_Update);
                }
                break;
            }
            case eKey_Yes:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    send_buff[2] = g_keymaps_cfg[eKey_Yes];
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_HiLi_Confirm);
                }
                break;
            }
            case eKey_Up:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    send_buff[2] = g_keymaps_cfg[eKey_Up];
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_HiLi_Up);
                }
                break;
            }
            case eKey_Down:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                    send_buff[2] = g_keymaps_cfg[eKey_Down];
                    dispatch_hid_report(send_buff, KEY_EVNT_REPORT_BUFF_LEN);
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_HiLi_Down);
                }
                break;
            }
            case eKey_Always_No:{
#if defined(SUPPORT_SLE_KEYBOARD)
                if ((get_protocol_type() == eProto_SLE) && (sle_client_get_ready_count() > 0)) {
                    sle_client_conn_slot_t *slots = sle_get_client_slots();
                    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
                        if (slots[i].state != SLE_CLIENT_SLOT_CONNECTED) {
                            continue;
                        }
                        sle_disconnect_remote_device(&slots[i].server_addr);
                    }
                    sle_client_slot_reset(0);
                    sle_clear_server_addr();
                    clear_scan_result();
                    sle_start_scan();
                    post_msg_event(g_disp_event_qid, eDisp_Show_Sle_Pairing_Page);

                    // DisConnect with Dongle, Off LED
                    post_msg_event(g_leds_event_qid, eLed_AllOff);
                }
#endif
#if defined(SUPPORT_BLE_KEYBOARD)
                if ((get_protocol_type() == eProto_BLE) && ble_hid_host_connected()) {
                    /* 断开BLE主机并重启广播, PC端可重新扫描配对 */
                    ble_hid_disconnect_host();
                    ble_hid_restart_adv();
                }
#endif
                if (get_protocol_type() == eProto_SLE) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Sle_Pairing_Page);
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_All_Update);
                }
                break;
            }
            case eKey_Always_Yes:{
                if (kb_any_link_ready() && (eWorkMode_Normal == get_workmode())) {
                } else {
                    post_msg_event(g_disp_event_qid, eDisp_HiLi_Confirm);
                }
                break;
            }
            case eKey_Always_Up:{  // 切换按键的工作模式：正常模式/本地模式
                set_workmode();
                break;
            }
            case eKey_Always_Down:{  // 切换按键的键码映射模式：官方默认模式/用户自定义键码模式
                set_keymode();
                post_msg_event(g_disp_event_qid, eDisp_Bar_Update);
                break;
            }
#if 0  // Mic键不与其他键组合
            case eKey_Mic_No:{
                log_error("%s %s: Sending '[%X]Mic_No' key to Dongle ====\r\n", LOG_TAG, __func__, msg_recv);
                break;
            }
            case eKey_Mic_Yes:{
                log_error("%s %s: Sending '[%X]Mic_Yes' key to Dongle ====\r\n", LOG_TAG, __func__, msg_recv);
                break;
            }
            case eKey_Mic_Up:{
                log_error("%s %s: Sending '[%X]Mic_Up' key to Dongle ====\r\n", LOG_TAG, __func__, msg_recv);
                break;
            }
            case eKey_Mic_Down:{
                log_error("%s %s: Sending '[%X]Mic_Down' key to Dongle ====\r\n", LOG_TAG, __func__, msg_recv);
                break;
            }
#endif
            default: {
                log_error("%s %s: Sending '[%X]Unknown' key to Dongle ====\r\n", LOG_TAG, __func__, msg_recv);
                break;
            }
        }
    }
}

uint32_t KeyDispatchTaskEntry(void)
{
    task_config_t task_conf = {
        "KDSPTask",      // task name
        1024*8,          // stack_size
        24,              // priority
        (void*)KeyDispatchTask  // func
    };

    return create_task(&task_conf, NULL);
}