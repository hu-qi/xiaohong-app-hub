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

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#if SUPPORT_OHOS
#include "ohos_init.h"
#else
#include "app_init.h"
#endif
#include "cmsis_os2.h"
#include "soc_osal.h"
#include "errcode.h"

#include "nv.h"
#include "key_id.h"
#include "nv_common_cfg.h"
#include "key_config.h"
#include "log.h"

// 1+31+1+31+1=65, 见read_config_from_nv()的使用说明
#define  WIFI_SSID_LEN_MAX    (31)
#define  WIFI_PSWD_LEN_MAX    (31)

// 低电量告警阈值（电量百分比，见 get_voltage_level()）
#define  BATTERY_LOW_THRESHOLD       (20)

extern osMessageQueueId_t g_main_event_qid;   // 主事件队列
extern osMessageQueueId_t g_kdsp_event_qid;   // KeyDispatch任务事件队列，用于接收按键、解析和分发给SLE/BLE发送给接收端
extern osMessageQueueId_t g_disp_event_qid;   // 显示事件队列，用于接收显示事件
extern osMessageQueueId_t g_leds_event_qid;   // LED灯任务事件队列，用于接受LED灯控制事件
osStatus_t post_msg_event(osMessageQueueId_t qid, uint8_t evt);
#define SWITCH_PROTOCOL_TYPE  (1)

errcode_t read_config_from_nv(void);
// static errcode_t write_config_to_nv(void);

// 键盘的工作模式(不落盘保存)
// 0-Normal/正常模式(已配对则将按键发往Dongle)
// 1-Local /本地模式(已配对不将按键发往Dongle，本地处理)
enum {
    eWorkMode_Normal,
    eWorkMode_Local,
};
uint8_t get_workmode(void);
uint8_t set_workmode(void);

// 键盘的按键映射模式(落盘保存)
// 0-Default/默认模式(按键以官方定义的键码发往Dongle)
// 1-Custom /用户自定义模式(按键以用户自定义的重映射键码发往Dongle)
enum {
    eKeyMode_Default,
    eKeyMode_Custom,
};
uint8_t get_keymode(void);
uint8_t set_keymode(void);

uint8_t get_dongle_mac(uint8_t *buff, uint8_t len);
uint8_t set_dongle_mac(uint8_t *buff, uint8_t len);

uint8_t get_voltage_level(void);

uint8_t get_rotation(void);
uint8_t set_rotation(uint8_t value);
uint8_t get_blk_level(void);
uint8_t set_blk_level(uint8_t value);
uint8_t get_sleep_time(void);
uint8_t set_sleep_time(uint8_t value);

char *get_hotspot_name(void);
char *get_hotspot_pswd(void);
uint8_t enable_key_remap(void);
void disable_key_remap(void);

char* get_atomcode_status_str(void);
uint8_t get_atomcode_status(void);
uint8_t set_atomcode_status(uint8_t status);

enum {
    eProto_SLE,
    eProto_BLE,
};
uint8_t get_protocol_type(void);
uint8_t set_protocol_type(uint8_t value);
// BLE本地地址覆盖(换地址兜底后持久化, 重启后沿用, 避免回到旧地址复发"缓存不订阅"问题)
uint8_t get_ble_addr_override(uint8_t *buff, uint8_t len);
uint8_t set_ble_addr_override(uint8_t *buff, uint8_t len);

#endif//__SETTINGS_H__