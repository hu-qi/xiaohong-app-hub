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

#include "board_config.h"
#include "settings.h"
#include "agent_state.h"
#include "audio_config.h"
#include "net_wifi_config.h"
#include "adc_porting.h"
#include "cJSON.h"
#include "ota.h"

static uint8_t g_local_state = eSysSt_UNKNOWN;
static uint8_t g_peer_state = eSysSt_UNKNOWN;

static uint8_t g_dev_id[DEVICE_ID_LEN] = {0};
static char    g_dev_id_str[DEVICE_ID_STR_LEN] = "\0";

static int32_t g_ver_p4_int   = 0x00000000;
#ifdef FW_VERSION
static int32_t g_ver_ws63_int = FW_VERSION;
#else
static int32_t g_ver_ws63_int = 0x00000000;
#endif
static int32_t g_ver_font_int = 0x00000000;
static char g_ver_p4_str[VERSION_STR_LEN]   = "0.0.0";  // len[5,8]
static char g_ver_ws63_str[VERSION_STR_LEN] = "0.0.0";  // len[5,8]
static char g_ver_font_str[VERSION_STR_LEN] = "0.0.0";  // len[5,8]

static char g_websocket_server_url[WEBSOCKET_SERVER_URL_MAX] = {0};
static char g_websocket_server_token[WEBSOCKET_SERVER_TOKEN_MAX] = {0};
static int  g_websocket_protocol_version = 0;


static char    g_hotspot_ssid[WIFI_SSID_LEN_MAX + 1] = {0};
static char    g_hotspot_pswd[] = "12345678";

static uint8_t g_wifi_ssid[WIFI_SSID_LEN_MAX+1] = "xhws63";
static uint8_t g_wifi_pswd[WIFI_PSWD_LEN_MAX+1] = "12344321";
static uint8_t g_wifi_state = eWifi_St_DisConnected;
static char g_wifi_ip[IP_ADDR_LEN_MAX] = {0};


char g_ota_address[OTA_ADDR_LEN_MAX] = {0};
char g_ota_origin[OTA_ADDR_LEN_MAX] = {0};
static char g_ota_firmware_address[OTA_ADDR_LEN_MAX] = {0};
static char g_ota_fw_ver_pending[64] = {0};
static uint32_t g_ota_fw_size_pending = 0U;
static char g_ota_fw_sha256_pending[65] = {0};

/** 默认 0：auto（AutoStop）；1：realtime。仅配网页 POST 与 MainTask 长按唤醒(＜5s) 可调，见 config.json `voice_interrupt` */
static uint8_t g_voice_interrupt_enabled = 0U;

#if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
static int32_t g_sensors_temp = 0;
static int32_t g_sensors_humi = 0;

int32_t get_sensors_temp(void)
{
    return g_sensors_temp;
}
int32_t get_sensors_humi(void)
{
    return g_sensors_humi;
}
void set_sensors_temp(int32_t temp)
{
    g_sensors_temp = temp;
}
void set_sensors_humi(int32_t humi)
{
    g_sensors_humi = humi;
}
#endif




void restart_system(uint8_t delay_seconds)
{
    if (delay_seconds > 0) {
        sleep(delay_seconds);
    }
    cpu_utils_set_system_status_by_cause(0);
    hal_reboot_chip();
}

osStatus_t post_msg_event(osMessageQueueId_t qid, uint16_t evt)
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

uint8_t get_local_status(void)
{
    return g_local_state;
}
uint8_t set_local_status(uint8_t st)
{
    if (g_local_state != st) {
        g_local_state = st;
        post_msg_event(g_63tx_event_qid, 0x0A03);
    }
    return g_local_state;
}
uint8_t get_peer_status(void)
{
    return g_peer_state;
}
uint8_t set_peer_status(uint8_t st)
{
    g_peer_state = st;
    return g_peer_state;
}

uint8_t set_device_id(uint8_t* id_buff, uint8_t set_len)
{
    if ((id_buff == NULL) || (set_len == 0)) {
        log_error("[Settings] set_device_id: (empty)\r\n");
        return 0;
    } else {
        uint8_t len = (set_len <= DEVICE_ID_LEN)?set_len:DEVICE_ID_LEN;
        if (memcpy_s(&g_dev_id[DEVICE_ID_LEN-len], len, id_buff, len) == 0) {
        #if 0
            printf("[i][Settings] set_device_id: [%d][", len);
            for (uint8_t i = DEVICE_ID_LEN-len; i < DEVICE_ID_LEN; i++) {
                printf("%02X ", g_dev_id[i]);
            }
            printf("]\r\n");  //  41 50 33 59 32 33 33 02
        #endif
            return len;
        } else {
            log_error("[Settings] set_device_id: failed\r\n");
            return 0;
        }
    }
}
uint8_t get_device_id(uint8_t* id_buff, uint8_t get_len)
{
    if ((id_buff == NULL) || (get_len == 0)) {
        return 0;
    } else {
        uint8_t len = (get_len <= DEVICE_ID_LEN)?get_len:DEVICE_ID_LEN;
        if (memcpy_s(id_buff, len, &g_dev_id[DEVICE_ID_LEN-len], len) == 0) {
            return len;
        } else {
            return 0;
        }
    }
}
char* get_device_id_str(void)
{
    /* DEVICE_ID_STR_LEN=18 → 最多 "XX:XX:XX:XX:XX:XX\0"（6 字节 Flash ID） */
    uint8_t len = (uint8_t)(DEVICE_ID_STR_LEN / 3U);
    if (len > DEVICE_ID_LEN) {
        len = DEVICE_ID_LEN;
    }
    if (len == 0U) {
        g_dev_id_str[0] = '\0';
        log_error("[Settings] get_device_id_str: (empty)\r\n");
        return g_dev_id_str;
    }

    const uint8_t *src = &g_dev_id[DEVICE_ID_LEN - len];
    size_t pos = 0U;
    for (uint8_t i = 0U; i < len; i++) {
        if (i > 0U) {
            if (pos + 1U >= DEVICE_ID_STR_LEN) {
                break;
            }
            g_dev_id_str[pos++] = ':';
        }
        int w = snprintf(g_dev_id_str + pos, DEVICE_ID_STR_LEN - pos, "%02X", (unsigned int)src[i]);
        if (w <= 0 || (size_t)w >= DEVICE_ID_STR_LEN - pos) {
            break;
        }
        pos += (size_t)w;
    }
    if (pos < DEVICE_ID_STR_LEN) {
        g_dev_id_str[pos] = '\0';
    } else {
        g_dev_id_str[DEVICE_ID_STR_LEN - 1U] = '\0';
    }

    //log_debug("[Settings] get_device_id_str: [%s]\r\n", g_dev_id_str);
    return g_dev_id_str;
}


/* 将 "major.minor.patch" 转为 int，例如 "1.2.13" -> 0x0001020D，各段按 0~255 占一个字节；解析失败返回 0 */
static int convert_version_str_to_int(char *version)
{
    int major = 0, minor = 0, patch = 0;
    char *ver = version;
    char *end = NULL;
    int32_t v;
    if (ver == NULL || *ver == '\0') {
        log_error("[Settings] %s: error 1\r\n", __func__);
        return 0;
    }
    /* major */
    v = strtol(ver, &end, 10);
    if (end == ver || *end != '.' || v < 0 || v > 255) {
        log_error("[Settings] %s: error 2: %s\r\n", __func__, end);
        return 0;
    }
    major = (int32_t)v;
    ver = end + 1;
    /* minor */
    v = strtol(ver, &end, 10);
    if (end == ver || *end != '.' || v < 0 || v > 255) {
        log_error("[Settings] %s: error 3: %s\r\n", __func__, end);
        return 0;
    }
    minor = (int32_t)v;
    ver = end + 1;
    /* patch */
    v = strtol(ver, &end, 10);
    if (end == ver || v < 0 || v > 255) {
        log_error("[Settings] %s: error 4: %s\r\n", __func__, end);
        return 0;
    }
    patch = (int32_t)v;
    /* 允许末尾仅有空白 */
    while (end && *end && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end != '\0') {
        log_error("[Settings] %s: error 5: %s\r\n", __func__, end);
        return 0;
    }

    v = (major << 16) | (minor << 8) | patch;
    //log_debug("[Settings] %s: version[%s] -> [0x%06X]\r\n", __func__, version, v);
    return v;
}
static char* convert_version_int_to_str(char *version, int ver)
{
    memset(version, 0, VERSION_STR_LEN);
    snprintf(version, VERSION_STR_LEN-1, "%d.%d.%d", (ver>>16)&0xFF, (ver>>8)&0xFF, (ver>>0)&0xFF);
    //log_debug("[Settings] %s: [0x%06X] -> version[%s]\r\n", __func__, ver, version);
    return version;
}
int check_version_int(int ver_local, int ver_remote)
{
    int ret = 0;
    ret = ver_remote > ver_local ? 1 : 0;

    log_info("[Settings] check_version_int: local[0x%06X] vs. remote[0x%06X] >> ret[%d]\r\n",
        ver_local, ver_remote, ret);

    return ret;
}
int check_version_str(char* ver_local, char* ver_remote)
{
    int local  = convert_version_str_to_int(ver_local);
    int remote = convert_version_str_to_int(ver_remote);
    int ret = 0;
    ret = remote > local ? 1 : 0;

    log_info("[Settings] check_version_str: local[%s] vs. remote[%s] >> ret[%d]\r\n",
        ver_local, ver_remote, ret);

    //return ret;
    return 0;  // 先固定返回0，不做ota升级检查
}

errcode_t set_version_p4(char* version)
{
    g_ver_p4_int = convert_version_str_to_int(version);
    convert_version_int_to_str(g_ver_p4_str, g_ver_p4_int);
    log_info("[Settings] set_version_p4: version[%s] -> [0x%06X]\r\n", version, g_ver_p4_int);
    return ERRCODE_SUCC; 
}
int get_version_p4(void)
{
    return g_ver_p4_int;
}
char* get_version_p4_str(void)
{
    convert_version_int_to_str(g_ver_p4_str, get_version_p4());
    return g_ver_p4_str;
}

errcode_t set_version_ws63(char* version)
{
    g_ver_ws63_int = convert_version_str_to_int(version);
    convert_version_int_to_str(g_ver_ws63_str, g_ver_ws63_int);
    log_info("[Settings] set_version_ws63: version[%s] -> [0x%06X]\r\n", version, g_ver_ws63_int);
    return ERRCODE_SUCC; 
}
int get_version_ws63(void)
{
    return g_ver_ws63_int;
}
char* get_version_ws63_str(void)
{
    convert_version_int_to_str(g_ver_ws63_str, get_version_ws63());
    return g_ver_ws63_str;
}

errcode_t set_version_font(char* version)
{
    g_ver_font_int = convert_version_str_to_int(version);
    convert_version_int_to_str(g_ver_font_str, g_ver_font_int);
    log_info("[Settings] set_version_font: version[%s] -> [0x%06X]\r\n", version, g_ver_font_int);
    return ERRCODE_SUCC; 
}
int get_version_font(void)
{
    return g_ver_font_int;
}
char* get_version_font_str(void)
{
    convert_version_int_to_str(g_ver_font_str, get_version_font());
    return g_ver_font_str;
}

errcode_t set_websocket_server_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        g_websocket_server_url[0] = '\0';
        return ERRCODE_SUCC;
    }
    (void)snprintf(g_websocket_server_url, sizeof(g_websocket_server_url), "%s", url);

    post_msg_event(g_63tx_event_qid, 0x0B08);  // WS63 SET_WS_SERVER_URL
    return ERRCODE_SUCC;
}

errcode_t get_websocket_server_url(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0U) {
        return ERRCODE_FAIL;
    }
    (void)snprintf(buf, cap, "%s", g_websocket_server_url);
    return ERRCODE_SUCC;
}

errcode_t set_websocket_server_token(const char *token)
{
    if (token == NULL || token[0] == '\0') {
        g_websocket_server_token[0] = '\0';
        return ERRCODE_SUCC;
    }
    (void)snprintf(g_websocket_server_token, sizeof(g_websocket_server_token), "%s", token);
    return ERRCODE_SUCC;
}

errcode_t get_websocket_server_token(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0U) {
        return ERRCODE_FAIL;
    }
    (void)snprintf(buf, cap, "%s", g_websocket_server_token);
    return ERRCODE_SUCC;
}

void set_websocket_protocol_version(int version)
{
    g_websocket_protocol_version = version;
}

int get_websocket_protocol_version(void)
{
    return g_websocket_protocol_version;
}

int get_voice_interrupt_enabled(void)
{
    return (g_voice_interrupt_enabled != 0U) ? 1 : 0;
}

errcode_t set_voice_interrupt_enabled(int enabled)
{
    uint8_t msg_send = 0;
    g_voice_interrupt_enabled = (enabled != 0) ? 1U : 0U;
    agent_sync_listen_mode_from_settings();
//  post_msg_event(g_audx_event_qid, eAud_InterruptMode);
    return ERRCODE_SUCC;
}










char *get_hotspot_name(void)
{
    uint8_t devid[2] = {0};
    get_device_id(devid, 2);
    (void)snprintf(g_hotspot_ssid, sizeof(g_hotspot_ssid), "Xiaohong_%02X%02X", devid[0], devid[1]);
    log_debug("[Settings] get_hotspot_name: [%s]\r\n", g_hotspot_ssid);
    return g_hotspot_ssid;
}
char *get_hotspot_pswd(void)
{
    return g_hotspot_pswd;
}

uint8_t get_wifi_ssid(char* ssid, uint8_t ssidLen)
{
    if (ssid == NULL) {
        log_error("[Settings] get_wifi_ssid: args invalid\r\n");
        return 0;
    }
    if (eWifi_St_Connected != get_wifi_status()) {
        log_error("[Settings] get_wifi_ssid: NOT connected\r\n");
        return 0;
    }
    uint8_t len = strlen((char*)g_wifi_ssid);
    if (len == 0) {
        log_error("[Settings] get_wifi_ssid: no record\r\n");
        return 0;
    }
    if (len > ssidLen) {
        log_error("[Settings] get_wifi_ssid: len invalid\r\n");
        return 0;
    }

    memcpy_s(ssid, WIFI_SSID_LEN_MAX, g_wifi_ssid, len);
    ssid[len] = 0;

    return len;
}
uint8_t get_wifi_pswd(char* pswd, uint8_t pswdLen)
{
    if (pswd == NULL) {
        log_error("[Settings] get_wifi_pswd: args invalid\r\n");
        return 0;
    }
    if (eWifi_St_Connected != get_wifi_status()) {
        log_error("[Settings] get_wifi_pswd: NOT connected\r\n");
        return 0;
    }
    uint8_t len = strlen((char*)g_wifi_pswd);
    if (len == 0) {
        log_error("[Settings] get_wifi_pswd: no record\r\n");
        return 0;
    }
    if (len > pswdLen) {
        log_error("[Settings] get_wifi_pswd: len invalid\r\n");
        return 0;
    }

    memcpy_s(pswd, WIFI_PSWD_LEN_MAX, g_wifi_pswd, len);
    pswd[len] = 0;

    return len;
}
errcode_t get_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen)
{
    if ((ssid == NULL) || (pswd == NULL)) {
        log_error("[Settings] get_wifi_config: args invalid\r\n");
        return ERRCODE_FAIL;
    }
    uint8_t len_ssid = strlen((char*)g_wifi_ssid);
    uint8_t len_pswd = strlen((char*)g_wifi_pswd);
    if ((len_ssid == 0) || (len_pswd == 0)) {
        log_error("[Settings] get_wifi_config: no record\r\n");
        return ERRCODE_FAIL;
    }
    if ((len_ssid > ssidLen) || (len_pswd > pswdLen)) {
        log_error("[Settings] get_wifi_config: len invalid\r\n");
        return ERRCODE_FAIL;
    }

    memcpy_s(ssid, WIFI_SSID_LEN_MAX, g_wifi_ssid, len_ssid);
    memcpy_s(pswd, WIFI_PSWD_LEN_MAX, g_wifi_pswd, len_pswd);
    ssid[len_ssid] = 0;
    pswd[len_pswd] = 0;

    log_info("[Settings] get_wifi_config: ssid[%s] pswd[%s]\r\n", ssid, pswd);
    return ERRCODE_SUCC;
}
/*
errcode_t has_wifi_config(void)
{
    uint8_t len_ssid = strlen((char*)g_wifi_ssid);
    uint8_t len_pswd = strlen((char*)g_wifi_pswd);
    if ((len_ssid == 0) || (len_pswd == 0)) {
        return ERRCODE_FAIL;
    }
    return ERRCODE_SUCC;
}
*/
errcode_t clear_wifi_config(void)
{
    memset_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, 0, WIFI_SSID_LEN_MAX);
    memset_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, 0, WIFI_PSWD_LEN_MAX);

    return ERRCODE_SUCC;
}

errcode_t set_wifi_ssid(char* ssid, uint8_t ssidLen)
{
    if ((ssid == NULL) || (ssidLen == 0) || (ssidLen > WIFI_SSID_LEN_MAX)) {
        log_error("[Settings] set_wifi_ssid: args invalid\r\n");
        return ERRCODE_FAIL;
    }
    memset_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, 0, WIFI_SSID_LEN_MAX);
    memcpy_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, ssid, ssidLen);
    return ERRCODE_SUCC;
}
errcode_t set_wifi_pswd(char* pswd, uint8_t pswdLen)
{
    if ((pswd == NULL) || (pswdLen == 0) || (pswdLen > WIFI_SSID_LEN_MAX)) {
        log_error("[Settings] set_wifi_pswd: args invalid\r\n");
        return ERRCODE_FAIL;
    }
    memset_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, 0, WIFI_PSWD_LEN_MAX);
    memcpy_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, pswd, pswdLen);
    return ERRCODE_SUCC;
}
errcode_t set_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen)
{
    set_wifi_ssid(ssid, ssidLen);
    set_wifi_pswd(pswd, pswdLen);
    return ERRCODE_SUCC;
}

char* get_wifi_ip(void)
{
    return g_wifi_ip;
}
void set_wifi_ip(char* ip)
{
    if (ip == NULL) {
        memset_s(g_wifi_ip, IP_ADDR_LEN_MAX, 0U, IP_ADDR_LEN_MAX);
        log_info("[Settings] set_wifi_ip: ip is NULL, reset to 0.0.0.0\r\n");
        return;
    }
    uint8_t ip_len = strlen(ip);
    if (ip_len >= IP_ADDR_LEN_MAX) {
        log_error("[Settings] set_wifi_ip: ip len[%d] > %d, reset to 0.0.0.0\r\n", ip_len, IP_ADDR_LEN_MAX);
        memset_s(g_wifi_ip, IP_ADDR_LEN_MAX, 0U, IP_ADDR_LEN_MAX);
        return;
    }
    (void)memcpy_s(g_wifi_ip, IP_ADDR_LEN_MAX, ip, ip_len);
    g_wifi_ip[ip_len] = '\0';
    log_info("[Settings] set_wifi_ip: ip[%s]\r\n", g_wifi_ip);
}

uint8_t get_wifi_status(void)
{
    return g_wifi_state;
}

void set_wifi_status(uint8_t status)
{
    g_wifi_state = status;

    if (status == eWifi_St_Connected) {
        post_msg_event(g_main_event_qid, eSettings_Wifi_Connected);
        post_msg_event(g_63tx_event_qid, 0x0D01);
        post_msg_event(g_63tx_event_qid, 0x0D02);
    } else if (status == eWifi_St_DisConnected) {
        set_wifi_ip(NULL);
        post_msg_event(g_main_event_qid, eSettings_Wifi_DisConnected);
    }
    post_msg_event(g_63tx_event_qid, 0x0D03);
}

errcode_t set_ota_address(const char *addr)
{
    if ((addr == NULL) || (addr[0] == '\0')) {
        g_ota_address[0] = '\0';
        return ERRCODE_FAIL;
    }
    uint8_t addr_len = strlen(addr);

    if (addr_len >= OTA_ADDR_LEN_MAX) {
        log_error("[Settings] %s: addr len[%d] invalid\r\n", __func__, addr_len);
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(g_ota_address, OTA_ADDR_LEN_MAX, addr, addr_len);
    g_ota_address[addr_len] = '\0';
    log_debug("[Settings] %s: addr[%s]\r\n", __func__, g_ota_address);
    post_msg_event(g_63tx_event_qid, 0x0B09);
    return ERRCODE_SUCC;  //UART->P4
}
errcode_t get_ota_address(char *buf, size_t cap)
{
    if ((buf == NULL) || (cap == 0U)) {
        log_error("[Settings] %s: args invalid\r\n", __func__);
        return ERRCODE_FAIL;
    }
    char* ota_url = get_ota_url();
    uint8_t addr_len = strlen(ota_url);
    if (addr_len >= cap) {
        log_error("[Settings] %s: addr len[%d] invalid\r\n", __func__, addr_len);
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(buf, cap, ota_url, addr_len);
    buf[addr_len] = '\0';
    log_debug("[Settings] %s: addr[%s]\r\n", __func__, buf);
    return ERRCODE_SUCC;
}
char* get_ota_url(void)
{
    if (g_ota_address[0] == '\0') {
        return XIAOHONG_OTA_CHECK_URL;
    }

    if (strncmp((char*)g_ota_address, XIAOHONG_OTA_CHECK_URL_OLD, strlen(XIAOHONG_OTA_CHECK_URL_OLD)-1) == 0) {
        return XIAOHONG_OTA_CHECK_URL;
    } else if (strncmp((char*)g_ota_address, XIAOHONG_OTA_CHECK_URL, strlen(XIAOHONG_OTA_CHECK_URL)-1) == 0) {
        return XIAOHONG_OTA_CHECK_URL;
    }

    return (char*)g_ota_address;    // 用户在配网页面填写的OTA地址
}
#if 0
char* get_ota_url_origin(void)
{
    uint8_t len = 0;
    char* url = NULL;

    if (g_ota_address[0] == '\0') {
        url = XIAOHONG_OTA_CHECK_URL;
    } else {
        url = g_ota_address;
    }

    const char *p = url;
    if ((p = strstr(url, "://")) != NULL) {  // 跳过协议(http:///https://)
        p += 3;
    } else {
        p = url; // 无协议，直接从主机开始
    }
    const char *end = strstr(p, "/");  // 路径分隔符
    len = end ? (uint8_t)(end - url) : strlen(url);

    if (len + 1 > OTA_ADDR_LEN_MAX) {
        len = OTA_ADDR_LEN_MAX - 1;
    }
    (void)memcpy_s(g_ota_origin, OTA_ADDR_LEN_MAX, url, len);
    g_ota_origin[len] = '\0';

    log_debug("[Settings] %s: origin[%s]\r\n", __func__, g_ota_origin);
    return (char*)g_ota_origin;
}
#endif

errcode_t set_ota_firmware_address(const char *addr)
{
    if ((addr == NULL) || (addr[0] == '\0')) {
        g_ota_firmware_address[0] = '\0';
        g_ota_fw_ver_pending[0] = '\0';
        g_ota_fw_size_pending = 0U;
        g_ota_fw_sha256_pending[0] = '\0';
        return ERRCODE_FAIL;
    }
    uint8_t addr_len = strlen(addr);
    if (addr_len >= OTA_ADDR_LEN_MAX) {
        log_error("[Settings] set_ota_firmware_address: addr len[%d] invalid\r\n", addr_len);
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(g_ota_firmware_address, OTA_ADDR_LEN_MAX, addr, addr_len);
    g_ota_firmware_address[addr_len] = '\0';
    g_ota_fw_ver_pending[0] = '\0';
    g_ota_fw_size_pending = 0U;
    g_ota_fw_sha256_pending[0] = '\0';
    log_debug("[Settings] set_ota_firmware_address: addr[%s]\r\n", g_ota_firmware_address);
    return ERRCODE_SUCC;
}

errcode_t get_ota_firmware_address(char *buf, size_t cap)
{
    if ((buf == NULL) || (cap == 0U)) {
        log_error("[Settings] get_ota_firmware_address: args invalid\r\n");
        return ERRCODE_FAIL;
    }
    uint8_t addr_len = strlen(g_ota_firmware_address);
    if (addr_len == 0U || addr_len >= cap) {
        if (addr_len >= cap) {
            log_error("[Settings] get_ota_firmware_address: addr len[%d] invalid\r\n", addr_len);
        }
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(buf, cap, g_ota_firmware_address, addr_len);
    buf[addr_len] = '\0';
    log_debug("[Settings] get_ota_firmware_address: addr[%s]\r\n", buf);
    return ERRCODE_SUCC;
}

errcode_t set_ota_firmware_pending_meta(const char *version, uint32_t size_bytes, const char *sha256_hex)
{
    g_ota_fw_size_pending = size_bytes;
    g_ota_fw_sha256_pending[0] = '\0';
    if (sha256_hex != NULL && sha256_hex[0] != '\0') {
        (void)snprintf(g_ota_fw_sha256_pending, sizeof(g_ota_fw_sha256_pending), "%s", sha256_hex);
    }
    g_ota_fw_ver_pending[0] = '\0';
    if (version != NULL && version[0] != '\0') {
        (void)snprintf(g_ota_fw_ver_pending, sizeof(g_ota_fw_ver_pending), "%s", version);
    }
    return ERRCODE_SUCC;
}

errcode_t get_ota_firmware_version_pending(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0U) {
        return ERRCODE_FAIL;
    }
    (void)snprintf(buf, cap, "%s", g_ota_fw_ver_pending);
    return ERRCODE_SUCC;
}

uint32_t get_ota_firmware_size_pending(void)
{
    return g_ota_fw_size_pending;
}

errcode_t get_ota_firmware_sha256_pending(char *buf, size_t cap)
{
    if (buf == NULL || cap == 0U) {
        return ERRCODE_FAIL;
    }
    (void)snprintf(buf, cap, "%s", g_ota_fw_sha256_pending);
    return ERRCODE_SUCC;
}

void clear_ota_firmware_update_pending(void)
{
    g_ota_firmware_address[0] = '\0';
    g_ota_fw_ver_pending[0] = '\0';
    g_ota_fw_size_pending = 0U;
    g_ota_fw_sha256_pending[0] = '\0';
}


static char g_activation_hint[256] = {0};
void set_activation_hint(uint8_t type, const char *hint, uint8_t hint_len)
{
    memset_s(g_activation_hint, sizeof(g_activation_hint), '\0', sizeof(g_activation_hint));

    if ((hint == NULL) || (hint_len == 0)) {
        return;
    }
// type: 0: unknown msg, 1: activation_code, 2: activation_message, 3: activation_challenge, 3: activate_ok
    g_activation_hint[0] = type;
    memcpy_s(g_activation_hint+1, sizeof(g_activation_hint)-1, hint, hint_len);
    if (hint_len >= sizeof(g_activation_hint)-1) {
        g_activation_hint[sizeof(g_activation_hint)-1] = '\0';
    }
    post_msg_event(g_63tx_event_qid, 0x0B0B);
}
char* get_activation_hint(void)
{
    return (char*)g_activation_hint;
}

