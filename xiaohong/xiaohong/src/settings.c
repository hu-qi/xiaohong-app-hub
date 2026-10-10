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
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "settings.h"
#include "agent_state.h"
#include "audio_config.h"
#include "net_wifi_config.h"
#include "adc_porting.h"
#include "cJSON.h"
#include "ota.h"
#if defined(SUPPORT_LFS)
#include "fcntl.h"
#include "littlefs_adapt.h"
#endif

extern osMessageQueueId_t g_disp_event_qid;
extern osMessageQueueId_t g_main_event_qid;
extern osMessageQueueId_t g_audx_event_qid;


static uint8_t g_curr_vol = 70;  // [0, 100]
static uint8_t g_curr_blk = 10;  // [1, 40]

static uint8_t g_flash_id[FLASH_ID_LEN] = {0};
static char    g_dev_id_str[DEVICE_ID_STR_LEN] = "\0";

static char    g_hotspot_ssid[WIFI_SSID_LEN_MAX + 1] = {0};
static char    g_hotspot_pswd[] = "12345678";

static uint8_t g_wifi_ssid[WIFI_SSID_LEN_MAX+1] = {0};
static uint8_t g_wifi_pswd[WIFI_PSWD_LEN_MAX+1] = {0};
static uint8_t g_wifi_state = eWifi_St_DisConnected;

static char g_websocket_server_url[WEBSOCKET_SERVER_URL_MAX] = {0};
static char g_websocket_server_token[WEBSOCKET_SERVER_TOKEN_MAX] = {0};
static int  g_websocket_protocol_version = 0;
static int  g_font_version = 0;
static char g_font_ver_str[10] = "1.0.0";

char g_ota_address[OTA_ADDR_LEN_MAX] = {0};
char g_ota_origin[OTA_ADDR_LEN_MAX] = {0};
static char g_ota_firmware_address[OTA_ADDR_LEN_MAX] = {0};
static char g_ota_fw_ver_pending[64] = {0};
static uint32_t g_ota_fw_size_pending = 0U;
static char g_ota_fw_sha256_pending[65] = {0};
char  g_fw_ver_str[10] = "1.0.0";

/** 默认 0：auto（AutoStop）；1：realtime。仅配网页 POST 与 MainTask 长按唤醒(＜5s) 可调，见 config.json `voice_interrupt` */
static uint8_t g_voice_interrupt_enabled = 0U;

#if defined(SUPPORT_LFS)
#define SETTINGS_CONFIG_JSON_PATH       "system/config.json"
#define SETTINGS_CONFIG_JSON_READ_MAX   (2048U)
/*
static void settings_apply_backlight(uint8_t level)
{
    if ((BLK_DIV > 0U) && (level > BLK_MIN)) {
        (void)disp_set_backlight_level((uint8_t)(level / BLK_DIV * BLK_MUL));
    } else {
        (void)disp_set_backlight_level(level);
    }
}
*/
static errcode_t settings_parse_config_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        log_error("[Settings] config.json: JSON parse NG\r\n");
        return ERRCODE_FAIL;
    }

    cJSON *it = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(it)) {
        double v = it->valuedouble;
        if (v < 0.0) {
            v = 0.0;
        }
        if (v > (double)VOL_MAX) {
            v = (double)VOL_MAX;
        }
        g_curr_vol = (uint8_t)v;
    }

    it = cJSON_GetObjectItem(root, "brightness");
    if (cJSON_IsNumber(it)) {
        double v = it->valuedouble;
        uint8_t lvl = (uint8_t)v;
        if (lvl < BLK_MIN) {
            lvl = BLK_MIN;
        } else if (lvl > BLK_MAX) {
            lvl = BLK_MAX;
        }
        g_curr_blk = lvl;
    }

    it = cJSON_GetObjectItem(root, "wifi_ssid");
    if (cJSON_IsString(it) && (it->valuestring != NULL)) {
        (void)memset_s(g_wifi_ssid, sizeof(g_wifi_ssid), 0, sizeof(g_wifi_ssid));
        uint32_t n = (uint32_t)strlen(it->valuestring);
        if (n > WIFI_SSID_LEN_MAX) {
            n = WIFI_SSID_LEN_MAX;
        }
        (void)memcpy_s(g_wifi_ssid, sizeof(g_wifi_ssid), it->valuestring, n);
        g_wifi_ssid[n] = '\0';
    }

    it = cJSON_GetObjectItem(root, "wifi_password");
    if (cJSON_IsString(it) && (it->valuestring != NULL)) {
        (void)memset_s(g_wifi_pswd, sizeof(g_wifi_pswd), 0, sizeof(g_wifi_pswd));
        uint32_t n = (uint32_t)strlen(it->valuestring);
        if (n > WIFI_PSWD_LEN_MAX) {
            n = WIFI_PSWD_LEN_MAX;
        }
        (void)memcpy_s(g_wifi_pswd, sizeof(g_wifi_pswd), it->valuestring, n);
        g_wifi_pswd[n] = '\0';
    }

    it = cJSON_GetObjectItem(root, "ota_url");
    if (cJSON_IsString(it) && (it->valuestring != NULL)) {
        uint32_t n = (uint32_t)strlen(it->valuestring);
        if (n >= OTA_ADDR_LEN_MAX) {
            n = OTA_ADDR_LEN_MAX - 1U;
        }
        (void)memcpy_s(g_ota_address, OTA_ADDR_LEN_MAX, it->valuestring, n);
        g_ota_address[n] = '\0';
    } else {
        uint32_t n = (uint32_t)strlen(XIAOHONG_OTA_CHECK_URL);
        if (n >= OTA_ADDR_LEN_MAX) {
            n = OTA_ADDR_LEN_MAX - 1U;
        }
        (void)memcpy_s(g_ota_address, OTA_ADDR_LEN_MAX, XIAOHONG_OTA_CHECK_URL, n);
        g_ota_address[n] = '\0';
    }

    it = cJSON_GetObjectItem(root, "voice_interrupt");
    if (cJSON_IsNumber(it)) {
        g_voice_interrupt_enabled = (it->valuedouble != 0.0) ? 1U : 0U;
    }

    it = cJSON_GetObjectItem(root, "font_version");
    if (cJSON_IsNumber(it)) {
        g_font_version = (int)it->valuedouble;
    }

    cJSON_Delete(root);
//  settings_apply_backlight(g_curr_blk);
    agent_sync_listen_mode_from_settings();
    return ERRCODE_SUCC;
}
#endif /* SUPPORT_LFS */

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

    //log_debug("[Settings] get_voltage_level: volt[%d], voltage[%d]\r\n", volt, voltage);
    return volt;
}

uint8_t get_device_id(uint8_t* id_buff, uint8_t get_len)
{
    if ((id_buff == NULL) || (get_len == 0)) {
        return 0;
    } else {
        uint8_t len = (get_len <= FLASH_ID_LEN)?get_len:FLASH_ID_LEN;
        if (memcpy_s(id_buff, len, &g_flash_id[FLASH_ID_LEN-len], len) == 0) {
            return len;
        } else {
            return 0;
        }
    }
}
char* get_device_id_str(void)
{
    /* DEVICE_ID_STR_LEN=18 -> 最多 "XX:XX:XX:XX:XX:XX\0"（6 字节 Flash ID） */
    uint8_t len = (uint8_t)(DEVICE_ID_STR_LEN / 3U);
    if (len > FLASH_ID_LEN) {
        len = FLASH_ID_LEN;
    }
    if (len == 0U) {
        g_dev_id_str[0] = '\0';
        log_error("[Settings] %s: (empty)\r\n", __func__);
        return g_dev_id_str;
    }

    if (g_dev_id_str[0] != '\0') {
        log_debug("[Settings] %s: {%s}\r\n", __func__, g_dev_id_str);
        return g_dev_id_str;
    }

    size_t pos = 0U;
    const uint8_t *src = &g_flash_id[FLASH_ID_LEN - len]; // 默认使用后6个字节作为设备ID
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

    log_debug("[Settings] %s: [%s]\r\n", __func__, g_dev_id_str);
    return g_dev_id_str;
}

char *get_hotspot_name(void)
{
    uint8_t devid[2] = {0};
    get_device_id(devid, 2);
    (void)snprintf(g_hotspot_ssid, sizeof(g_hotspot_ssid), "Xiaohong_%02X%02X", devid[0], devid[1]);
    log_debug("[Settings] %s: [%s]\r\n", __func__, g_hotspot_ssid);
    return g_hotspot_ssid;
}
char *get_hotspot_pswd(void)
{
    return g_hotspot_pswd;
}

errcode_t read_config_from_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint16_t nvlen = 0;

    if (flash_read_id(g_flash_id) != ERRCODE_SUCC) {
        log_error("[Settings] uapi_nv_read: FlashID: NG\r\n");
    }

    hilink_connect_info_t hl_data = {0};
    ret = uapi_nv_read(0x1001, sizeof(hilink_connect_info_t), &nvlen, (uint8_t*)&hl_data);
    if (ret == ERRCODE_SUCC) {
        log_debug("[Settings] uapi_nv_read: OK\r\n  ssid:[%s]\r\n  pswd:[%s]\r\n   vol:[%d]\r\n   blk:[%d]\r\n",
                (char*)hl_data.ssid, (char*)hl_data.pwd, hl_data.bssid[0], hl_data.bssid[1]);
        memcpy_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, hl_data.ssid, WIFI_SSID_LEN_MAX);
        memcpy_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, hl_data.pwd,  WIFI_PSWD_LEN_MAX);
        set_volume(hl_data.bssid[0]);
        set_blk_level(hl_data.bssid[1]);
    } else {
        log_error("[Settings] uapi_nv_read: NG\r\n");
    }

    return ret;
}

errcode_t write_config_to_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;
    hilink_connect_info_t hl_data = {0};
    hl_data.bssid[0] = get_volume();
    hl_data.bssid[1] = get_blk_level();

    memcpy_s(hl_data.ssid, WIFI_SSID_LEN_MAX, g_wifi_ssid, WIFI_SSID_LEN_MAX);
    memcpy_s(hl_data.pwd,  WIFI_PSWD_LEN_MAX, g_wifi_pswd, WIFI_PSWD_LEN_MAX);

    ret = uapi_nv_write(0x1001, (uint8_t*)&hl_data, sizeof(hilink_connect_info_t));
    if (ret != ERRCODE_SUCC) {
        log_error("[Settings] uapi_nv_write    : NG\r\n");
    }
    return ret;
}

errcode_t read_config_from_lfs(void)
{
#if defined(FW_VERSION)
    memset_s(g_fw_ver_str, sizeof(g_fw_ver_str), 0, sizeof(g_fw_ver_str));
    (void)snprintf(g_fw_ver_str, sizeof(g_fw_ver_str), "%d.%d.%d", FW_VERSION >> 16, (FW_VERSION >> 8) & 0xFF, FW_VERSION & 0xFF);
#endif

#if !defined(SUPPORT_LFS)
    return read_config_from_nv();
#else
    errcode_t ret;

    if (flash_read_id(g_flash_id) != ERRCODE_SUCC) {
        log_error("[Settings] flash_read_id: FlashID: NG\r\n");
    }

    unsigned int sz = 0U;
    if (fs_adapt_stat(SETTINGS_CONFIG_JSON_PATH, &sz) != 0) {
        log_warn("[Settings] %s not found, create default\r\n", SETTINGS_CONFIG_JSON_PATH);
        ret = write_config_to_lfs();
        if (ret != ERRCODE_SUCC) {
            log_error("[Settings] write_config_to_lfs: NG\r\n");
            return ret;
        }
//      settings_apply_backlight(g_curr_blk);
        return ERRCODE_SUCC;
    } else {
        log_debug("[Settings] config.json: found, size %u\r\n", sz);
    }

    if ((sz == 0U) || (sz >= SETTINGS_CONFIG_JSON_READ_MAX)) {
        log_error("[Settings] config.json: invalid size %u\r\n", sz);
        return ERRCODE_FAIL;
    }

    int fd = fs_adapt_open(SETTINGS_CONFIG_JSON_PATH, O_RDONLY);
    if (fd < 0) {
        log_error("[Settings] config.json: open RO NG\r\n");
        return ERRCODE_FAIL;
    }

    char *buf = (char *)malloc(sz + 1U);
    if (buf == NULL) {
        (void)fs_adapt_close(fd);
        return ERRCODE_FAIL;
    }

    int n = fs_adapt_read(fd, buf, sz);
    (void)fs_adapt_close(fd);
    if (n != (int)sz) {
        log_error("[Settings] config.json: read NG (%d / %u)\r\n", n, sz);
        free(buf);
        return ERRCODE_FAIL;
    }
    buf[sz] = '\0';

    log_debug("[Settings] config.json: content:\r\n%s\r\n", buf);
    ret = settings_parse_config_json(buf);
    free(buf);
    if (ret == ERRCODE_SUCC) {
        printf("[Settings] config.json: loaded (vol=%u blk=%u font=0x%06X fw=0x%06X)\r\n",
               (unsigned int)g_curr_vol, (unsigned int)g_curr_blk, g_font_version, FW_VERSION);
        printf("[Settings] config.json: g_ota_address: %s\r\n", g_ota_address);
        (void) get_device_id_str();
    }

    memset_s(g_font_ver_str, sizeof(g_font_ver_str), 0, sizeof(g_font_ver_str));
    (void)snprintf(g_font_ver_str, sizeof(g_font_ver_str), "%d.%d.%d", g_font_version >> 16, (g_font_version >> 8) & 0xFF, g_font_version & 0xFF);

    return ret;
#endif /* SUPPORT_LFS */
}

errcode_t write_config_to_lfs(void)
{
#if !defined(SUPPORT_LFS)
    return write_config_to_nv();
#else
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ERRCODE_FAIL;
    }

    (void)cJSON_AddNumberToObject(root, "brightness", (double)g_curr_blk);
    (void)cJSON_AddNumberToObject(root, "volume", (double)g_curr_vol);
    (void)cJSON_AddStringToObject(root, "wifi_ssid", (const char *)g_wifi_ssid);
    (void)cJSON_AddStringToObject(root, "wifi_password", (const char *)g_wifi_pswd);
    (void)cJSON_AddStringToObject(root, "ota_url", g_ota_address);
    (void)cJSON_AddNumberToObject(root, "voice_interrupt", (double)g_voice_interrupt_enabled);
    (void)cJSON_AddNumberToObject(root, "font_version", (double)g_font_version);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) {
        log_error("[Settings] config.json: cJSON_PrintUnformatted NG\r\n");
        return ERRCODE_FAIL;
    }

    int fd = fs_adapt_open(SETTINGS_CONFIG_JSON_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        log_error("[Settings] config.json: open WR NG\r\n");
        free(out);
        return ERRCODE_FAIL;
    }

    size_t len = strlen(out);
    int w = fs_adapt_write(fd, out, (unsigned int)len);
    (void)fs_adapt_sync(fd);
    (void)fs_adapt_close(fd);
    free(out);

    if (w != (int)len) {
        log_error("[Settings] config.json: write NG (%d / %zu)\r\n", w, len);
        return ERRCODE_FAIL;
    }
    return ERRCODE_SUCC;
#endif /* SUPPORT_LFS */
}

uint8_t get_volume(void)
{
    return g_curr_vol;
}

errcode_t set_volume(uint8_t vol)
{
    if (vol >= VOL_MAX) {
        g_curr_vol = VOL_MAX;
    } else {
        g_curr_vol = vol;
    }

    (void)write_config_to_lfs();

#if defined(SUPPORT_LVGL)
    uint8_t ev = eDisp_Bar_Update;
    osMessageQueuePut(g_disp_event_qid, (const void *)&ev, 0, 0);
#endif

    return ERRCODE_SUCC;
}

errcode_t set_volume_mute(void)
{
    return set_volume(0);
}

errcode_t increase_volume(void)
{
    uint8_t new_vol = get_volume();
    if (new_vol < VOL_MIN) {
        new_vol = VOL_MIN;
    } else if (new_vol <= VOL_MAX - VOL_STEP) {
        new_vol += VOL_STEP;
    } else if (new_vol <= VOL_MAX){
        new_vol = VOL_MAX;
    }
    return set_volume(new_vol);
}

errcode_t decrease_volume(void)
{
    uint8_t new_vol = get_volume();
    if (new_vol <= VOL_MIN) {
        new_vol = 0;
    } else if (new_vol <= VOL_STEP) {
        new_vol = VOL_MIN;
    } else {
        new_vol -= VOL_STEP;
    }
    return set_volume(new_vol);
}

uint8_t get_blk_level(void)
{
    return g_curr_blk;
}

errcode_t set_blk_level(uint8_t level)
{
    if (level <= BLK_MIN) {
        g_curr_blk = BLK_MIN;
    } else if (level >= BLK_MAX) {
        g_curr_blk = BLK_MAX;
    } else {
        g_curr_blk = level;
    }

    (void)write_config_to_lfs();

#if defined(SUPPORT_LVGL)
    uint8_t ev = eDisp_Bar_Update;
    osMessageQueuePut(g_disp_event_qid, (const void *)&ev, 0, 0);
    if ((BLK_DIV > 0) && (g_curr_blk > BLK_MIN)) {
        return disp_set_backlight_level(g_curr_blk/BLK_DIV*BLK_MUL);
    }
    return disp_set_backlight_level(g_curr_blk);
#else
    return ERRCODE_SUCC;
#endif
}

errcode_t set_blk_level_off(void)
{
#if defined(SUPPORT_LVGL)
    return disp_set_backlight_level(0);
#else
    return ERRCODE_SUCC;
#endif
}

errcode_t increase_blk_level(void)
{
    uint8_t new_level = get_blk_level();
    if (new_level < BLK_STEP) {
        new_level = BLK_STEP;
    } else if (new_level <= BLK_MAX - BLK_STEP) {
        new_level += BLK_STEP;
    } else if (new_level <= BLK_MAX) {
        new_level = BLK_MAX;
    }
    return set_blk_level(new_level);
}

errcode_t decrease_blk_level(void)
{
    uint8_t new_level = get_blk_level();
    if (new_level <= BLK_STEP) {
        new_level = BLK_MIN;
    } else {
        new_level -= BLK_STEP;
    }
    return set_blk_level(new_level);
}
errcode_t get_wifi_ssid(char* ssid, uint8_t ssidLen)
{
    if (ssid == NULL) {
        log_error("[Settings] get_wifi_ssid: args invalid\r\n");
        return ERRCODE_FAIL;
    }
    uint8_t len_ssid = strlen((char*)g_wifi_ssid);
    if (len_ssid == 0) {
        log_error("[Settings] get_wifi_ssid: no record\r\n");
        return ERRCODE_FAIL;
    }
    if (len_ssid > ssidLen) {
        log_error("[Settings] get_wifi_ssid: len invalid\r\n");
        return ERRCODE_FAIL;
    }

    memcpy_s(ssid, WIFI_SSID_LEN_MAX, g_wifi_ssid, len_ssid);
    ssid[len_ssid] = 0;

    return ERRCODE_SUCC;
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

    return ERRCODE_SUCC;
}
errcode_t has_wifi_config(void)
{
    uint8_t len_ssid = strlen((char*)g_wifi_ssid);
    uint8_t len_pswd = strlen((char*)g_wifi_pswd);
    if ((len_ssid == 0) || (len_pswd == 0)) {
        return ERRCODE_FAIL;
    }
    return ERRCODE_SUCC;
}
errcode_t clear_wifi_config(void)
{
    memset_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, 0, WIFI_SSID_LEN_MAX);
    memset_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, 0, WIFI_PSWD_LEN_MAX);

    return write_config_to_lfs();
}
errcode_t set_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen)
{
    if ((ssid == NULL) || (pswd == NULL)) {
        log_error("[Settings] set_wifi_config: args invalid\r\n");
        return ERRCODE_FAIL;
    }

    if ((ssidLen > WIFI_SSID_LEN_MAX) || (pswdLen > WIFI_PSWD_LEN_MAX)) {
        log_error("[Settings] set_wifi_config: len invalid\r\n");
        return ERRCODE_FAIL;
    }
    memset_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, 0, WIFI_SSID_LEN_MAX);
    memset_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, 0, WIFI_PSWD_LEN_MAX);
    memcpy_s(g_wifi_ssid, WIFI_SSID_LEN_MAX, ssid, ssidLen);
    memcpy_s(g_wifi_pswd, WIFI_PSWD_LEN_MAX, pswd, pswdLen);

    return write_config_to_lfs();
}

uint8_t get_wifi_status(void)
{
    return g_wifi_state;
}

void set_wifi_status(uint8_t status)
{
    g_wifi_state = status;
    uint8_t msg_send = 0;

#if defined(SUPPORT_LVGL)
    msg_send = eDisp_Bar_Update;
    osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);

    msg_send = eDisp_WiFi_St_Update;
    osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
#endif

    if (status == eWifi_St_Connected) {
        msg_send = eSettings_Wifi_Connected;
    } else {
        msg_send = eSettings_Wifi_DisConnected;
    }
    osMessageQueuePut(g_main_event_qid, (const void *) &msg_send, 0, 0);
}

errcode_t set_websocket_server_url(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        g_websocket_server_url[0] = '\0';
        return ERRCODE_SUCC;
    }
    (void)snprintf(g_websocket_server_url, sizeof(g_websocket_server_url), "%s", url);
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

errcode_t set_ota_address(const char *addr)  // 用户在配网页面填写的OTA地址
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
    (void)write_config_to_lfs();
    return ERRCODE_SUCC;
}

// 官方已废弃的OTA地址，官方最新的OTA地址为 XIAOHONG_OTA_CHECK_URL
static char *obsoleted_ota_addr_list[] =
{
    "http://xiaohong-ota.yunxin360.com",  // ":8002/xiaohong/ota/",
    "http://xhong-ota.yunxin360.com",     // "/xiaohong/ota/",
    "http://xh-ota.atomgit.com",          // "/xiaohong/ota/",
};
static uint8_t obsoleted_ota_addr_list_len = sizeof(obsoleted_ota_addr_list) / sizeof(obsoleted_ota_addr_list[0]);

char *get_ota_url(void)
{
    if (g_ota_address[0] == '\0') {
        return XIAOHONG_OTA_CHECK_URL;
    }
    for (uint8_t i = 0; i < obsoleted_ota_addr_list_len; i++) {
        // 匹配官方已废弃的OTA地址，统一返回官方最新的OTA地址
        if (strncmp((char*)g_ota_address, obsoleted_ota_addr_list[i], strlen(obsoleted_ota_addr_list[i])) == 0) {
            return XIAOHONG_OTA_CHECK_URL;
        }
    }

    return (char*)g_ota_address;    // 用户在配网页面填写的OTA地址
}

char* get_server_url(void)
{
    uint8_t len = 0;
    char* url = get_ota_url();

    if ((url[0] == '\0') || (url == NULL)) {
        url = XIAOHONG_SERVER_URL;
    }
    for (uint8_t i = 0; i < obsoleted_ota_addr_list_len; i++) {
        // 匹配官方已废弃的OTA地址，统一返回官方最新的后台服务器地址
        if (strncmp((char *)g_ota_address, obsoleted_ota_addr_list[i], strlen(obsoleted_ota_addr_list[i])) == 0) {
            url = XIAOHONG_SERVER_URL;
        }
    }
    return url;    // 直接返回URL，不再截取URL
}

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
        log_error("[Settings] %s: addr len[%d] invalid\r\n", __func__, addr_len);
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(g_ota_firmware_address, OTA_ADDR_LEN_MAX, addr, addr_len);
    g_ota_firmware_address[addr_len] = '\0';
    g_ota_fw_ver_pending[0] = '\0';
    g_ota_fw_size_pending = 0U;
    g_ota_fw_sha256_pending[0] = '\0';
    log_debug("[Settings] %s: addr[%s]\r\n", __func__, g_ota_firmware_address);
    return ERRCODE_SUCC;
}

errcode_t get_ota_firmware_address(char *buf, size_t cap)
{
    if ((buf == NULL) || (cap == 0U)) {
        log_error("[Settings] %s: args invalid\r\n", __func__);
        return ERRCODE_FAIL;
    }
    uint8_t addr_len = strlen(g_ota_firmware_address);
    if (addr_len == 0U || addr_len >= cap) {
        if (addr_len >= cap) {
            log_error("[Settings] %s: addr len[%d] invalid\r\n", __func__, addr_len);
        }
        return ERRCODE_FAIL;
    }
    (void)memcpy_s(buf, cap, g_ota_firmware_address, addr_len);
    buf[addr_len] = '\0';
    log_debug("[Settings] %s: addr[%s]\r\n", __func__, buf);
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

int get_voice_interrupt_enabled(void)
{
    return (g_voice_interrupt_enabled != 0U) ? 1 : 0;
}

errcode_t set_voice_interrupt_enabled(int enabled)
{
    uint8_t msg_send = 0;
    uint8_t voice_intr_en = (enabled != 0) ? 1U : 0U;
    if (g_voice_interrupt_enabled == voice_intr_en) {
        return ERRCODE_SUCC;
    }
    g_voice_interrupt_enabled = voice_intr_en;

    agent_sync_listen_mode_from_settings();
    if (g_audx_event_qid != NULL) {
        msg_send = eAud_InterruptMode;
        osMessageQueuePut(g_audx_event_qid, (const void *)&msg_send, 0, 0);
    }
#if defined(SUPPORT_LVGL)
    if (g_disp_event_qid != NULL) {
        msg_send = eDisp_Bar_Update;
        osMessageQueuePut(g_disp_event_qid, (const void *) &msg_send, 0, 0);
    }
#endif
    osal_msleep(1U);
    log_info("[Settings] set_voice_interrupt_enabled: [%d]\r\n", g_voice_interrupt_enabled);
    return write_config_to_lfs();
}

uint8_t check_font_bin_exists(bool rm_flag)
{
    unsigned int sz = 0U;
    if (rm_flag && (fs_adapt_stat("system/font.bin", &sz) == 0)) {
        log_info("[Settings] /system/font.bin exists, size = %d\r\n", sz);
        if (fs_adapt_delete("system/font.bin") != 0) {
            log_error("[Settings] remove /system/font.bin failed (e.g. file open)\r\n");
        } else {
            log_info("[Settings] remove /system/font.bin OK\r\n");
        }
    }

    if (fs_adapt_stat("system/font.bin", &sz) == 0) {
        log_debug("[Settings] /system/font.bin exists, size = %d\r\n", sz);
        return 1;
    } else {
        log_error("[Settings] /system/font.bin not exists\r\n");
        return 0;
    }
}

/**
 * 将 "major.minor.patch" 转为 int，例如 "1.2.13" -> 0x0001020D
 * 各段按 0~255 占一个字节；解析失败返回 0
 */
static int convert_version_str_to_int(char *version)
{
    int major = 0, minor = 0, patch = 0;
    char *ver = version;
    char *end = NULL;
    long v;

    log_debug("[Settings] %s: version[%s] ----->>>>>\r\n", __func__, version);
    if (ver == NULL || *ver == '\0') {
        log_error("[Settings] %s: ----->>>>> 0\r\n", __func__);
        return 0;
    }
    /* major */
    v = strtol(ver, &end, 10);
    if (end == ver || *end != '.' || v < 0 || v > 255) {
        log_error("[Settings] %s: ----->>>>> 1\r\n", __func__);
        return 0;
    }
    major = (int)v;
    ver = end + 1;
    /* minor */
    v = strtol(ver, &end, 10);
    if (end == ver || *end != '.' || v < 0 || v > 255) {
        log_error("[Settings] %s: ----->>>>> 2\r\n", __func__);
        return 0;
    }
    minor = (int)v;
    ver = end + 1;
    /* patch */
    v = strtol(ver, &end, 10);
    if (end == ver || v < 0 || v > 255) {
        log_error("[Settings] %s: ----->>>>> 3\r\n", __func__);
        return 0;
    }
    patch = (int)v;
    /* 允许末尾仅有空白 */
    while (end && *end && isspace((unsigned char)*end))
        end++;
    if (end && *end != '\0') {
        log_error("[Settings] %s: ----->>>>> 4\r\n", __func__);
        return 0;
    }

    log_debug("[Settings] convert_version_str_to_int: version[%s] -> [0x%06X]\r\n", version, (major << 16) | (minor << 8) | patch);
    return (major << 16) | (minor << 8) | patch;
}

int check_font_version(char* version)
{
    int version_int = convert_version_str_to_int(version);

    log_info("[Settings] check_font_version: local[0x%06X] vs. remote[0x%06X] >> ret[%d]\r\n",
        g_font_version, version_int, version_int > g_font_version ? 1 : 0);

#if defined(SUPPORT_LVGL)
    return version_int > g_font_version ? 1 : 0;
#else  // 无显示需要，不下载字库
    return 0;
#endif
}

errcode_t set_font_version(char* version)
{
    g_font_version = convert_version_str_to_int(version);
    return write_config_to_lfs();
}

int get_font_version(void)
{
    if (check_font_bin_exists(false) == 0) {
        g_font_version = 0;
    }
    return g_font_version;
}

char* get_font_ver_str(void)
{
    if (check_font_bin_exists(false) == 0) {
        return "0.0.0";
    } else {
        return (char*)g_font_ver_str;
    }
}

int check_fw_version(char* version)
{
#if defined(FW_VERSION)
    int version_int = convert_version_str_to_int(version);

    log_info("[Settings] check_fw_version:   local[0x%06X] vs. remote[0x%06X] >> ret[%d]\r\n",
        FW_VERSION, version_int, version_int > FW_VERSION ? 1 : 0);

    return version_int > FW_VERSION ? 1 : 0;
#else
    return 0;
#endif
}

int get_fw_version(void)
{
#if defined(FW_VERSION)
    return FW_VERSION;
#else
    return 0x010000;    // 1.0.0
#endif
}

char* get_fw_ver_str(void)
{
#if defined(FW_VERSION)
    return (char*)g_fw_ver_str;
#else
    return "1.0.0";
#endif
}

void restart_system(uint8_t delay_seconds)
{
    if (delay_seconds > 0) {
        sleep(delay_seconds);
    }
    cpu_utils_set_system_status_by_cause(0);
    hal_reboot_chip();
}