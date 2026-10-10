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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adc_porting.h"
//#include "cJSON.h"
#include "nv.h"
#include "key_id.h"
#include "nv_common_cfg.h"
#include "sle_common.h"   # SLE_ADDR_LEN

#include "board_config.h"
#include "disp_driver.h"
#include "led_config.h"
#include "settings.h"

// 键盘的工作模式(不落盘保存)
// 0-Normal/正常模式(已配对则将按键发往Dongle)
// 1-Local /本地模式(已配对不将按键发往Dongle，本地处理)
static uint8_t g_work_mode = eWorkMode_Normal;

// 以下是落盘保存的设置项
static uint8_t g_protocol = 0;     // 协议类型：SLE, BLE
static uint8_t g_rotation = 0;     // 对应 LCD_PANEL_ROTATION_180[0]
static uint8_t g_brightness = 20;  // [1, 100], 默认50%, 息屏时不保存0值
static uint8_t g_sleep_time = 10;  // [10, 20, 30, 60, never]
// 键盘的按键映射模式(落盘保存)
// 0-Default/默认模式(按键以官方定义的键码发往Dongle)
// 1-Custom/用户自定义模式(按键以用户自定义的重映射键码发往Dongle)
static uint8_t g_key_mode = eKeyMode_Default;

static uint8_t g_dongle_mac[SLE_ADDR_LEN] = {0};
static uint8_t g_wifi_ssid[WIFI_SSID_LEN_MAX] = {0};
static uint8_t g_wifi_pswd[WIFI_PSWD_LEN_MAX] = {0};
static char    g_hotspot_ssid[WIFI_SSID_LEN_MAX] = "XH-KB_Def";
static char    g_hotspot_pswd[] = "\0";

// BLE本地地址覆盖: 存于NV记录ssid[5](magic) + ssid[6~11](6字节地址), 见read/write_config_from_nv
#define BLE_ADDR_OVERRIDE_LEN   (6)
#define BLE_ADDR_OVERRIDE_MAGIC (0xAA)
static uint8_t g_ble_addr_override[BLE_ADDR_OVERRIDE_LEN] = {0};
static uint8_t g_ble_addr_override_valid = 0;

osStatus_t post_msg_event(osMessageQueueId_t qid, uint8_t evt)
{
    if (qid == NULL) {
        return osErrorParameter;
    }
    for (uint8_t i = 0; i < 3U; i++) {
        if (osMessageQueuePut(qid, (const void *)&evt, 0, 1U) == osOK) {
            return osOK;
        }
    }
    return osError;
}

#if 0
  使用ID[0x1001]的编码对应的结构体来存储用户配置信息, "hilink配网ssid与密码" 结构体字段重定义:
   ssid[0~32]: [0]显示屏的旋转角度
               [1]显示屏亮度
               [2]进入休眠模式的时间间隔
               [3]按键模式：0-默认模式，1-用户自定义模式
               [4]协议类型：0-SLE, 1-BLE
               [5~11]: BLE本地地址覆盖: ssid[5](magic) + ssid[6~11](6字节地址)
               [12~32]: 其它信息
   pwd [0~64]: [0]上次连接成功的ssid长度n, [1~n]ssid字符串，[n+1]ssid字符串结束符'\0'
               [n+2 ~ 63]上次连接成功的ssid对应的密码字符串, [64]pswd字符串结束符'\0'
               1+31+1+31+1=65, WIFI_SSID_LEN_MAX[31]/WIFI_PSWD_LEN_MAX[31]
   bssid[0~5]: 上次连接并配对成功的Dongle的Mac地址
typedef struct {
    uint8_t ssid[WLAN_HILINK_SSID_LEN];  // 33
    uint8_t pwd[WLAN_HILINK_PWD_LEN];    // 65
    uint8_t bssid[WLAN_HILINK_MAC_LEN];  // 6
} hilink_connect_info_t;
#endif
errcode_t read_config_from_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;
    hilink_connect_info_t hl_data = {0};
    uint8_t n = 0;
    uint16_t nvlen = 0;

    memset(g_wifi_ssid, 0, WIFI_SSID_LEN_MAX);
    memset(g_wifi_pswd, 0, WIFI_PSWD_LEN_MAX);
    memset(g_dongle_mac, 0, SLE_ADDR_LEN);

    ret = uapi_nv_read(0x1001, sizeof(hilink_connect_info_t), &nvlen, (uint8_t*)&hl_data);
    if (ret == ERRCODE_SUCC) {
        g_rotation   = hl_data.ssid[0]%4;
        g_brightness = hl_data.ssid[1];
        g_sleep_time = hl_data.ssid[2];
        g_key_mode   = hl_data.ssid[3];
        g_protocol   = hl_data.ssid[4];
        if ((g_brightness == 0) || (g_brightness > 100)) {
            log_error("[Settings] %s: brightness[%d] invalid, set to 50\r\n", __func__, g_brightness);
            g_brightness = 50;
        }
        n = hl_data.pwd[0];
        memcpy_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, &hl_data.pwd[1], n);
        memcpy_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, &hl_data.pwd[n+2], WLAN_HILINK_PWD_LEN-n-2);
        memcpy_s(g_dongle_mac, SLE_ADDR_LEN, hl_data.bssid, WLAN_HILINK_MAC_LEN);

        if (hl_data.ssid[5] == BLE_ADDR_OVERRIDE_MAGIC) {
            g_ble_addr_override_valid = 1;
            memcpy_s(g_ble_addr_override, BLE_ADDR_OVERRIDE_LEN, &hl_data.ssid[6], BLE_ADDR_OVERRIDE_LEN);
        } else {
            g_ble_addr_override_valid = 0;
            memset(g_ble_addr_override, 0, BLE_ADDR_OVERRIDE_LEN);
        }

        log_debug("[Settings] %s: OK\r\n"               \
                  "    rota:[%d]\r\n    disp:[%d]\r\n"  \
                  "   sleep:[%d]\r\n    Kmod:[%d]\r\n"  \
                  "   proto:[%d]\r\n"  \
                  "    ssid:[%s]\r\n    pswd:[%s]\r\n"  \
                  "   bssid:[%02X:%02X:%02X:%02X:%02X:%02X]\r\n", __func__,
                  g_rotation, g_brightness, g_sleep_time, g_key_mode, g_protocol,
                  (char*)g_wifi_ssid, (char*)g_wifi_pswd,
                  g_dongle_mac[0], g_dongle_mac[1], g_dongle_mac[2],
                  g_dongle_mac[3], g_dongle_mac[4], g_dongle_mac[5]);

    } else {
        log_error("[Settings] %s: uapi_nv_read: NG\r\n", __func__);
    }
    return ret;
}

static errcode_t write_config_to_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;
    hilink_connect_info_t hl_data = {0};

    uint8_t n = strlen(g_wifi_ssid);
    uint8_t m = strlen(g_wifi_pswd);

    hl_data.ssid[0] = get_rotation();
    hl_data.ssid[1] = get_blk_level();
    hl_data.ssid[2] = get_sleep_time();
    hl_data.ssid[3] = get_keymode();
    hl_data.ssid[4] = get_protocol_type();

    hl_data.pwd[0] = n;
    memcpy_s(hl_data.pwd+1, WIFI_SSID_LEN_MAX, g_wifi_ssid, n);
    hl_data.pwd[n+1] = '\0';
    memcpy_s(hl_data.pwd+n+2, WLAN_HILINK_PWD_LEN-n-2, g_wifi_pswd, m);
    hl_data.pwd[n+2+m] = '\0';

    memcpy_s(hl_data.bssid, WLAN_HILINK_MAC_LEN, g_dongle_mac, SLE_ADDR_LEN);

    if (g_ble_addr_override_valid) {
        hl_data.ssid[5] = BLE_ADDR_OVERRIDE_MAGIC;
        memcpy_s(&hl_data.ssid[6], WLAN_HILINK_SSID_LEN-6, g_ble_addr_override, BLE_ADDR_OVERRIDE_LEN);
    } else {
        hl_data.ssid[5] = 0;
    }

    ret = uapi_nv_write(0x1001, (uint8_t*)&hl_data, sizeof(hilink_connect_info_t));
    if (ret != ERRCODE_SUCC) {
        log_error("[Settings] %s: uapi_nv_write    : NG\r\n", __func__);
    }
    return ret;
}

uint8_t get_dongle_mac(uint8_t *buff, uint8_t len)
{
    if ((buff == NULL) || (len < SLE_ADDR_LEN)) {
        log_error("[Settings] %s: args invalid\r\n", __func__);
        return 0;
    }
    if (0 == memcpy_s(buff, len, g_dongle_mac, SLE_ADDR_LEN)) {
        return SLE_ADDR_LEN;
    }
    return 0;
}
uint8_t set_dongle_mac(uint8_t *buff, uint8_t len)
{
    if ((buff == NULL) || (len < SLE_ADDR_LEN)) {
        log_error("[Settings] %s: args invalid\r\n", __func__);
        return 0;
    }
    if (0 == memcpy_s(g_dongle_mac, SLE_ADDR_LEN, buff, len)) {
        write_config_to_nv();
        return SLE_ADDR_LEN;
    }

    return 0;
}

uint8_t get_ble_addr_override(uint8_t *buff, uint8_t len)
{
    if ((buff == NULL) || (len < BLE_ADDR_OVERRIDE_LEN) || (!g_ble_addr_override_valid)) {
        return 0;
    }
    if (0 == memcpy_s(buff, len, g_ble_addr_override, BLE_ADDR_OVERRIDE_LEN)) {
        return BLE_ADDR_OVERRIDE_LEN;
    }
    return 0;
}

uint8_t set_ble_addr_override(uint8_t *buff, uint8_t len)
{
    if ((buff == NULL) || (len < BLE_ADDR_OVERRIDE_LEN)) {
        log_error("[Settings] %s: args invalid\r\n", __func__);
        return 0;
    }
    if (0 == memcpy_s(g_ble_addr_override, BLE_ADDR_OVERRIDE_LEN, buff, BLE_ADDR_OVERRIDE_LEN)) {
        g_ble_addr_override_valid = 1;
        write_config_to_nv();
        return BLE_ADDR_OVERRIDE_LEN;
    }
    return 0;
}

// 电量显示
uint8_t get_voltage_level(void)
{
    uint16_t voltage = 0;
    adc_port_read(BAT_LEVEL_ADC_CHN, &voltage);
    // [1000, 1400] -> [5, 100]

    uint8_t volt = (uint8_t)(voltage/14);
    if (voltage <= 1000) {
        volt = 5;
    } else {
        volt = (uint8_t)((voltage-1000)/20*5+5);
    }
    if (volt > 100) {
        volt = 100;
    }

//  log_debug("[Settings] get_voltage_level: volt[%d], voltage[%d]\r\n", volt, voltage);
    return volt;
}

// 键盘端发射协议：SLE/BLE
uint8_t get_protocol_type(void)
{
    return g_protocol;
}
uint8_t set_protocol_type(uint8_t value)
{
    g_protocol = value%2;  // 只支持协议：SLE/BLE

    write_config_to_nv();
    return g_protocol;
}

// 显示屏旋转设置
uint8_t get_rotation(void)
{
    return g_rotation;
}
uint8_t set_rotation(uint8_t value)
{
    g_rotation = value%4;  // 只支持旋转: 0°/90°/180°/270°
    post_msg_event(g_disp_event_qid, eDisp_Set_Rotation);

    write_config_to_nv();
    return g_rotation;
}

// 设置休眠时间间隔
uint8_t get_sleep_time(void)
{
    return g_sleep_time;
}
uint8_t set_sleep_time(uint8_t value)
{
    g_sleep_time = value;

    write_config_to_nv();
    return g_sleep_time;
}

uint8_t get_blk_level(void)
{
    return g_brightness;
}
uint8_t set_blk_level(uint8_t value)
{
    if ((value == 0) || (value > 100)) {
        log_error("[Settings] %s: brightness[%d] invalid, set to 50\r\n", __func__, g_brightness);
        g_brightness = 50;
    } else {
        g_brightness = value;
    }
    disp_set_backlight_level(g_brightness);
    post_msg_event(g_leds_event_qid, eLed_Refresh);

    write_config_to_nv();
    return g_brightness;
}


// 键盘的工作模式
uint8_t get_workmode(void)
{
    return g_work_mode;
}
uint8_t set_workmode(void)
{
    g_work_mode = (g_work_mode+1)%2;

    if (eWorkMode_Normal == g_work_mode) {
        post_msg_event(g_disp_event_qid, eDisp_Show_Normal_Page);
    } else if (eWorkMode_Local == g_work_mode) {
        post_msg_event(g_disp_event_qid, eDisp_Show_Local_Page);
    }

    return g_work_mode;
}

// 键盘的按键映射模式
uint8_t get_keymode(void)
{
    return g_key_mode;
}
uint8_t set_keymode(void)
{
    g_key_mode = (g_key_mode+1)%2;
    write_config_to_nv();
    return g_key_mode;
}
static uint8_t is_keyremap_enable = 0;
uint8_t enable_key_remap(void)
{
    if (is_keyremap_enable) {
        log_error("[Settings] %s: is_keyremap_enable[1] ...\r\n", __func__);
        return 0;
    }
    if (start_hotspot(get_hotspot_name(), get_hotspot_pswd()) != ERRCODE_SUCC) {
        log_error("[Settings] %s: start_hotspot NG\r\n", __func__);
        return 0;
    }
    if (KbConfigServerTaskEntry() == 0) {
        log_error("[Settings] %s: KbConfigServerTaskEntry NG\r\n", __func__);
        stop_hotspot();
        return 0;
    }
    is_keyremap_enable = 1;
    return is_keyremap_enable;
}
void disable_key_remap(void)
{
    if (is_keyremap_enable) {
        stop_hotspot();
        kb_config_server_stop();
        is_keyremap_enable = 0;
    }
    log_debug("[Settings] %s: is_keyremap_enable[0] ...\r\n", __func__);
}

char *get_hotspot_name(void)
{
    //  [00][D8][11][04][97][14] [D1][07][65][19][FA][01][AB] [0A][A1][70]
    //  [00][D8][11][04][97][14] [11][E2][64][19][FA][01][87] [0A][A1][70]
    //  [00][D8][11][04][97][14] [19][47][6A][19][7A][82][9C] [0A][A1][70]
    //  die_id的前6个字节跟后三个字节大概率是相同的，所以取[6,7]两个字节做AP名后缀，
    //  并在 sle_client和ble_client中，使用[6...B]六个字节作为Mac地址，避免地址重复
    uint8_t die_id[16] = {0};
    if (uapi_efuse_get_die_id(die_id, sizeof(die_id)) == ERRCODE_SUCC) {
        memset(g_hotspot_ssid, 0, sizeof(g_hotspot_ssid));
        (void)snprintf(g_hotspot_ssid, sizeof(g_hotspot_ssid)-1, "XH-KB_%02X%02X", die_id[6], die_id[7]);
    }
    log_info("[Settings] get_hotspot_name: [%s]\r\n", g_hotspot_ssid);
    return g_hotspot_ssid;
}
char *get_hotspot_pswd(void)
{
    return g_hotspot_pswd;
}

// Atomcode的状态，不落盘
static uint8_t g_atomcode_status = 0xFF;
char* get_atomcode_status_str(void)
{
    if (g_atomcode_status == eLed_AtomCode_Idle) {
        return "Idle";
    } else if (g_atomcode_status == eLed_AtomCode_Thinking) {
        return "Thinking";
    } else if (g_atomcode_status == eLed_AtomCode_Working) {
        return "Working";
    } else if (g_atomcode_status == eLed_AtomCode_Waiting_Confirm) {
        return "Waiting_Confirm";
    } else if (g_atomcode_status == eLed_AtomCode_Error) {
        return "Error";
    } else {
        return "unKnown";
    }
}
uint8_t get_atomcode_status(void)
{
    return g_atomcode_status;
}
uint8_t set_atomcode_status(uint8_t status)
{
    g_atomcode_status = status;
    return g_atomcode_status;
}