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
#include "ota_http.h"
#include "ota.h"
#include "log.h"
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
#include "disp_driver.h"
#endif
#if defined(SUPPORT_LFS)
#include "font_download.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "cJSON.h"

uint8_t g_ota_check_connect_to_server_failed = 0;
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
extern osMessageQueueId_t g_disp_event_qid;
#endif

/** OTA 检查响应在堆极小时 cJSON_Parse/malloc 易崩；本文件内用无堆分配扫描解析（不调用 cJSON_InitHooks，避免与 Mongoose 冲突）。 */
static const char *ota_skip_ws(const char *p)
{
    if (p == NULL) {
        return NULL;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    return p;
}

static const char *ota_json_skip_to_matching_close(const char *p)
{
    if (p == NULL || (*p != '{' && *p != '[')) {
        return NULL;
    }
    char stack[48];
    int sp = 0;
    stack[sp++] = *p;
    p++;
    bool instr = false;
    bool esc = false;
    while (*p != '\0') {
        if (instr) {
            if (esc) {
                esc = false;
                p++;
                continue;
            }
            if (*p == '\\') {
                esc = true;
                p++;
                continue;
            }
            if (*p == '"') {
                instr = false;
            }
            p++;
            continue;
        }
        if (*p == '"') {
            instr = true;
            p++;
            continue;
        }
        if (*p == '{') {
            if (sp >= (int)sizeof(stack)) {
                return NULL;
            }
            stack[sp++] = '{';
            p++;
            continue;
        }
        if (*p == '[') {
            if (sp >= (int)sizeof(stack)) {
                return NULL;
            }
            stack[sp++] = '[';
            p++;
            continue;
        }
        if (*p == '}') {
            if (sp == 0 || stack[sp - 1] != '{') {
                return NULL;
            }
            sp--;
            p++;
            if (sp == 0) {
                return p;
            }
            continue;
        }
        if (*p == ']') {
            if (sp == 0 || stack[sp - 1] != '[') {
                return NULL;
            }
            sp--;
            p++;
            if (sp == 0) {
                return p;
            }
            continue;
        }
        p++;
    }
    return NULL;
}

static const char *ota_json_find_named_object(const char *json, const char *name)
{
    char pat[48];
    (void)snprintf(pat, sizeof(pat), "\"%s\"", name);
    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        const char *q = p + strlen(pat);
        q = ota_skip_ws(q);
        if (*q != ':') {
            p++;
            continue;
        }
        q++;
        q = ota_skip_ws(q);
        if (*q == '{') {
            return q;
        }
        /* 第一个匹配可能是 "firmware":null / false / 数字等，须继续找下一个同名键 */
        p++;
    }
    return NULL;
}

static int ota_json_get_string_after_key(const char *obj_open, const char *after_obj, const char *key, char *out,
                                         size_t cap)
{
    char pat[72];
    (void)snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (after_obj == NULL || obj_open == NULL) {
        return -1;
    }
    const char *p = obj_open;
    while (p < after_obj) {
        p = strstr(p, pat);
        if (p == NULL || p >= after_obj) {
            return -1;
        }
        const char *q = p + strlen(pat);
        q = ota_skip_ws(q);
        if (*q == ':') {
            q++;
            q = ota_skip_ws(q);
            if (*q != '"') {
                return -1;
            }
            q++;
            size_t i = 0U;
            while (*q != '\0' && *q != '"' && i + 1U < cap) {
                if (*q == '\\' && q[1] != '\0') {
                    q++;
                }
                out[i++] = *q++;
            }
            out[i] = '\0';
            return 0;
        }
        p++;
    }
    return -1;
}

static int ota_json_get_int_after_key(const char *obj_open, const char *after_obj, const char *key, int *outv)
{
    char pat[72];
    (void)snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(obj_open, pat);
    if (p == NULL || (after_obj != NULL && p >= after_obj)) {
        return -1;
    }
    p += strlen(pat);
    p = ota_skip_ws(p);
    if (*p != ':') {
        return -1;
    }
    p++;
    p = ota_skip_ws(p);
    return (sscanf(p, "%d", outv) == 1) ? 0 : -1;
}

static int ota_json_get_double_after_key(const char *obj_open, const char *after_obj, const char *key, double *outv)
{
    char pat[72];
    (void)snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(obj_open, pat);
    if (p == NULL || (after_obj != NULL && p >= after_obj)) {
        return -1;
    }
    p += strlen(pat);
    p = ota_skip_ws(p);
    if (*p != ':') {
        return -1;
    }
    p++;
    p = ota_skip_ws(p);
    char *endp = NULL;
    *outv = strtod(p, &endp);
    if (endp == p) {
        return -1;
    }
    return 0;
}

static void ota_emit_flat_object(const char *inner_start, const char *inner_end, const char *namespace_,
                                 const ota_parse_hooks_t *hooks)
{
    if (hooks == NULL || inner_start == NULL || inner_end == NULL || inner_start >= inner_end) {
        return;
    }
    if (hooks->on_string == NULL && hooks->on_int == NULL) {
        return;
    }

    const char *p = inner_start;
    while (p < inner_end) {
        p = ota_skip_ws(p);
        if (*p != '"') {
            break;
        }
        p++;
        char kbuf[96];
        size_t ki = 0U;
        while (p < inner_end && *p != '\0' && *p != '"' && ki + 1U < sizeof(kbuf)) {
            if (*p == '\\' && (p + 1) < inner_end && p[1] != '\0') {
                p++;
            }
            kbuf[ki++] = *p++;
        }
        kbuf[ki] = '\0';
        if (*p != '"') {
            break;
        }
        p++;
        p = ota_skip_ws(p);
        if (*p != ':') {
            break;
        }
        p++;
        p = ota_skip_ws(p);
        if (*p == '"') {
            p++;
            char vbuf[512];
            size_t vi = 0U;
            while (p < inner_end && *p != '\0' && *p != '"' && vi + 1U < sizeof(vbuf)) {
                if (*p == '\\' && (p + 1) < inner_end && p[1] != '\0') {
                    p++;
                }
                vbuf[vi++] = *p++;
            }
            vbuf[vi] = '\0';
            if (*p == '"') {
                p++;
            }
            if (hooks->on_string != NULL) {
                hooks->on_string(namespace_, kbuf, vbuf, hooks->user);
            }
        } else {
            int iv = 0;
            if (sscanf(p, "%d", &iv) == 1) {
                if (hooks->on_int != NULL) {
                    hooks->on_int(namespace_, kbuf, iv, hooks->user);
                }
            }
            while (p < inner_end && *p != ',' && *p != '}') {
                p++;
            }
        }
        p = ota_skip_ws(p);
        if (*p == ',') {
            p++;
        }
    }
}

static void ota_apply_server_time_scan(const char *obj_open, const ota_parse_hooks_t *hooks, ota_result_t *out)
{
    const char *after = ota_json_skip_to_matching_close(obj_open);
    if (after == NULL) {
        return;
    }

    double ts = 0.0;
    if (ota_json_get_double_after_key(obj_open, after, "timestamp", &ts) != 0) {
        return;
    }
    int tz_min = 0;
    (void)ota_json_get_int_after_key(obj_open, after, "timezone_offset", &tz_min);

    ts += (double)tz_min * 60.0 * 1000.0;
    out->has_server_time = true;

    if (hooks != NULL && hooks->on_server_time != NULL) {
        hooks->on_server_time(ts, tz_min, hooks->user);
    } else {
        struct timeval tv;
        tv.tv_sec = (time_t)(ts / 1000.0);
        tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;
        (void)settimeofday(&tv, NULL);
    }
}

static void ota_strcpy_safe(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, dst_cap, "%s", src);
}

static int ota_split_version(const char *version, int *parts, int max_parts)
{
    int n = 0;
    const char *p = (version != NULL) ? version : "";

    while (n < max_parts && p != NULL && *p != '\0') {
        int v = 0;
        int consumed = 0;
        if (sscanf(p, "%d%n", &v, &consumed) != 1) {
            break;
        }
        parts[n++] = v;
        p += consumed;
        if (*p == '.') {
            p++;
        } else {
            break;
        }
    }
    return n;
}

bool ota_is_newer_version(const char *current_version, const char *new_version)
{
    int cur[16];
    int neu[16];
    int nc = ota_split_version(current_version != NULL ? current_version : "0", cur, 16);
    int nn = ota_split_version(new_version != NULL ? new_version : "0", neu, 16);

    int min_n = nc < nn ? nc : nn;
    for (int i = 0; i < min_n; i++) {
        if (neu[i] > cur[i]) {
            return true;
        }
        if (neu[i] < cur[i]) {
            return false;
        }
    }
    return nn > nc;
}

errcode_t ota_parse_check_response(const char *json_body, const char *current_version,
                                   const ota_parse_hooks_t *hooks, ota_result_t *out)
{
    if (out == NULL) {
        return ERRCODE_FAIL;
    }
    memset(out, 0, sizeof(*out));
    out->activation_timeout_ms = 30000;

    if (json_body == NULL) {
        log_error("[OTA-Parse] json_body is NULL\r\n");
        return ERRCODE_FAIL;
    }

    const char *parse_ptr = json_body;
    size_t jlen = strlen(json_body);
    if (jlen >= 3U && (unsigned char)parse_ptr[0] == 0xEFU && (unsigned char)parse_ptr[1] == 0xBBU &&
        (unsigned char)parse_ptr[2] == 0xBFU) {
        parse_ptr += 3;
    }
    parse_ptr = ota_skip_ws(parse_ptr);
    if (*parse_ptr != '{') {
        log_error("[OTA-Parse] json_body is not a valid JSON object\r\n");
        return ERRCODE_FAIL;
    }

    const char *act = ota_json_find_named_object(parse_ptr, "activation");
    if (act != NULL) {
        const char *after = ota_json_skip_to_matching_close(act);
        if (after != NULL) {
            (void)ota_json_get_string_after_key(act, after, "message", out->activation_message,
                                                sizeof(out->activation_message));
            if (ota_json_get_string_after_key(act, after, "code", out->activation_code, sizeof(out->activation_code)) ==
                0) {
                out->has_activation_code = true;
            }
            if (ota_json_get_string_after_key(act, after, "challenge", out->activation_challenge,
                                              sizeof(out->activation_challenge)) == 0) {
                out->has_activation_challenge = true;
            }
            int tms = 30000;
            if (ota_json_get_int_after_key(act, after, "timeout_ms", &tms) == 0) {
                out->activation_timeout_ms = tms;
            }
        }
    }

    const char *mqtt = ota_json_find_named_object(parse_ptr, "mqtt");
    if (mqtt != NULL) {
        const char *after = ota_json_skip_to_matching_close(mqtt);
        if (after != NULL) {
            out->has_mqtt_config = true;
            ota_emit_flat_object(mqtt + 1U, after - 1U, "mqtt", hooks);
        }
    }

    const char *websocket = ota_json_find_named_object(parse_ptr, "websocket");
    if (websocket != NULL) {
        const char *after = ota_json_skip_to_matching_close(websocket);
        if (after != NULL) {
            out->has_websocket_config = true;
            (void)ota_json_get_string_after_key(websocket, after, "url", out->websocket_url,
                                                sizeof(out->websocket_url));
            (void)ota_json_get_string_after_key(websocket, after, "token", out->websocket_token,
                                                sizeof(out->websocket_token));
            {
                int wver = 0;
                if (ota_json_get_int_after_key(websocket, after, "version", &wver) == 0) {
                    out->websocket_protocol_version = wver;
                }
            }
            ota_emit_flat_object(websocket + 1U, after - 1U, "websocket", hooks);
        }
    }

    const char *server_time = ota_json_find_named_object(parse_ptr, "server_time");
    if (server_time != NULL) {
        ota_apply_server_time_scan(server_time, hooks, out);
    }

    const char *firmware = ota_json_find_named_object(parse_ptr, "firmware");
    if (firmware != NULL) {
        const char *after = ota_json_skip_to_matching_close(firmware);
        if (after != NULL) {
            bool have_v =
                (ota_json_get_string_after_key(firmware, after, "version", out->firmware_version,
                                                 sizeof(out->firmware_version)) == 0);
            bool have_u = (ota_json_get_string_after_key(firmware, after, "url", out->firmware_url,
                                                         sizeof(out->firmware_url)) == 0);
        }
    }

    /* chunked 或键名差异导致扫描未取到 url 时，用 cJSON 再取 firmware 下 url / downloadUrl / download_url */
    if (out->firmware_url[0] == '\0' && json_body != NULL) {
        cJSON *root = cJSON_Parse(json_body);
        if (root != NULL) {
            cJSON *fw = cJSON_GetObjectItem(root, "firmware");
            if (fw != NULL && cJSON_IsObject(fw)) {
                static const char *const url_keys[] = { "url" };
                for (size_t ki = 0; ki < sizeof(url_keys) / sizeof(url_keys[0]); ki++) {
                    cJSON *u = cJSON_GetObjectItem(fw, url_keys[ki]);
                    if (cJSON_IsString(u) && u->valuestring != NULL && u->valuestring[0] != '\0') {
                        ota_strcpy_safe(out->firmware_url, sizeof(out->firmware_url), u->valuestring);
                        break;
                    }
                }
                if (out->firmware_version[0] == '\0') {
                    cJSON *ver = cJSON_GetObjectItem(fw, "version");
                    if (cJSON_IsString(ver) && ver->valuestring != NULL) {
                        ota_strcpy_safe(out->firmware_version, sizeof(out->firmware_version), ver->valuestring);
                    }
                }
            }
            cJSON_Delete(root);
        }
    }

    return ERRCODE_SUCC;
}

errcode_t ota_build_activation_json(const char *serial_number, const char *challenge,
                                    const char *hmac_hex, char *out_buf, size_t out_buf_len)
{
    if (out_buf == NULL || out_buf_len < 32U) {
        return ERRCODE_FAIL;
    }
    if (serial_number == NULL || challenge == NULL) {
        return ERRCODE_FAIL;
    }
    const char *hmac = (hmac_hex != NULL) ? hmac_hex : "";

    cJSON *payload = cJSON_CreateObject();
    if (payload == NULL) {
        return ERRCODE_FAIL;
    }
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number);
    cJSON_AddStringToObject(payload, "challenge", challenge);
    cJSON_AddStringToObject(payload, "hmac", hmac);

    char *json_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (json_str == NULL) {
        return ERRCODE_FAIL;
    }
    size_t jl = strlen(json_str);
    bool ok = (jl + 1U <= out_buf_len);
    if (ok) {
        memcpy(out_buf, json_str, jl + 1U);
    }
    free(json_str);
    return ok ? ERRCODE_SUCC : ERRCODE_FAIL;
}

void ota_make_activate_url(const char *base_url, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0U) {
        return;
    }
    if (base_url == NULL || base_url[0] == '\0') {
        out[0] = '\0';
        return;
    }
    size_t n = strlen(base_url);
    if (n > 0U && base_url[n - 1U] == '/') {
        (void)snprintf(out, out_len, "%sactivate", base_url);
    } else {
        (void)snprintf(out, out_len, "%s/activate", base_url);
    }
}

/** Mongoose + cJSON 调用链较深，勿在 WiFiTask(4KB 栈) 内同步跑 OTA，改为独立任务。 */
static volatile uint8_t g_ota_check_spawned;
static int ota_post_json_append(char *out, size_t cap, size_t *off, const char *s);

static bool ota_startup_check_font_version(void)
{
    bool ret = false;

    if (get_ota_url() == NULL) {
        log_error("[OtaCheck] ota_startup_check_font_version: get_ota_url NG\r\n");
        return false;
    }

    const char *dev_id_str = get_device_id_str();
    if (dev_id_str == NULL) {
        log_error("[OtaCheck] ota_startup_check_font_version: get_device_id_str NG\r\n");
        return false;
    }

    char post_json[768];
    size_t pj = 0U;

    ota_result_t ota_out;
    memset(&ota_out, 0, sizeof(ota_out));

    if (ota_post_json_append(post_json, sizeof(post_json), &pj,
                        "{\"version\":2,\"language\":\"zh-CN\",\"mac_address\":\"") != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj, "\",\"uuid\":\"") != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj,
                                         "\",\"application\":{\"version\":\"") != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj, get_version_font_str()) != 0 ||
            ota_post_json_append(post_json, sizeof(post_json), &pj,
                                         "\"},\"board\":{\"type\":\"xiaohong_font\"}}") != 0) {
        log_error("[OtaCheck] check_font_version post JSON build failed\r\n");
    } else {
        post_json[pj] = '\0';
        log_debug("[OtaCheck] check_font_version post JSON: len[%d]{{\r\n%s\r\n}}\r\n", (int)pj, post_json);

        ota_http_request_t ota_req = {0};
        ota_req.activation_version = "1";
        ota_req.device_id = dev_id_str;
        ota_req.client_id = dev_id_str;
        ota_req.post_body = post_json;
        ota_req.debug_print_io = 0U;

        errcode_t err = ota_check_version_http(get_ota_url(), get_version_ws63_str(),
                                               &ota_req, NULL, &ota_out);
        if (err == ERRCODE_SUCC && ota_out.firmware_version[0] != '\0') {
            log_debug("[OtaCheck] check_font_version: ver[%s]\r\n", ota_out.firmware_version);
            if (check_version_str(get_version_font_str(), ota_out.firmware_version)) {
                //check_font_bin_exists(true);
                // remove font.bin
                return true;
            } else {
                return false;
            }
        } else {
            log_info("[OtaCheck] check_font_version: err[%d]\r\n", (int)err);
        }
    }
    return ret;
}

static bool ota_startup_must_fetch_font_first(void)
{
#if defined(SUPPORT_LFS)
    if (xh_font_bin_installed() == 0) {  // 字库未安装，需要下载字库
        return true;
    } else {
        // 字库已安装，需要根据字库版本号来决定是否需要重新下载字库
        return ota_startup_check_font_version();
    }
#else
    //log_warn("[OtaCheck] lfs not enabled\r\n");
    return false;
#endif
}

/**
 * 将一次 OTA JSON 解析结果写入 settings，并按与 ota_check_task 相同规则通知 main（新固件 / Run_Ready）。
 * 若已绑定且拿到 ws/新固件配置但字库未安装，则先投递 eSettings_Start_Font_If_Needed，字库完成重启后再走 OTA/Run_Ready。
 * @return true 表示已投递 eSettings_Ota_FW_Update、eSettings_Run_Ready 或 eSettings_Start_Font_If_Needed（可安全跳过紧随其后的版本检查 HTTP）。
 */
static bool ota_apply_parsed_result_notify_main(const ota_result_t *ota_out)
{
    if (ota_out == NULL) {
        return false;
    }

    if (ota_out->websocket_url[0] != '\0') {
        (void)set_websocket_server_url(ota_out->websocket_url);
    }
    if (ota_out->websocket_token[0] != '\0') {
        (void)set_websocket_server_token(ota_out->websocket_token);
    }
    if (ota_out->has_websocket_config) {
        set_websocket_protocol_version(ota_out->websocket_protocol_version);
    }

    if (ota_out->has_websocket_config) {
        if (ota_startup_must_fetch_font_first()) {
            post_msg_event(g_main_event_qid, eSettings_Start_Font_If_Needed);
            return true;
        }
        post_msg_event(g_main_event_qid, eSettings_Run_Ready);
        return true;
    }
    return false;
}

/** 分段拼接 JSON，避免大段 snprintf 在 libc 堆紧张时触发 malloc。 */
static int ota_post_json_append(char *out, size_t cap, size_t *off, const char *s)
{
    if (s == NULL) {
        return 0;
    }
    size_t l = strlen(s);
    if (*off + l >= cap) {
        return -1;
    }
    memcpy(out + *off, s, l);
    *off += l;
    return 0;
}

/** 带重试的 OTA 检查 POST。 */
static errcode_t ota_run_check_version_http_ex(char *post_json, ota_http_request_t *ota_req, ota_result_t *out,
                                               int max_attempts, uint32_t gap_ms)
{
    ota_req->post_body = post_json;
    errcode_t err = ERRCODE_FAIL;
    if (max_attempts < 1) {
        max_attempts = 1;
    }
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        if (attempt > 0) {
            osal_msleep(gap_ms);
        }
        //log_info("[OtaCheck] get_ota_url: %s\r\n", get_ota_url());
        err = ota_check_version_http(get_ota_url(), get_version_ws63_str(), ota_req, NULL, out);
        if (err == ERRCODE_SUCC) {
            break;
        }
    }
    return err;
}

static errcode_t ota_run_check_version_http(char *post_json, ota_http_request_t *ota_req, ota_result_t *out)
{
    return ota_run_check_version_http_ex(post_json, ota_req, out, 3, 1000U);
}

/**
 * POST /activate 成功后：连续 netconn + 服务端写入可能有延迟；过短间隔易整轮 err=-1。
 * 每轮内多次 HTTP 尝试 + 轮间更长休眠，尽量在回退首次快照前拿到新 token。
 */
static errcode_t ota_run_second_check_after_activate(char *post_json, ota_http_request_t *ota_req, ota_result_t *out)
{
    errcode_t last_err = ERRCODE_FAIL;
    const int max_rounds = 8;
    for (int r = 0; r < max_rounds; r++) {
        if (r == 0) {
            osal_msleep(4000U);
        } else {
            osal_msleep(5000U);
        }
        /* 每轮 5 次 POST、间隔 1.5s，避免与单次 3 次重试相比过早放弃 */
        last_err = ota_run_check_version_http_ex(post_json, ota_req, out, 5, 1500U);
        if (last_err == ERRCODE_SUCC) {
            return last_err;
        }
        printf("[OtaCheck] second check round %d/%d err=%d\r\n", r + 1, max_rounds, (int)last_err);
    }
    return last_err;
}

static void ota_show_activation_hint(const ota_result_t *r)
{
//  type: 0: unknown msg, 1: activation_code, 2: activation_message, 3: activation_challenge
    uint8_t type = 0;
    char buf[256] = {0};

    if (r == NULL) {
        return;
    }

    memset_s(buf, sizeof(buf), '\0', sizeof(buf));
    if (r->has_activation_code && r->activation_code[0] != '\0') {
        type = 1;
        (void)snprintf(buf, sizeof(buf), "%.48s", r->activation_code);
    } else if (r->activation_message[0] != '\0') {
        type = 2;
        (void)snprintf(buf, sizeof(buf), "%.240s", r->activation_message);
    } else if (r->has_activation_challenge) {
        type = 3;
        (void)snprintf(buf, sizeof(buf), "设备待激活，请按提示完成绑定");
    }

    if (strlen(buf) > 0) {
        log_info("[OtaCheck] %s: type[%d] buf[%s]\r\n", __func__, type, buf);
        set_activation_hint(type, buf, strlen(buf));
    }
}

/**
 * 先 CheckVersion；有新固件则通知升级并结束；
 * 否则若需激活则 POST /activate；若尚未通知 main（例如激活 200 未带 ws），再 CheckVersion 一次。
 */
static void ota_check_task(void *arg)
{
    (void)arg;
    uint8_t msg_send = 0;

    char *dev_id_str = get_device_id_str();
    if (dev_id_str == NULL) {
        log_error("[OtaCheck] get_device_id_str NG, osThreadExit\r\n");
        g_ota_check_spawned = 0;
        osThreadExit();
    }

    char post_json[768];
    size_t pj = 0U;
    if (ota_post_json_append(post_json, sizeof(post_json), &pj,
                             "{\"version\":2,\"language\":\"zh-CN\",\"mac_address\":\"") != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj, "\",\"uuid\":\"") != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj,
                             "\",\"application\":{\"version\":\"") != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj, get_version_ws63_str()) != 0 ||
        ota_post_json_append(post_json, sizeof(post_json), &pj,
                             "\"},\"board\":{\"type\":\"xiaohong_v1\"}}") != 0) {
        g_ota_check_spawned = 0;
        log_error("[OtaCheck] ota_post_json_append NG, osThreadExit\r\n");
        osThreadExit();
    }
    post_json[pj] = '\0';

    ota_http_request_t ota_req = {0};
    ota_req.activation_version = "1";
    ota_req.device_id = dev_id_str;
    ota_req.client_id = dev_id_str;

    ota_result_t ota_out = {0};
    errcode_t err = ota_run_check_version_http(post_json, &ota_req, &ota_out);
    printf("[OtaCheck] ota_check_task: err[%d] ver[%s] has_ws[%d]\r\n", (int)err,
              ota_out.firmware_version, ota_out.has_websocket_config ? 1 : 0);

    if (err != ERRCODE_SUCC) {
        g_ota_check_spawned = 0;
        log_error("[OtaCheck] ota_run_check_version_http error, ota_check_task exit\r\n");
        g_ota_check_connect_to_server_failed = 1;
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
        if (g_disp_event_qid != NULL) {
            uint8_t ev = eDisp_Hand_Shaking;
            (void)osMessageQueuePut(g_disp_event_qid, (const void *)&ev, 0, 0);
        }
#endif
        osThreadExit();
        return;
    }

// 在这里进行版本比较，版本高于本地版本，需要进行OTA升级
// 当前只检测ws63的版本，还未检测P4的版本
    if (check_version_str(get_version_ws63_str(), ota_out.firmware_version) == 1) {
        log_info("[OtaCheck] ota_check_task: version[%s] is newer than local version, OTA upgrade\r\n", ota_out.firmware_version);
        (void)set_ota_firmware_address(ota_out.firmware_url);
        (void)set_ota_firmware_pending_meta(ota_out.firmware_version, ota_out.firmware_size,
                                            (ota_out.firmware_sha256[0] != '\0') ? ota_out.firmware_sha256 : NULL);
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
        uint8_t msg_send = eDisp_Ota_FW_Update;
        (void)osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);
#endif
        post_msg_event(g_main_event_qid, eSettings_Ota_FW_Update);

        //(void)ota_apply_parsed_result_notify_main(&ota_out);
        g_ota_check_spawned = 0;
        osThreadExit();
        return;
    }

    bool notified_main = false;
    bool tried_activation = false;
    /** POST /activate 曾返回 200（body 可能为空，未写入 act_on_200） */
    bool activate_http_200 = false;

    if (ota_out.has_activation_code || ota_out.has_activation_challenge) {
        log_info("[OtaCheck] activate has_activation_code[%d] has_activation_challenge[%d]\r\n",
            ota_out.has_activation_code, ota_out.has_activation_challenge);
        tried_activation = true;
        ota_show_activation_hint(&ota_out);

        if (ota_out.has_activation_challenge && ota_out.activation_challenge[0] != '\0') {
            char act_json[768];
            if (ota_build_activation_json(dev_id_str, ota_out.activation_challenge, "", act_json, sizeof(act_json)) ==
                ERRCODE_SUCC) {
                //log_debug("[OtaCheck] act_json=\r\n%s\r\n", act_json);
                for (int i = 0; i < 20; i++) {
                    int http_st = -1;
                    ota_result_t act_on_200 = {0};
                    err = ota_activate_http(get_ota_url(), &ota_req, act_json, &http_st, &act_on_200);
                    if (err != ERRCODE_SUCC) {
                        log_error("[OtaCheck] activate HTTP transport fail i=%d\r\n", i);
                        /* netconn 池耗尽后需数秒～数十秒才回收；10s 往往不够 */
                        osal_msleep(30000U);
                        continue;
                    }
                    log_info("[OtaCheck] activate httpst[%d]\r\n", http_st);
                    if (http_st == 200) {
                        log_info("[OtaCheck] POST /activate OK\r\n");
                        set_activation_hint(4, "Device activate successfull.", strlen("Device activate successfull."));
                        activate_http_200 = true;
                        if (ota_apply_parsed_result_notify_main(&act_on_200)) {
                            log_info("[OtaCheck] POST /activate OK, notified_main\r\n");
                            notified_main = true;
                        }
                        break;
                    }
                    if (http_st == 202) {
                        /* 过密轮询会拖垮 MEMP；略拉长减轻与 TIME_WAIT 叠加 */
                        osal_msleep(8000U);   // 5轮约40s
                        continue;
                    }
                    log_error("[OtaCheck] activate unexpected HTTP %d\r\n", http_st);
                    osal_msleep(15000U);
                }
            } else {
                log_error("[OtaCheck] ota_build_activation_json NG\r\n");
            }
        } else {
            log_error("[OtaCheck] activation without challenge, skip POST /activate\r\n");
        }
    }

    if (!notified_main) {
        if (tried_activation) {
            /* 激活循环大量 netconn，紧接着 CheckVersion 易再失败；统一垫一段时间 */
            restart_system(3);
            return;

            ota_result_t ota2 = {0};
            if (activate_http_200) {
                err = ota_run_second_check_after_activate(post_json, &ota_req, &ota2);
            } else {
                err = ota_run_check_version_http(post_json, &ota_req, &ota2);
            }
            log_info("[OtaCheck] second check err=%d new_ver=%d has_ws=%d tok=%d\r\n",
                   (int)err,
                   ota2.has_new_version ? 1 : 0,
                   ota2.has_websocket_config ? 1 : 0,
                   (ota2.websocket_token[0] != '\0') ? 1 : 0);
            if (err == ERRCODE_SUCC) {
                if (ota_apply_parsed_result_notify_main(&ota2)) {
                    notified_main = true;
                }
            }
            /* 激活 200 常无 body；首次 CheckVersion 可能已带 ws。二次全失败时回退首次结果（token 可能仍非绑定后最新）。 */
            if (!notified_main && activate_http_200 &&
                (ota_out.has_websocket_config || ota_out.has_new_version)) {
                if (ota_out.websocket_token[0] == '\0') {
                    log_info("[OtaCheck] WARN fallback first check: empty ws token, server may reject WS\r\n");
                }
                log_info("[OtaCheck] apply first check after activate OK (empty act body or second check miss)\r\n");
                (void)ota_apply_parsed_result_notify_main(&ota_out);
            }
        } else {
            (void)ota_apply_parsed_result_notify_main(&ota_out);
        }
    }

    if (ota_startup_must_fetch_font_first()) {
        log_info("[OtaCheck] ota_check_task: going to fetch font bin file\r\n");
        post_msg_event(g_main_event_qid, eSettings_Start_Font_If_Needed);
    }
    g_ota_check_spawned = 0;
    osThreadExit();
}

/**
 * 配网成功并已保存 SSID 后触发 OTA 检查。
 * 在独立任务中执行，避免撑爆 WiFiTask 栈。
 */
void ota_check_after_wifi_config_success(void)
{
    if (g_ota_check_spawned != 0U) {
        log_info("[OtaCheck] OTA check already running\r\n");
        return;
    }
    g_ota_check_spawned = 1;

    task_config_t ota_check_task_conf = {
        .name = "OtaCheckTask",
        .stack_size = 1024 * 20,
        .priority = 23,
        .func = (void *)ota_check_task,
    };
    if (create_task(&ota_check_task_conf, NULL) == 0U) {
        log_error("[OtaCheck] OtaCheckTask create_task failed\r\n");
        g_ota_check_spawned = 0;
    }
}
