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

#ifndef __NET_WIFI_CONFIG_H__
#define __NET_WIFI_CONFIG_H__

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#if SUPPORT_OHOS
#include "ohos_init.h"
#else
#include "app_init.h"
#endif
#include "cmsis_os2.h"

#include "lwip/netifapi.h"
#include "lwip/nettool/misc.h"
#include "soc_osal.h"
#include "errcode.h"
#include "wifi_device.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"
#include "wifi_linked_info.h"
#include "wifi_device_config.h"
#include "td_base.h"
#include "td_type.h"
#include "wifi_event.h"
#include "settings.h"

#define HOTSPOT_SEC_TYPE_PSK             2
#define HOTSPOT_CHANNEL                  7

//#define WIFI_IFNAME_MAX_SIZE             64
#define WIFI_SCAN_AP_MAX_COUNT           3
#define WIFI_SCAN_AP_LIMIT               64
#define WIFI_MAC_LEN                     6
#define WIFI_NOT_AVAILIABLE              0
#define WIFI_AVAILIABLE                  1
#define WIFI_CONN_STATUS_MAX_GET_TIMES   10   /* 获取连接信息最大尝试次数 */
#define DHCP_BOUND_STATUS_MAX_GET_TIMES  30   /* 获取绑定状态最大尝试次数(500ms/次) */
#define WIFI_STA_IP_MAX_GET_TIMES        10   /* 获取IP最大尝试次数 */
#define WIFI_STA_ENABLE_MAX_COUNT        300

enum {
    eWifi_St_DisConnected = 0,  /* 0:初始态/未连接 */
    eWifi_St_AutoConnecting,    /* 1:上电后自动连接中 */
    eWifi_St_ReConfiguring,     /* 2:重新配网连接中 */
    eWifi_St_Connected,         /* 3:已连接 */
};

enum {
    eWifi_Config_Initiate = 0,
    eWifi_Config_Success,
    eWifi_Config_Failure,
};

char* get_hotspot_name(void);

uint8_t get_wifi_status(void);
errcode_t start_hotspot(char* ssid, char* pswd);
errcode_t connect_to_hotspot(char* ssid, char* pswd);
errcode_t disconnect_with_hotspot(void);

/**
 * 等待 STA 具备外网 TCP 条件（IP + DHCP 已绑定，且默认网关非 0）。
 * 避免刚关联 AP 或 DHCP 刚返回时立刻 connect，lwIP 报 connect: 101（ENETUNREACH 等）。
 */
errcode_t wifi_sta_wait_ready_for_tcp(uint32_t timeout_ms);
errcode_t get_wifi_scan_result(char* buff, uint16_t buff_len);

uint32_t WiFiTaskEntry(void);
uint32_t WiFiReConfgTaskEntry(void);
uint32_t HttpServerTaskEntry(void);
#define WIFI_CONFIG_SUCCESS_EVENT        (0x01<<0)
#define WIFI_CONFIG_FAILURE_EVENT        (0x02<<0)
#define WIFI_CONFIG_INITIATE_EVENT       (0x04<<0)
#define WIFI_CONFIG_INITIATE_TIMEOUT     (1*60*100)   // 1分钟
#define WIFI_CONFIG_INITIATE_RETRY       (3)          // 3次重试
#define WIFI_CONFIG_STA_CONNECT_DELAY_MS (800)        /* POST 长连接就绪后再连 STA */
#define WIFI_CONFIG_STA_RESTORE_AP_MS    (1000)       /* 验证成功后断开 STA，等待 SoftAP 恢复 */
#define WIFI_CONFIG_NOTIFY_WAIT_TIMEOUT  (10*1000)    /* 等手机收到结果再关热点 */
#define WIFI_CONFIG_CONNECTING_EVENT     (0x08<<0)
#define WIFI_CONFIG_NOTIFY_DONE_EVENT    (0x10<<0)    /* HTTP 已向手机回写结果 */
#define WIFI_CONFIG_OTA_EVENT            (0x20<<0)

#endif  // __NET_WIFI_CONFIG_H__