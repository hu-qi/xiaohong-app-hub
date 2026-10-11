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

#ifndef XIAOHONG_OTA_H
#define XIAOHONG_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "errcode.h"

#ifndef XIAOHONG_OTA_CHECK_URL
#define XIAOHONG_OTA_CHECK_URL  "https://xh-ota.atomgit.com/xiaohong/ota/"
#define XIAOHONG_SERVER_URL     "https://xhong.atomgit.com/#"
#endif

#define OTA_STRING_MAX 512
#define OTA_URL_MAX    384
/** 与 `mongoose_protocol.h` MG_WS_TOKEN_MAX 一致 */
#define OTA_WS_TOKEN_MAX 256

typedef struct ota_result {
    bool has_activation_code;
    bool has_activation_challenge;
    bool has_mqtt_config;
    bool has_websocket_config;
    bool has_server_time;
    bool has_new_version;

    char activation_message[OTA_STRING_MAX];
    char activation_code[128];
    char activation_challenge[256];
    char firmware_version[64];
    char firmware_url[OTA_URL_MAX];
    /** OTA JSON 中 `firmware.size`（字节），0 表示未返回 */
    uint32_t firmware_size;
    /** OTA JSON 中 `firmware.sha256`（可选，64 位十六进制小写/大写均可） */
    char firmware_sha256[65];
    /** WebSocket 入口（OTA JSON 中 `websocket.url`） */
    char websocket_url[OTA_URL_MAX];
    /** OTA JSON 中 `websocket.token`（Authorization） */
    char websocket_token[OTA_WS_TOKEN_MAX];
    /** OTA JSON 中 `websocket.version`（协议版本，0 表示未返回该字段） */
    int  websocket_protocol_version;
    int  activation_timeout_ms;
} ota_result_t;

typedef void (*ota_kv_string_fn)(const char *namespace_, const char *key, const char *value,
                                 void *user);
typedef void (*ota_kv_int_fn)(const char *namespace_, const char *key, int value, void *user);

typedef void (*ota_server_time_fn)(double timestamp_ms, int timezone_offset_minutes, void *user);

typedef struct ota_parse_hooks {
    ota_kv_string_fn on_string;
    ota_kv_int_fn    on_int;
    ota_server_time_fn on_server_time;
    void *user;
} ota_parse_hooks_t;

/**
 * 解析 OTA 检查接口返回的 JSON（HTTP 200 body）。
 * @param json_body        完整响应字符串（NUL 结尾）
 * @param current_version  当前固件版本号，如 "1.0.0"，用于与 firmware.version 比较
 * @param hooks            可选；为 NULL 时不写入 mqtt/websocket，也不设置时间
 * @param out              输出解析状态与字符串（由本函数填充）
 */
errcode_t ota_parse_check_response(const char *json_body, const char *current_version,
                                   const ota_parse_hooks_t *hooks, ota_result_t *out);

/** 语义对标 `Ota::IsNewVersionAvailable` */
bool ota_is_newer_version(const char *current_version, const char *new_version);

/**
 * 构建激活 POST JSON（对标 `Ota::GetActivationPayload`）。
 * hmac_hex 为 64 字符十六进制 SHA-256 HMAC；若暂无硬件 HMAC，可传 "" 或 NULL。
 */
errcode_t ota_build_activation_json(const char *serial_number, const char *challenge,
                                      const char *hmac_hex, char *out_buf, size_t out_buf_len);

/** 在 base_url 后拼接 activate 路径 */
void ota_make_activate_url(const char *base_url, char *out, size_t out_len);

/**
 * 配网成功并已保存 SSID 后触发 OTA 检查。
 * 在独立任务中执行，避免撑爆 WiFiTask 栈。
 */
void ota_check_after_wifi_config_success(void);

#endif /* XIAOHONG_OTA_H */
