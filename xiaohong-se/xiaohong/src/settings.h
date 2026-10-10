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

#include "log.h"

enum {
    eSysSt_READY = 0,
    eSysSt_NOT_READY,
    eSysSt_UNKNOWN = 0xFF
};

enum {
    eSettings_WiFi_Init = 0,  // 发起WiFi连接
    eSettings_WiFi_ReInit,    // 发起WiFi重新配网
    eSettings_WiFi_DisConn,   // 发起WiFi断开连接

    eSettings_Wifi_DisConnected,  // WiFi状态，已断开连接
    eSettings_Wifi_Connected,     // WiFi状态，已连接成功

    /** OTA 检查已拿到后续配置（新固件或 websocket），但字库未就绪：先拉字库，完成后再走 OTA/Run_Ready */
    eSettings_Start_Font_If_Needed,
    eSettings_Font_Downloaded,
    eSettings_Font_Not_Downloaded,
    eSettings_Ota_FW_Update,
    eSettings_Ota_FW_Not_Update,
    eSettings_Run_Ready,
    eSettings_MaxCount,
};

extern osMessageQueueId_t g_main_event_qid;   // 主事件队列，用于接收按键事件、网络连接/断开事件、字库下载完成事件等
//extern osMessageQueueId_t g_audx_event_qid;   // 音频事件队列，用于接收音频事件
extern osMessageQueueId_t g_agent_event_qid;  // Agent事件队列，用于接收Agent事件
extern osMessageQueueId_t g_63tx_event_qid;   // WS63 Uart Tx事件队列，用于相应UART的TX事件
osStatus_t post_msg_event(osMessageQueueId_t qid, uint16_t evt);


uint8_t get_local_status(void);
uint8_t set_local_status(uint8_t st);
uint8_t get_peer_status(void);
uint8_t set_peer_status(uint8_t st);

#define DEVICE_ID_LEN         (16)
#define DEVICE_ID_STR_LEN     (18)
uint8_t set_device_id(uint8_t* id_buff, uint8_t set_len);
uint8_t get_device_id(uint8_t* id_buff, uint8_t get_len);
char* get_device_id_str(void);


#define VERSION_STR_LEN       (10)
int check_version_int(int ver_local, int ver_remote);
int check_version_str(char* ver_local, char* ver_remote);
errcode_t set_version_p4(char* version);
int get_version_p4(void);
char* get_version_p4_str(void);
errcode_t set_version_ws63(char* version);
int get_version_ws63(void);
char* get_version_ws63_str(void);
errcode_t set_version_font(char* version);
int get_version_font(void);
char* get_version_font_str(void);


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




#define  HOTSPOT_SEC_OPEN_TYPE (1)  // 1: open network, 0: psk network
#define  SCAN_SSID_LIST_COUNT (32)  // 扫描到的Wi-Fi网络数量，显示这个供选择
#define  WIFI_SSID_LEN_MAX    (32)
#define  WIFI_PSWD_LEN_MAX    (32)
#define  IP_ADDR_LEN_MAX      (16)
char *get_hotspot_name(void);
char *get_hotspot_pswd(void);

//errcode_t has_wifi_config(void);
errcode_t clear_wifi_config(void);
errcode_t set_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen);
errcode_t get_wifi_config(char* ssid, uint8_t ssidLen, char* pswd, uint8_t pswdLen);
errcode_t set_wifi_ssid(char* ssid, uint8_t ssidLen);
errcode_t set_wifi_pswd(char* pswd, uint8_t pswdLen);
uint8_t get_wifi_ssid(char* ssid, uint8_t ssidLen);
uint8_t get_wifi_pswd(char* pswd, uint8_t pswdLen);
void set_wifi_status(uint8_t status);
uint8_t get_wifi_status(void);
char* get_wifi_ip(void);
void set_wifi_ip(char* ip);





#define  OTA_ADDR_LEN_MAX           (256)
errcode_t set_ota_address(const char *addr);
errcode_t get_ota_address(char *buf, size_t cap);
char* get_ota_url(void);
char* get_ota_url_origin(void);

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



/**
 * TTS 语音打断（WS listen/start 的 mode=realtime）。持久化键：config.json 中 `voice_interrupt`（1/0）。
 * 默认 0（auto / AutoStop）；仅配网页或唤醒键长按（小于 5s，与进配网的 5s 长按互斥）应调用 set 修改。
 * 无 LFS 时仅有内存默认值，不写入 NV。
 * @return 非 0：开启 realtime；0：使用 auto
 */
int get_voice_interrupt_enabled(void);
/** 会同步 Agent 内 g_listen_mode（agent_sync_listen_mode_from_settings）并写 config.json */
errcode_t set_voice_interrupt_enabled(int enabled);

void set_activation_hint(uint8_t type, const char *hint, uint8_t hint_len);
char* get_activation_hint(void);

void restart_system(uint8_t delay_seconds);


#if defined(SUPPORT_SLE_SERVER)&&(SUPPORT_SLE_SERVER == 1)
int32_t get_sensors_temp(void);
int32_t get_sensors_humi(void);
void set_sensors_temp(int32_t temp);
void set_sensors_humi(int32_t humi);
#endif
#endif//__SETTINGS_H__