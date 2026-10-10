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

#include "log.h"
#include "net_wifi_config.h"

/*****************************************************************************
  Hotspot状态改变事件回调函数
*****************************************************************************/
static td_void wifi_softap_state_changed(int32_t state)
{
    log_debug("[wifi__ap] wifi_softap_state_changed: state[%d][%s]\r\n",
            state, state?"Available":"Unavailable");
}

/*****************************************************************************
  Station连接事件回调函数
*****************************************************************************/
static td_void wifi_softap_sta_join(const wifi_sta_info_stru *info)
{
    log_debug("[wifi__ap] wifi_softap_sta_join: mac_addr[%02X %02X %02X ** ** **]\r\n",
        info->mac_addr[0], info->mac_addr[1], info->mac_addr[2]);
}
/*****************************************************************************
  Station断连事件回调函数
*****************************************************************************/
static td_void wifi_softap_sta_leave(const wifi_sta_info_stru *info)
{
    log_debug("[wifi__ap] wifi_softap_sta_leave: mac_addr[%02X %02X %02X ** ** **]\r\n",
        info->mac_addr[0], info->mac_addr[1], info->mac_addr[2]);
}

wifi_event_stru wifi_ap_event_cb = {
    .wifi_event_softap_state_changed = wifi_softap_state_changed,
    .wifi_event_softap_sta_join      = wifi_softap_sta_join,
    .wifi_event_softap_sta_leave     = wifi_softap_sta_leave,
};

errcode_t start_hotspot(char *ssid, char *pswd)
{
    char ifname[] = "ap0";

    ip4_addr_t st_ipaddr = {0};
    ip4_addr_t st_netmask = {0};
    ip4_addr_t st_gw = {0};
    struct netif *netif_p = TD_NULL;

    (void)pswd;

    log_info("[wifi__ap] start_hotspot: Begin: ssid[%s]\r\n", ssid);
    if (wifi_register_event_cb(&wifi_ap_event_cb) != ERRCODE_SUCC) {
        log_error("[wifi__ap] start_hotspot: wifi_register_event_cb NG\r\n");
    }

    IP4_ADDR(&st_gw, 192, 168, 1, 1);       /* 网关地址设置：192.168.1.1 */
    IP4_ADDR(&st_ipaddr, 192, 168, 1, 1);   /* IP地址设置：192.168.1.1 */
    IP4_ADDR(&st_netmask, 255, 255, 255, 0);

    /* 配置SoftAp基本参数 */
    softap_config_stru ap_base_config = {0};
    memcpy_s(ap_base_config.ssid, sizeof(ap_base_config.ssid), ssid, strlen(ssid));
    ap_base_config.security_type = WIFI_SEC_TYPE_OPEN;   /* 开放热点，方便手机/PC直连 */
    ap_base_config.channel_num = HOTSPOT_CHANNEL;

    /* 启动SoftAp接口 */
    while (wifi_softap_enable(&ap_base_config) != ERRCODE_SUCC) {
        log_error("[wifi__ap] start_hotspot: wifi_softap_enable NG, retry\r\n");
        osal_msleep(100);
    }

    log_info("[wifi__ap] start_hotspot: wifi_softap_enable OK\r\n");

    /* 配置DHCP服务器 */
    netif_p = netif_find(ifname);
    if (netif_p == TD_NULL) {
        log_error("[wifi__ap] start_hotspot: netif_find NG\r\n");
        wifi_softap_disable();
        return ERRCODE_FAIL;
    }

    if (netifapi_netif_set_addr(netif_p, &st_ipaddr, &st_netmask, &st_gw) != ERRCODE_SUCC) {
        log_error("[wifi__ap] start_hotspot: netifapi_netif_set_addr NG\r\n");
        wifi_softap_disable();
        return ERRCODE_FAIL;
    }

    if (netifapi_dhcps_start(netif_p, NULL, 0) != ERRCODE_SUCC) {
        log_error("[wifi__ap] start_hotspot: netifapi_dhcps_start NG\r\n");
        wifi_softap_disable();
        return ERRCODE_FAIL;
    }

    log_info("[wifi__ap] start_hotspot: End. OK\r\n");
    return ERRCODE_SUCC;
}

errcode_t stop_hotspot(void)
{
    errcode_t ret = ERRCODE_SUCC;

    log_info("[wifi__ap] stop_hotspot: Begin\r\n");
    if (wifi_softap_disable() != ERRCODE_SUCC) {
        log_error("[wifi__ap] stop_hotspot: wifi_softap_disable NG\r\n");
        ret = ERRCODE_FAIL;
    }
    log_info("[wifi__ap] stop_hotspot: End\r\n");
    return ret;
}
