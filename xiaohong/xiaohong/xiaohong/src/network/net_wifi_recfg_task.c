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

#include "task_entry.h"
#include "settings.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "net_wifi_config.h"

osEventFlagsId_t net_cfg_evt_id = NULL;
char strSSID[WIFI_SSID_LEN_MAX] = {0};
char strPSWD[WIFI_PSWD_LEN_MAX] = {0};
char strOTAA[OTA_ADDR_LEN_MAX] = {0};   // OTA地址

char* scan_ssid_list = NULL;
errcode_t get_wifi_scan_result(char* buff, uint16_t buff_len)
{
    char ifname[] = "wlan0";  /* WiFi sta 网络设备名 */

    if ((buff == NULL) || (buff_len == 0)) {
        return ERRCODE_FAIL;
    }

    if (wifi_is_sta_enabled() != 1) {
        uint16_t timeout = WIFI_STA_ENABLE_MAX_COUNT;
        while (wifi_sta_enable() != ERRCODE_SUCC) {
            osal_msleep(100);
            timeout--;
            if (timeout == 0) {
                return ERRCODE_FAIL;
            }
        }
    }

    printf("[RCfgTask] get_wifi_scan_result: wifi_sta_scan:\r\n");
    if (wifi_sta_scan() != ERRCODE_SUCC) {
        printf("[RCfgTask] get_wifi_scan_result: wifi_sta_scan NG\r\n");
    }
    osal_msleep(4000);  /* wait scan finish*/

    /* 获取扫描结果 */
    uint32_t num = WIFI_SCAN_AP_LIMIT; /* 64:可以扫描到的Wi-Fi网络数量 */
    uint32_t scan_len = sizeof(wifi_scan_info_stru) * num;
    wifi_scan_info_stru *result = osal_kmalloc(scan_len, OSAL_GFP_ATOMIC);
    if (result == NULL) {
        return ERRCODE_MALLOC;
    }
    memset_s(result, scan_len, 0, scan_len);
    if (wifi_sta_get_scan_info(result, &num) != ERRCODE_SUCC) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    // 筛选扫描到的Wi-Fi网络，选择待连接的网络按["A","B","C","D"]格式保存到buff中
    uint8_t count = 0;
    uint16_t rem_len = buff_len;
    char* p_buff = buff;
    for (uint8_t i = 0; i < num; i++) {
        if (strlen(result[i].ssid) == 0) {
            continue;
        }
        if ((rem_len <= 0) || (count >= SCAN_SSID_LIST_COUNT)) {
            break;
        }
        snprintf(p_buff, rem_len, "\"%s\",", result[i].ssid);
        rem_len = rem_len - strlen(result[i].ssid) - 3;  // 3: "",
        p_buff  = p_buff + strlen(result[i].ssid) + 3;
        count++;
    }
    if (strlen(buff) > 0) {
        buff[strlen(buff)-1] = '\0'; // 去掉最后一个多余逗号
    }
    osal_kfree(result);
    return ERRCODE_SUCC;
}

void WiFiReConfgTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint8_t  net_ok = 0;
    uint32_t flag = 0;
    net_cfg_evt_id = osEventFlagsNew(NULL);

    if (get_wifi_status() == eWifi_St_Connected) {
        ret = disconnect_with_hotspot();
    }

# if 1 // 扫描WiFi网络列表给HTTP服务器使用
    uint16_t list_len = SCAN_SSID_LIST_COUNT*WIFI_SSID_LEN_MAX;
    scan_ssid_list = osal_kmalloc(list_len, OSAL_GFP_ATOMIC);
    if (scan_ssid_list != NULL) {
        memset_s(scan_ssid_list, list_len, 0, list_len);
        get_wifi_scan_result(scan_ssid_list, list_len);
    }
#endif

    set_wifi_status(eWifi_St_ReConfiguring);
    ret = start_hotspot(get_hotspot_name(), get_hotspot_pswd());

    uint32_t http_server_task_id = HttpServerTaskEntry();
    if (http_server_task_id == 0) {
        printf("[RCfgTask] WiFiReConfgTask: HttpServerTaskEntry failed\r\n");
        wifi_softap_disable();
        set_wifi_status(eWifi_St_DisConnected);
        if (scan_ssid_list != NULL) {
            osal_kfree(scan_ssid_list);
            scan_ssid_list = NULL;
        }
        osThreadExit();
        return;
    }

    uint8_t count = 0;
    while (count < WIFI_CONFIG_INITIATE_RETRY) {  // 3
        count ++;
        flag = osEventFlagsWait(net_cfg_evt_id,
                                WIFI_CONFIG_INITIATE_EVENT,
                                osFlagsWaitAll,
                                WIFI_CONFIG_INITIATE_TIMEOUT);
        if (flag == WIFI_CONFIG_INITIATE_EVENT) {
            osEventFlagsSet(net_cfg_evt_id, WIFI_CONFIG_CONNECTING_EVENT);
            osal_msleep(WIFI_CONFIG_STA_CONNECT_DELAY_MS);
            ret = connect_to_hotspot(strSSID, strPSWD);
            if (ret == ERRCODE_SUCC) {
                net_ok = 1;
                break;
            } else {
                osEventFlagsSet(net_cfg_evt_id, WIFI_CONFIG_FAILURE_EVENT);
            }
        }
    }

    (void)osEventFlagsWait(net_cfg_evt_id,
                           WIFI_CONFIG_NOTIFY_DONE_EVENT,
                           osFlagsWaitAll,
                           WIFI_CONFIG_NOTIFY_WAIT_TIMEOUT);
    wifi_softap_disable();
    if (strOTAA[0] != '\0') {
        printf("[RCfgTask] WiFiReConfgTask: set ota address[%s]\r\n", strOTAA);
        set_ota_address(strOTAA);
    }
    if (net_ok) {
        printf("[RCfgTask] WiFiReConfgTask: connect to ssid[%s] OK, restore to nv\r\n", strSSID);
        set_wifi_config(strSSID, WIFI_SSID_LEN_MAX, strPSWD, WIFI_PSWD_LEN_MAX);
        set_wifi_status(eWifi_St_Connected);
    } else {
        printf("[RCfgTask] WiFiReConfgTask: connect to ssid[%s] NG, retry count[%d] timeout\r\n", strSSID, count);
        set_wifi_status(eWifi_St_DisConnected);
    }

    if (scan_ssid_list != NULL) {
        osal_kfree(scan_ssid_list);
        scan_ssid_list = NULL;
    }
    printf("[RCfgTask] WiFiReConfgTask: osThreadExit\r\n");
    osThreadExit();
}

uint32_t WiFiReConfgTaskEntry(void)
{
    if ((get_wifi_status() == eWifi_St_AutoConnecting) || 
        (get_wifi_status() == eWifi_St_ReConfiguring)) {
        printf("[WiFiReConfgTaskEntry] already in reconfiguring\r\n");
        return 0;
    }

    task_config_t task_conf = {
        "RCfgTask",             // task name
        1024*4,                 // stack_size
        24,                     // priority
        (void*)WiFiReConfgTask  // func
    };

    return create_task(&task_conf, NULL);
}
// SYS_RUN(WiFiReConfgTaskEntry);
