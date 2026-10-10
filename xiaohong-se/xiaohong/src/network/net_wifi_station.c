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

#include "settings.h"
#include "net_wifi_config.h"

#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

struct netif * g_iface = NULL;

errcode_t wifi_sta_wait_ready_for_tcp(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step = 100U;

    while (elapsed < timeout_ms) {
        if (g_iface != NULL && g_iface->ip_addr.u_addr.ip4.addr != 0U &&
            netifapi_dhcp_is_bound(g_iface) == ERR_OK) {
            /* 外网目标需默认路由；仅同网段且无网关时可能长期为 0，后面兜底仍尝试 OTA */
            if (g_iface->gw.u_addr.ip4.addr != 0U) {
                printf("[wifi_sta] wait_tcp: ready (ip+dhcp+gw) elapsed[%u]ms\r\n", elapsed);
                return ERRCODE_SUCC;
            }
        }
        osal_msleep(step);
        elapsed += step;
    }

    if (g_iface != NULL && g_iface->ip_addr.u_addr.ip4.addr != 0U &&
        netifapi_dhcp_is_bound(g_iface) == ERR_OK) {
        printf("[wifi_sta] wait_tcp: timeout but ip+dhcp ok (gw may be 0)\r\n");
        return ERRCODE_SUCC;
    }

    printf("[wifi_sta] wait_tcp: fail timeout=%ums\r\n", (unsigned)timeout_ms);
    return ERRCODE_FAIL;
}

/*****************************************************************************
  STA 扫描事件回调函数
*****************************************************************************/
static td_void wifi_scan_state_changed(td_s32 state, td_s32 size)
{
    (void)state;
    (void)size;
}

/*****************************************************************************
  STA 关联事件回调函数
*****************************************************************************/
extern osEventFlagsId_t net_cfg_evt_id;
static td_void wifi_connection_changed(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason_code)
{
    (void)info;
    (void)reason_code;

    if (state == WIFI_NOT_AVAILIABLE) {
        printf("[wifi_sta] wifi_connection_changed: DisConnected\r\n");
    } else {
        printf("[wifi_sta] wifi_connection_changed: Connected\r\n");
    }
}

wifi_event_stru wifi_sta_event_cb = {
    .wifi_event_connection_changed = wifi_connection_changed,
    .wifi_event_scan_state_changed = wifi_scan_state_changed,
};


errcode_t disconnect_with_hotspot(void)
{
    errcode_t ret = ERRCODE_FAIL;
    if (g_iface) {
        ret = netifapi_dhcp_stop(g_iface);
        g_iface = NULL;
    }
    ret = wifi_sta_disconnect();
    ret = wifi_sta_disable();
    return ret;
}

/*STAl链接目标AP
    param1:待连接的网络名称
    param2:待连接的网络接入密码
    param3:wifi sta配置结构体
*/
errcode_t get_match_network(char *expected_ssid,
                            char *pswd,
                            wifi_sta_config_stru *expected_bss)
{
    uint32_t num = WIFI_IFNAME_MAX_SIZE; /* 64:可以扫描到的Wi-Fi网络数量 */
    uint32_t i = 0;

    /* 获取扫描结果 */
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

    /* 筛选扫描到的Wi-Fi网络，选择待连接的网络 */
    for (i = 0; i < num; i++) {
        if (strlen(expected_ssid) == strlen(result[i].ssid)) {
            if (memcmp(expected_ssid, result[i].ssid, strlen(expected_ssid)) == 0) {
                break;
            }
        }
    }

    /* 未找到待连接AP,可以继续尝试扫描或者退出 */
    if (i >= num) {
        osal_kfree(result);
        return ERRCODE_FAIL;
    }

    /* 找到网络后复制网络信息和接入密码 */
    if (memcpy_s(expected_bss->ssid, WIFI_SSID_LEN_MAX, result[i].ssid, WIFI_SSID_LEN_MAX) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    if (memcpy_s(expected_bss->bssid, WIFI_MAC_LEN, result[i].bssid, WIFI_MAC_LEN) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->security_type = result[i].security_type;
    if (memcpy_s(expected_bss->pre_shared_key, WIFI_MAX_KEY_LEN, pswd, strlen(pswd)) != EOK) {
        osal_kfree(result);
        return ERRCODE_MEMCPY;
    }
    expected_bss->ip_type = DHCP; /* IP类型为动态DHCP获取 */

    osal_kfree(result);
    return ERRCODE_SUCC;
}

errcode_t connect_to_hotspot(char* ssid, char* pswd)
{
    /*WiFi sta 网络设备名*/
    char ifname[] = "wlan0";
    wifi_sta_config_stru expected_bss = {0};
    wifi_linked_info_stru wifi_status;

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

    uint8_t retry = WIFI_SCAN_AP_MAX_COUNT;
    while (retry > 0) {
        retry--;
        /* start scan*/
        osal_msleep(1000);
        printf("[wifi_sta] connect_to_hotspot: wifi_sta_scan:\r\n");
        if (wifi_sta_scan() != ERRCODE_SUCC) {
            printf("[wifi_sta] connect_to_hotspot: wifi_sta_scan NG, retry[%d]\r\n", retry);
            continue;
        }
        if (wifi_register_event_cb(&wifi_sta_event_cb) != ERRCODE_SUCC){
            printf("[wifi_sta] connect_to_hotspot: wifi_register_event_cb NG, retry[%d]\r\n", retry);
            continue;
        }

        /* wait scan finish*/
        osal_msleep(2000);
        if (get_match_network(ssid, pswd, &expected_bss) != ERRCODE_SUCC) {
            printf("[wifi_sta] connect_to_hotspot: get_match_network NG, retry[%d]\r\n", retry);
            continue;
        }

        printf("[wifi_sta] connect_to_hotspot: wifi_sta_connect[%s]\r\n", (char*)expected_bss.ssid);
        if (wifi_sta_connect(&expected_bss) != ERRCODE_SUCC) {
            printf("[wifi_sta] connect_to_hotspot: wifi_sta_connect[%s] NG, retry[%d]\r\n", (char*)expected_bss.ssid, retry);
            continue;
        }

        for (uint8_t i = 0; i < WIFI_CONN_STATUS_MAX_GET_TIMES; i++) {
            osal_msleep(500);
            memset_s(&wifi_status, sizeof(wifi_linked_info_stru), 0, sizeof(wifi_linked_info_stru));
            if (wifi_sta_get_ap_info(&wifi_status) != ERRCODE_SUCC) {
                continue;
            }
            if (wifi_status.conn_state == WIFI_CONNECTED) {
            #if 1  //for ReConfiguring WiFi
                if (get_wifi_status() == eWifi_St_ReConfiguring) {
                    osEventFlagsSet(net_cfg_evt_id, WIFI_CONFIG_SUCCESS_EVENT);
                    disconnect_with_hotspot();
                    return ERRCODE_SUCC;
                }
            #endif
                break;
            }
        }

        if (wifi_status.conn_state == WIFI_CONNECTED) {
            break; 
        }
    }

    if ((retry == 0) && (wifi_status.conn_state != WIFI_CONNECTED)) {
        disconnect_with_hotspot();
        printf("[wifi_sta] connect_to_hotspot: timeout End. STA Disconnected\r\n");
        return ERRCODE_FAIL;
    }

    /* get DHCP ip */
    printf("[wifi_sta] connect_to_hotspot: DHCP start ...\r\n");
    g_iface = netifapi_netif_find(ifname);
    if (g_iface == NULL) {
        printf("[wifi_sta] connect_to_hotspot: netifapi_netif_find NG\r\n");
        return ERRCODE_FAIL;
    }

    // start DHCP server
    if (netifapi_dhcp_start(g_iface) != ERR_OK) {
        printf("[wifi_sta] connect_to_hotspot: netifapi_dhcp_start NG\r\n");
        return ERRCODE_FAIL;
    }

    /* dhcp_is_bound 先于 netif->ip_addr 置位；勿在已 bound 后再次 dhcp_start，否则会清空 IP */
    for (uint8_t i = 0; i < DHCP_BOUND_STATUS_MAX_GET_TIMES; i++) {
        osal_msleep(500);
        if (netifapi_dhcp_is_bound(g_iface) != ERR_OK) {
            continue;
        }
        if (g_iface->ip_addr.u_addr.ip4.addr == 0U) {
            continue;
        }

        printf("[wifi_sta] connect_to_hotspot: netifapi_dhcp_is_bound OK\r\n");
        netifapi_netif_common(g_iface, dhcp_clients_info_show, NULL);
        set_wifi_ip((char*)ip4addr_ntoa(&g_iface->ip_addr.u_addr.ip4));
        printf("[wifi_sta] connect_to_hotspot: End. STA Connected.\r\n");
        return ERRCODE_SUCC;
    }

    printf("[wifi_sta] connect_to_hotspot: End. STA Disonnected\r\n");
    return ERRCODE_FAIL;
}
