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
/**
 * @file ota_http.h
 * @brief OTA 版本检查 HTTP 封装：统一走 mongoose（mg_http_connect），再 `ota_parse_check_response`。
 *
 * 支持 `http://` 与 `https://`（工程以 MG_TLS=MG_TLS_BUILTIN 编译，TLS 由 xh_tls_apply()
 * 按 URL 挂载并内嵌 CA 严格校验）；URL 主机可为域名或 IPv4 字面量（lwIP DNS）。
 */

#ifndef XIAOHONG_OTA_HTTP_H
#define XIAOHONG_OTA_HTTP_H

#include <stdint.h>

#include "errcode.h"
#include "ota.h"

/** HTTP 请求参数 */
typedef struct ota_http_request {
    /** "GET" 或 "POST"；可为 NULL：有 post_body 则用 POST，否则 GET */
    const char *method;
    /** POST 正文；NULL 或空串表示 GET */
    const char *post_body;
    /** Activation-Version，默认 "1" */
    const char *activation_version;
    const char *device_id;
    const char *client_id;
    const char *user_agent;
    /** Accept-Language，默认 "zh-CN" */
    const char *accept_language;
    /** 可选，有则带 Serial-Number 头 */
    const char *serial_number;
    /** 连接/等待总超时（毫秒），0 表示 30000 */
    uint32_t timeout_ms;
    /** 非 0 时打印 POST 正文与 HTTP 响应 body（串口调试） */
    uint8_t debug_print_io;
} ota_http_request_t;

/**
 * 向 `url` 发起 OTA 检查请求，HTTP 200 时将 body 交给 `ota_parse_check_response`。
 *
 * @param url                     完整 URL，如 http://host:8002/xiaohong/ota/
 * @param current_firmware_version 当前固件版本，传给解析层
 * @param http_req                可为 NULL：使用 GET、默认头占位（建议至少填 device_id/client_id）
 * @param hooks                   传给 `ota_parse_check_response`
 * @param out                     解析结果
 */
errcode_t ota_check_version_http(const char *url, const char *current_firmware_version,
                                 const ota_http_request_t *http_req,
                                 const ota_parse_hooks_t *hooks, ota_result_t *out);

/**
 * POST 到 ota_make_activate_url(check_base_url)。
 * 成功收到 HTTP 应答且解析出状态码时返回 ERRCODE_SUCC；`http_status_out` 可为 NULL，非空时写入 200/202。
 * `parse_on_200` 可为 NULL；非空且状态码为 200 时，将 body 交给 `ota_parse_check_response`（与版本检查接口同形 JSON，可含 websocket/firmware）。
 */
errcode_t ota_activate_http(const char *check_base_url, const ota_http_request_t *http_req,
                            const char *activate_json_body, int *http_status_out, ota_result_t *parse_on_200);

#endif /* XIAOHONG_OTA_HTTP_H */
