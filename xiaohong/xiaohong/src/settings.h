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

enum {
    eSettings_Wifi_DisConnected = eKey_MaxCount,  // see key_config.h: eKey_MaxCount[6]
    eSettings_Wifi_Connected,
    /** OTA 检查已拿到后续配置（新固件或 websocket），但字库未就绪：先拉字库，完成后再走 OTA/Run_Ready */
    eSettings_Start_Font_If_Needed,
    eSettings_Font_Downloaded,
    eSettings_Font_Not_Downloaded,
    eSettings_Ota_FW_Update,
    eSettings_Ota_FW_Not_Update,
    eSettings_Run_Ready,
    eSettings_MaxCount,
};

uint8_t get_voltage_level(void);
uint8_t get_device_id(uint8_t* id_buff, uint8_t get_len);
#define DEVICE_ID_STR_LEN    (18)
char* get_device_id_str(void);

#define  VOL_MAX    (100)
#define  VOL_MIN    (10)
#define  VOL_STEP   (15)
uint8_t get_volume(void);
errcode_t set_volume(uint8_t vol);
errcode_t set_volume_mute(void);
errcode_t increase_volume(void);
errcode_t decrease_volume(void);

#define  BLK_MAX    (100)
#define  BLK_MIN    (1)
#define  BLK_STEP   (10)
#define  BLK_DIV    (5)
#define  BLK_MUL    (2)      // 100的亮度太大，调整为 val/BLK_DIV*BLK_MUL 即原先 2/5 的亮度值
uint8_t get_blk_level(void);
errcode_t set_blk_level(uint8_t level);
errcode_t set_blk_level_off(void);
errcode_t increase_blk_level(void);
errcode_t decrease_blk_level(void);

#define  HOTSPOT_SEC_OPEN_TYPE (1)  // 1: open network, 0: psk network
#define  SCAN_SSID_LIST_COUNT (32)  // 扫描到的Wi-Fi网络数量，显示这个供选择
#define  WIFI_SSID_LEN_MAX    (32)
#define  WIFI_PSWD_LEN_MAX    (32)
errcode_t has_wifi_config(void);
errcode_t clear_wifi_config(void);
errcode_t get_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen);
errcode_t set_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen);
errcode_t get_wifi_ssid(char* ssid, uint8_t ssidLen);

uint8_t get_wifi_status(void);
void set_wifi_status(uint8_t status);
char *get_hotspot_name(void);
char *get_hotspot_pswd(void);

errcode_t read_config_from_nv(void);
errcode_t write_config_to_nv(void);
errcode_t read_config_from_lfs(void);
errcode_t write_config_to_lfs(void);

/** OTA 返回的 WebSocket 地址（内存，与 mg_ws URL 上限一致） */
#define WEBSOCKET_SERVER_URL_MAX    (256)
#define WEBSOCKET_SERVER_TOKEN_MAX  (256)
errcode_t set_websocket_server_url(const char *url);
errcode_t get_websocket_server_url(char *buf, size_t cap);
errcode_t set_websocket_server_token(const char *token);
errcode_t get_websocket_server_token(char *buf, size_t cap);
/** OTA `websocket.version`，0 表示未配置或沿用默认 1 */
void set_websocket_protocol_version(int version);
int  get_websocket_protocol_version(void);

#define  OTA_ADDR_LEN_MAX     (256)
errcode_t set_ota_address(const char *addr);
//errcode_t get_ota_address(char *buf, size_t cap);
char* get_ota_url(void);
char* get_server_url(void);

/** OTA 检查返回的固件下载 URL（仅内存，不覆盖/持久化系统 ota_url） */
errcode_t set_ota_firmware_address(const char *addr);
errcode_t get_ota_firmware_address(char *buf, size_t cap);
/** 与固件 URL 配套的元数据（供 OTA 任务直链下载；`set_ota_firmware_address` 成功后会先清空再由本函数写入） */
errcode_t set_ota_firmware_pending_meta(const char *version, uint32_t size_bytes, const char *sha256_hex);
errcode_t get_ota_firmware_version_pending(char *buf, size_t cap);
uint32_t get_ota_firmware_size_pending(void);
/** 无 sha256 时返回 SUCC 且 buf[0]=='\\0' */
errcode_t get_ota_firmware_sha256_pending(char *buf, size_t cap);
void clear_ota_firmware_update_pending(void);

int check_fw_version(char* version);
int get_fw_version(void);
char* get_fw_ver_str(void);

uint8_t check_font_bin_exists(bool rm_flag);
int check_font_version(char* version);
errcode_t set_font_version(char* version);
int get_font_version(void);
char* get_font_ver_str(void);

/**
 * TTS 语音打断（WS listen/start 的 mode=realtime）。持久化键：config.json 中 `voice_interrupt`（1/0）。
 * 默认 0（auto / AutoStop）；仅配网页或唤醒键长按（小于 5s，与进配网的 5s 长按互斥）应调用 set 修改。
 * 无 LFS 时仅有内存默认值，不写入 NV。
 * @return 非 0：开启 realtime；0：使用 auto
 */
int get_voice_interrupt_enabled(void);
/** 会同步 Agent 内 g_listen_mode（agent_sync_listen_mode_from_settings）并写 config.json */
errcode_t set_voice_interrupt_enabled(int enabled);

void restart_system(uint8_t delay_seconds);

#endif//__SETTINGS_H__