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

/*
 * 键盘 BLE-HID 应用任务:
 *  - 负责初始化 BLE-HID 服务端(广播/GATT/配对, 见 ble_client.c)
 *  - 监控 BLE 主机(Central, 通常是PC)连接状态, 连接成功/断开时联动显示屏
 *  - 按键消息的"发送/接收"本体在 ble_client.c 的 HID 服务里完成:
 *      发送 = ble_hid_send_input_report()(由 sle_client_kb.c 的回退路由调用)
 *      接收 = 主机写 HID Output Report / Control Point, 经弱回调通知本模块
 *
 * 说明: 键盘与 Dongle 的 SLE 链路保持原样; BLE 作为"无Dongle时的直接蓝牙键盘"
 *       通道使用, 二者共存, 由 sle_client_kb.c 单一发送点按  Dongle优先 路由。
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
#include "ble_client_kb.h"

#define LOG_TAG  "[BleClientTask]"

/* 主机(HID Host)LED 状态位定义(与 ble_client.h 注释一致) */
#define BLE_HOST_LED_NUM_LOCK    0x01
#define BLE_HOST_LED_CAPS_LOCK   0x02
#define BLE_HOST_LED_SCROLL_LOCK 0x04

void BleClientTask(void)
{
    uint8_t host_old = 0xFF;             // 0=断开, 1=连接
    static uint16_t s_unsub_ticks = 0;   // 连接后未收到订阅写的计时(100ms节拍)
    static uint8_t  s_force_cnt = 0;     // 自动恢复尝试次数(上限3次防死循环)

    if (get_protocol_type() != eProto_BLE) {
        log_error("%s %s: protocol_type[!BLE] return\r\n", LOG_TAG, __func__);
        return;
    }
    post_msg_event(g_leds_event_qid, eLed_AllOff);

    if (ble_hid_server_init() != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: BLE-HID server init NG\r\n", LOG_TAG, __func__);
        return;
    }

    while (1) {
        if (get_protocol_type() != eProto_BLE) {
            log_info("%s %s: protocol_type[!BLE] exit\r\n", LOG_TAG, __func__);
            break;
        }

        uint8_t host_now = ble_hid_host_connected() ? 1 : 0;
        if (host_old != host_now) {
            host_old = host_now;
            if (host_now) {
                log_info("%s %s: BLE host CONNECTED\r\n", LOG_TAG, __func__);
                /* 连接成功, 与Dongle配对成功一样: 回到对应工作模式页面 */
                if (eWorkMode_Normal == get_workmode()) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Normal_Page);
                } else if (eWorkMode_Local == get_workmode()) {
                    post_msg_event(g_disp_event_qid, eDisp_Show_Local_Page);
                }
            } else {
                log_info("%s %s: BLE host DISCONNECTED\r\n", LOG_TAG, __func__);
                post_msg_event(g_disp_event_qid, eDisp_All_Update);
            }
            post_msg_event(g_disp_event_qid, eDisp_Bar_Update);
        }

        /* 重连兜底状态机: Windows自动重连用缓存不再写订阅, 栈内通知登记不会触发
           (通知"假成功"而空口无包) → 累计"已连接但无订阅"时间, 按阶梯恢复:
           stage0: 累计3s无订阅写 → 更换本地BLE地址(Windows视作全新设备, 强制重新配对/发现/订阅);
           stage1: 换地址后再累计1s仍无订阅 → 打日志提示在PC端手动重新配对;
           说明1: 计时跨连接累计(断开时不清零) —— 若按单次连接的连续时长计时,
                  连接反复断开(如配对失败重连)时会被反复清零, 阶梯永远无法升级;
           说明2: 曾先 gap_ble_remove_pair 期望Windows重新发起配对, 实测删除键盘侧
                  bond后Windows仍按原地址/原bond重连, 加密失败(配对 status[0x80006000]),
                  形成"连接→配对失败→断开→重连"死循环, 故不再使用, 直接换地址(放弃原绑定) */
        if (ble_hid_ccc_written_since_connect()) {
            s_unsub_ticks = 0;
            s_force_cnt = 0;
        } else if (host_now) {
            if (++s_unsub_ticks >= ((s_force_cnt == 0) ? 30 : 10)) {  /* 30×100ms=3s / 10×100ms=1s */
                s_unsub_ticks = 0;
                if (s_force_cnt == 0) {
                    s_force_cnt++;
                    log_info("%s %s: no CCCD subscribe, change local addr to force re-pair\r\n",
                            LOG_TAG, __func__);
                    (void)ble_hid_change_addr();
                } else if (s_force_cnt == 1) {
                    /* 只提示一次: 换地址后若栈从bond恢复订阅(无CCCD写事件), 按键实际可用,
                       继续每拍打印会刷屏; 静默等待手动重配对, 收到CCCD写即复位 */
                    s_force_cnt++;
                    log_error("%s %s: auto-recovery failed, please re-pair on PC\r\n",
                            LOG_TAG, __func__);
                }
                /* s_force_cnt >= 2: 已提示过, 不再重复打印 */
            }
        }
        usleep(100 * 1000);   // 100ms 轮询连接状态变化
    }

    ble_hid_server_deinit();
    if (get_protocol_type() != eProto_BLE) {
        post_msg_event(g_main_event_qid, (uint8_t)SWITCH_PROTOCOL_TYPE);
    }
}

uint32_t BleClientTaskEntry(void)
{
    task_config_t task_conf = {
        "BCliTask",      // task name
        1024 * 6,        // stack_size
        24,              // priority
        (void *)BleClientTask  // func
    };

    return create_task(&task_conf, NULL);
}
