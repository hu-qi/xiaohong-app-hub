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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "osal_task.h"
#include "task_entry.h"
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
#include "disp_driver.h"
#endif
#include "mongoose.h"
#include "ota_fw_upgrade.h"
#include "settings.h"
#include "uart.h"
#include "nv.h"

#if defined(SUPPORT_LFS)
#include <fcntl.h>
// #include "http_lfs_download.h"
#include "littlefs_adapt.h"
#endif

#ifndef FOTA_ENABLE_UPG
#define FOTA_ENABLE_UPG 1
#endif

#ifdef FOTA_ENABLE_UPG
#include "upg.h"
#endif
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
extern osMessageQueueId_t g_disp_event_qid;
#endif
static errcode_t fota_verify_final_sha256(const char *expected_sha256);
//static errcode_t fota_save_version_to_nv(const char *version);

const char * const g_fota_state_str[] = {
    FOTA_STATE_MAP(FOTA_STATE_STR)
};

#define FOTA_HTTP_RESP_MAX   1024
#define FOTA_DOWNLOAD_CHUNK_MAX (8 * 1024)
#define FOTA_DEFAULT_TIMEOUT  30000
#define FOTA_POLL_INTERVAL_MS 10

#define FOTA_NV_KEY_VERSION     0xF000

#ifndef FOTA_HEXDUMP
#define FOTA_HEXDUMP 0
#endif

#if FOTA_HEXDUMP
#define fota_hexdump(buf, len) mg_hexdump(buf, len)
#else
#define fota_hexdump(buf, len) ((void)0)
#endif

static fota_status_t s_fota_status = {
    .state = FOTA_STATE_IDLE,
    .download_progress = 0,
    .total_size = 0,
    .downloaded_size = 0,
    .last_error = 0
};

volatile uint8_t g_ota_fw_update_progress = 0;
static fota_progress_cb s_progress_cb = NULL;
static void *s_user_data = NULL;

static mg_sha256_ctx s_sha256_ctx;
static uint8_t s_sha256_hash[32];

#if FOTA_ENABLE_UPG
static void *fota_upg_malloc(const uint32_t size)
{
    return malloc(size);
}

static void fota_upg_free(void *ptr)
{
    free(ptr);
}

static void fota_upg_serial_putc(const char c)
{
    uint8_t ch = (uint8_t)c;
    uapi_uart_write(0, &ch, 1, 0);
}

static const upg_func_t s_upg_funcs = {
    .malloc = fota_upg_malloc,
    .free = fota_upg_free,
    .serial_putc = fota_upg_serial_putc
};
#endif

static char *fota_json_get_string(const char *json, size_t json_len, const char *key, char *out, size_t out_cap)
{
    if (json == NULL || key == NULL || out == NULL || out_cap == 0) {
        return NULL;
    }

    size_t key_len = strlen(key);
    char pattern[128];
    if (key_len + 3 >= sizeof(pattern)) {
        return NULL;
    }
    pattern[0] = '\"';
    memcpy(pattern + 1, key, key_len);
    pattern[key_len + 1] = '\"';
    pattern[key_len + 2] = '\0';

    const char *p = json;
    const char *json_end = json + json_len;

    while (p < json_end) {
        char *found = strstr(p, pattern);
        if (found == NULL) {
            break;
        }

        p = found + key_len + 2;
        p = strchr(p, ':');
        if (p == NULL) {
            return NULL;
        }
        p++;

        while (p < json_end && isspace((unsigned char)*p)) {
            p++;
        }

        if (p >= json_end) {
            return NULL;
        }

        if (*p == '\"') {
            p++;
            size_t i = 0;
            while (i + 1 < out_cap && *p != '\"' && *p != '\0') {
                out[i++] = *p++;
            }
            out[i] = '\0';
            if (i > 0) {
                return out;
            }
        } else if (*p != '{' && *p != '[' && *p != '}' && *p != ']') {
            size_t i = 0;
            while (i + 1 < out_cap && *p != ',' && *p != '}' && *p != '\0' && *p != '\n') {
                out[i++] = *p++;
            }
            out[i] = '\0';
            if (i > 0) {
                return out;
            }
        }
    }

    return NULL;
}

static int fota_json_get_int(const char *json, size_t json_len, const char *key, int *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return -1;
    }

    size_t key_len = strlen(key);
    char pattern[128];
    if (key_len + 3 >= sizeof(pattern)) {
        return -1;
    }
    pattern[0] = '\"';
    memcpy(pattern + 1, key, key_len);
    pattern[key_len + 1] = '\"';
    pattern[key_len + 2] = '\0';

    const char *p = json;
    const char *json_end = json + json_len;

    while (p < json_end) {
        char *found = strstr(p, pattern);
        if (found == NULL) {
            break;
        }

        p = found + key_len + 2;
        p = strchr(p, ':');
        if (p == NULL) {
            return -1;
        }
        p++;

        while (p < json_end && isspace((unsigned char)*p)) {
            p++;
        }

        if (p >= json_end || (!isdigit((unsigned char)*p) && *p != '-')) {
            return -1;
        }

        int val = 0;
        int sign = 1;
        if (*p == '-') {
            sign = -1;
            p++;
        }
        while (p < json_end && isdigit((unsigned char)*p)) {
            val = val * 10 + (*p - '0');
            p++;
        }
        *out = val * sign;
        return 0;
    }

    return -1;
}

static errcode_t fota_parse_info_json(const char *json_body, size_t json_len, fota_firmware_info_t *info)
{
    if (json_body == NULL || info == NULL || json_len == 0) {
        return ERRCODE_FAIL;
    }

    if (json_len > FOTA_HTTP_RESP_MAX) {
        json_len = FOTA_HTTP_RESP_MAX;
    }

    memset(info, 0, sizeof(fota_firmware_info_t));

    log_debug("[OtaFwUpg] JSON body: %.*s\r\n", (int)json_len, json_body);

    const char *firmware_pos = strstr(json_body, "\"firmware\"");
    if (firmware_pos == NULL) {
        log_error("[OtaFwUpg] Parse error: \"firmware\" key not found\r\n");
        return ERRCODE_FAIL;
    }

    const char *obj_start = strchr(firmware_pos, '{');
    if (obj_start == NULL) {
        return ERRCODE_FAIL;
    }

    const char *obj_end = obj_start;
    int brace_count = 0;
    while (obj_end < json_body + json_len) {
        if (*obj_end == '{') {
            brace_count++;
        } else if (*obj_end == '}') {
            brace_count--;
            if (brace_count == 0) {
                obj_end++;
                break;
            }
        }
        obj_end++;
    }

    size_t obj_len = obj_end - obj_start;
    if (obj_len == 0 || obj_len > json_len) {
        return ERRCODE_FAIL;
    }

    if (fota_json_get_string(obj_start, obj_len, "version", info->version, sizeof(info->version)) == NULL) {
        log_error("[OtaFwUpg] Parse error: \"version\" not found\r\n");
        return ERRCODE_FAIL;
    }
    log_info("[OtaFwUpg] Parse: version=%s\r\n", info->version);

    if (fota_json_get_string(obj_start, obj_len, "url", info->url, sizeof(info->url)) == NULL) {
        log_error("[OtaFwUpg] Parse error: \"url\" not found\r\n");
        return ERRCODE_FAIL;
    }
    log_info("[OtaFwUpg] Parse: url=%s\r\n", info->url);

    if (fota_json_get_string(obj_start, obj_len, "sha256", info->sha256, sizeof(info->sha256)) == NULL) {
        log_error("[OtaFwUpg] Parse error: \"sha256\" not found\r\n");
        return ERRCODE_FAIL;
    }
    log_info("[OtaFwUpg] Parse: sha256=%s\r\n", info->sha256);

    int size_val = 0;
    if (fota_json_get_int(obj_start, obj_len, "size", &size_val) == 0 && size_val > 0) {
        info->size = (uint32_t)size_val;
        log_info("[OtaFwUpg] Parse: size=%u\r\n", info->size);
    } else {
        log_error("[OtaFwUpg] Parse error: \"size\" invalid\r\n");
    }

    return ERRCODE_SUCC;
}

typedef struct {
    int complete;
    int success;
    char *data;
    uint32_t data_len;
    uint32_t data_cap;
    char url[256];
} fota_http_ctx_t;

static void fota_http_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    fota_http_ctx_t *ctx = (fota_http_ctx_t *)c->fn_data;
    struct mg_http_message *hm = NULL;

    if (ev == MG_EV_CONNECT) {
        struct mg_str host = mg_url_host(ctx->url);
        mg_printf(c,
                  "GET %s HTTP/1.0\r\n"
                  "Host: %.*s\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  mg_url_uri(ctx->url),
                  (int)host.len, host.buf);
    } else if (ev == MG_EV_HTTP_MSG) {
        hm = (struct mg_http_message *)ev_data;
        if (hm->body.len > 0 && hm->body.len < ctx->data_cap) {
            memcpy(ctx->data, hm->body.buf, hm->body.len);
            ctx->data_len = (uint32_t)hm->body.len;
            ctx->data[ctx->data_len] = '\0';
            ctx->success = 1;
        }
        ctx->complete = 1;
    } else if (ev == MG_EV_ERROR) {
        log_error("[OtaFwUpg] HTTP ev error: %s\r\n", (char *)ev_data);
        ctx->complete = 1;
        ctx->success = 0;
    } else if (ev == MG_EV_CLOSE) {
        if (!ctx->complete) {
            ctx->complete = 1;
            ctx->success = 0;
        }
    }
}

static errcode_t fota_http_get(const char *url, char *response, uint32_t resp_cap, uint32_t timeout_ms)
{
    if (url == NULL || response == NULL || resp_cap < 64) {
        log_error("[OtaFwUpg] HTTP get: invalid params\r\n");
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] HTTP get: url=%s, timeout=%ums\r\n", url, timeout_ms);

    struct mg_mgr mgr;
    struct mg_connection *c;
    fota_http_ctx_t ctx = {0};

    ctx.data = response;
    ctx.data_cap = resp_cap - 1;
    if (strlen(url) + 1 > sizeof(ctx.url)) {
        log_error("[OtaFwUpg] HTTP get: URL too long\r\n");
        return ERRCODE_FAIL;
    }
    strncpy(ctx.url, url, sizeof(ctx.url) - 1);
    ctx.url[sizeof(ctx.url) - 1] = '\0';

    mg_mgr_init(&mgr);

    c = mg_http_connect(&mgr, url, fota_http_ev_handler, &ctx);
    if (c == NULL) {
        log_error("[OtaFwUpg] HTTP get: connect failed\r\n");
        mg_mgr_free(&mgr);
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] HTTP get: waiting response...\r\n");
    uint64_t start = mg_millis();
    while (!ctx.complete && (mg_millis() - start < timeout_ms)) {
        mg_mgr_poll(&mgr, 10);
        osal_msleep(FOTA_POLL_INTERVAL_MS);
    }

    mg_mgr_free(&mgr);

    if (!ctx.success || ctx.data_len == 0) {
        log_error("[OtaFwUpg] HTTP get: failed (success=%d, len=%u)\r\n", ctx.success, ctx.data_len);
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] HTTP get: success, received %u bytes\r\n", ctx.data_len);
    return ERRCODE_SUCC;
}

typedef struct {
    int complete;
    int success;
    int connected;
    int headers_received;
    int chunked;
    int http_status;
    uint32_t write_offset;
    uint32_t expected_size;
    char url[256];
} fota_download_ctx_t;

static void fota_download_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    fota_download_ctx_t *ctx = (fota_download_ctx_t *)c->fn_data;

    if (ev == MG_EV_CONNECT) {
        log_info("[OtaFwUpg] Download: connected, sending request...\r\n");
        struct mg_str host = mg_url_host(ctx->url);
        mg_printf(c,
                  "GET %s HTTP/1.0\r\n"
                  "Host: %.*s\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  mg_url_uri(ctx->url),
                  (int)host.len, host.buf);
        ctx->connected = 1;
    } else if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        struct mg_str *cl, *te;

        ctx->http_status = mg_http_status(hm);
        log_info("[OtaFwUpg] Download: HTTP status %d\r\n", ctx->http_status);

        if (ctx->http_status != 200) {
            log_error("[OtaFwUpg] Download: unexpected HTTP status %d\r\n", ctx->http_status);
            ctx->success = 0;
            ctx->complete = 1;
            c->is_draining = 1;
            return;
        }

        cl = mg_http_get_header(hm, "Content-Length");
        if (cl != NULL) {
            bool mg_to_size_t(struct mg_str str, size_t *val);
            mg_to_size_t(*cl, (size_t *)&ctx->expected_size);
        }

        te = mg_http_get_header(hm, "Transfer-Encoding");
        ctx->chunked = (te != NULL && mg_strcasecmp(*te, mg_str("chunked")) == 0);

        if (ctx->chunked) {
            log_info("[OtaFwUpg] Download: Transfer-Encoding: chunked\r\n");
        } else {
            log_info("[OtaFwUpg] Download: Content-Length=%u\r\n", ctx->expected_size);
        }

        ctx->headers_received = 1;

        /* Write any body data that arrived with the headers.
         * NOTE: hm->body.len is the declared Content-Length (may be much larger
         * than what's actually buffered). Calculate the real available body bytes. */
        if (!ctx->chunked) {
            size_t body_off = (size_t)(hm->body.buf - (char *)c->recv.buf);
            size_t init_body_len = (body_off < c->recv.len) ? (c->recv.len - body_off) : 0;
            if (init_body_len > 0) {
                uint32_t remain = ctx->expected_size - ctx->write_offset;
                uint32_t write_len = init_body_len > remain ? remain : (uint32_t)init_body_len;
                log_info("[OtaFwUpg] Download: initial body %u bytes (clamped to %u)\r\n",
                       (uint32_t)init_body_len, write_len);
                fota_hexdump(hm->body.buf, write_len > 64 ? 64 : write_len);
                mg_sha256_update(&s_sha256_ctx, (unsigned char *)hm->body.buf, write_len);
#if FOTA_ENABLE_UPG
                errcode_t ret = uapi_upg_write_package_sync(ctx->write_offset,
                                                            (uint8_t *)hm->body.buf, (uint16_t)write_len);
                if (ret != ERRCODE_SUCC) {
                    log_error("[OtaFwUpg] Download: write failed at offset %u, ret=0x%x\r\n",
                           ctx->write_offset, ret);
                    ctx->success = 0;
                    ctx->complete = 1;
                    mg_close_conn(c);
                    return;
                }
#endif
                ctx->write_offset += write_len;
                s_fota_status.downloaded_size = ctx->write_offset;
            }
        }

        /*
         * Clear entire recv buffer (headers + init body we just processed).
         * This triggers http_cb's detach mechanism (mongoose.c:2519-2523):
         * c->pfn is set to NULL, so subsequent MG_EV_READ events bypass
         * the HTTP protocol layer entirely.
         * NOTE: must clear everything, not just headers, otherwise the body
         * data remains in recv.buf and will be processed again in MG_EV_READ.
         */
        c->recv.len = 0;

        if (!ctx->chunked && ctx->expected_size > 0 && ctx->write_offset >= ctx->expected_size) {
            log_info("[OtaFwUpg] Download: complete (all %u bytes received)\r\n", ctx->write_offset);
            ctx->success = 1;
            ctx->complete = 1;
            mg_close_conn(c);
        }
    } else if (ev == MG_EV_READ) {
        if (!ctx->headers_received || ctx->complete) {
            c->recv.len = 0;
            return;
        }

        uint8_t *buf = (uint8_t *)c->recv.buf;
        long len = (long)c->recv.len;

        if (ctx->chunked) {
            char *p = (char *)buf;
            char *end = p + len;
            while (p < end) {
                char *crlf = strstr(p, "\r\n");
                if (crlf == NULL) break;
                unsigned long chunk_size = strtoul(p, NULL, 16);
                if (chunk_size == 0) {
                    ctx->success = 1;
                    ctx->complete = 1;
                    c->recv.len = 0;
                    mg_close_conn(c);
                    return;
                }
                p = crlf + 2;
                if (p + chunk_size + 2 > end) break;

                if (chunk_size > 0) {
                    fota_hexdump(p, chunk_size > 64 ? 64 : chunk_size);
                    mg_sha256_update(&s_sha256_ctx, (unsigned char *)p, (size_t)chunk_size);
#if FOTA_ENABLE_UPG
                    errcode_t ret = uapi_upg_write_package_sync(ctx->write_offset,
                                                                (uint8_t *)p, (uint16_t)chunk_size);
                    if (ret != ERRCODE_SUCC) {
                        log_error("[OtaFwUpg] Download: write failed at offset %u, ret=0x%x\r\n",
                               ctx->write_offset, ret);
                        ctx->success = 0;
                        ctx->complete = 1;
                        mg_close_conn(c);
                        return;
                    }
#endif
                    ctx->write_offset += (uint32_t)chunk_size;
                    s_fota_status.downloaded_size = ctx->write_offset;
                }
                p += chunk_size + 2;
            }
            c->recv.len = 0;
        } else {
            if (len > 0) {
                uint32_t remain = ctx->expected_size - ctx->write_offset;
                uint32_t write_len = (uint32_t)len > remain ? remain : (uint32_t)len;
                log_info("[OtaFwUpg] Download: body data, len=%ld, clamped=%u\r\n", len, write_len);
                fota_hexdump(buf, write_len > 64 ? 64 : write_len);
                mg_sha256_update(&s_sha256_ctx, (unsigned char *)buf, write_len);
#if FOTA_ENABLE_UPG
                errcode_t ret = uapi_upg_write_package_sync(ctx->write_offset,
                                                            buf, (uint16_t)write_len);
                if (ret != ERRCODE_SUCC) {
                    log_error("[OtaFwUpg] Download: write failed at offset %u, ret=0x%x\r\n",
                           ctx->write_offset, ret);
                    ctx->success = 0;
                    ctx->complete = 1;
                    mg_close_conn(c);
                    return;
                }
#endif
                ctx->write_offset += write_len;
                s_fota_status.downloaded_size = ctx->write_offset;
                log_info("[OtaFwUpg] Download: wrote %u bytes, total=%u\r\n", write_len, ctx->write_offset);
            }
            c->recv.len = 0;

            if (ctx->expected_size > 0 && ctx->write_offset >= ctx->expected_size) {
                log_info("[OtaFwUpg] Download: complete (all %u bytes received)\r\n", ctx->write_offset);
                ctx->success = 1;
                ctx->complete = 1;
                mg_close_conn(c);
            }
        }
    } else if (ev == MG_EV_ERROR) {
        log_error("[OtaFwUpg] Download: MG_EV_ERROR: %s\r\n", (char *)ev_data);
        ctx->complete = 1;
        ctx->success = 0;
    } else if (ev == MG_EV_CLOSE) {
        log_info("[OtaFwUpg] Download: MG_EV_CLOSE, connected=%d, write_offset=%u\r\n",
               ctx->connected, ctx->write_offset);
        if (!ctx->complete) {
            if (ctx->expected_size > 0) {
                ctx->success = (ctx->write_offset >= ctx->expected_size);
            } else {
                ctx->success = (ctx->write_offset > 0);
            }
            ctx->complete = 1;
        }
    }
}

#if FOTA_ENABLE_UPG
static const char *fota_upg_err_str(errcode_t e)
{
    switch ((uint32_t)e) {
    case 0x80003040U:
        return "UPG_NOT_INIT";
    case 0x80003041U:
        return "UPG_ALREADY_INIT";
    case 0x80003042U:
        return "UPG_INVALID_PARAMETER";
    case 0x80003043U:
        return "UPG_WRONG_IMAGE_NUM";
    case 0x80003044U:
        return "UPG_INVALID_IMAGE_ID";
    case 0x80003045U:
        return "UPG_NULL_POINTER";
    case 0x80003046U:
        return "UPG_NOT_SUPPORTED";
    case 0x80003047U:
        return "UPG_NOT_NEED_TO_UPDATE";
    case 0x80003048U:
        return "UPG_FLASH_ERASE_ERROR";
    case 0x80003049U:
        return "UPG_FLASH_WRITE_ERROR";
    case 0x80003050U:
        return "UPG_FLAG_NOT_INITED";
    case 0x80003051U:
        return "UPG_NO_ENOUGH_SPACE";
    case 0x80003052U:
        return "UPG_PARTITION_ALIGN_ERROR";
    case 0x80003053U:
        return "UPG_INVALID_PREPARE_INFO";
    case 0x80003054U:
        return "UPG_NOT_PREPARED";
    case 0x80003055U:
        return "UPG_INVALID_BUFF_LEN";
    case 0x80003056U:
        return "UPG_INVALID_OFFSET";
    case 0xFFFFFFFFU:
        return "GENERIC_FAIL";
    default:
        return "UPG_UNKNOWN";
    }
}

/**
 * 从 UPG 暂存区读出包头并做整包校验（与 uapi_upg_request_upgrade 内部一致）。
 * 若 URL 上为裸 bin 或非本机型打包的升级包，会在此失败，避免先写 NV 版本再 reboot 失败。
 */
static errcode_t fota_upg_verify_stored_package(void)
{
    upg_package_header_t hdr;
    (void)memset(&hdr, 0, sizeof(hdr));
    errcode_t r = uapi_upg_read_package(0U, (uint8_t *)&hdr, (uint32_t)sizeof(hdr));
    if (r != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] UPG read_package(0, header) failed, ret=0x%x\r\n", (unsigned)r);
        return r;
    }
    r = uapi_upg_verify_file(&hdr);
    if (r != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] uapi_upg_verify_file failed, ret=0x%x (%s)\r\n",
                  (unsigned)r, fota_upg_err_str(r));
        return r;
    }
    log_info("[OtaFwUpg] UPG verify_file OK\r\n");
    return ERRCODE_SUCC;
}

/**
 * 先 uapi_upg_prepare 成功，再 write_package_sync。
 * SDK 中 `upg_prepare_info_t` 仅含 `package_len`（升级包字节数）；返回 INVALID_PARAMETER 时多为包长非法、未 init、或分区容量等。
 */
static errcode_t fota_upg_prepare_once(uint32_t package_len)
{
    if (package_len == 0U) {
        log_error("[OtaFwUpg] uapi_upg_prepare: package_len=0, skip (invalid)\r\n");
        return ERRCODE_FAIL;
    }

    uint32_t max_len = uapi_upg_get_storage_size();
    /* max_len==0：部分环境表示未实现或未知，此时不拦截，交给 uapi_upg_prepare */
    if (max_len > 0U && package_len > max_len) {
        log_error("[OtaFwUpg] package_len[%u] > max_len[%u] skip (no enough space)\r\n",
                  package_len, max_len);
        return ERRCODE_UPG_NO_ENOUGH_SPACE;
    }

    upg_prepare_info_t prepare_info;
    (void)memset(&prepare_info, 0, sizeof(prepare_info));
    prepare_info.package_len = package_len;

    errcode_t ret = uapi_upg_prepare(&prepare_info);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] uapi_upg_prepare(len=%u) ret=0x%x %s\r\n", package_len, (unsigned)ret, fota_upg_err_str(ret));
    }
    return ret;
}
#else
static errcode_t fota_upg_prepare_once(uint32_t package_len)
{
    (void)package_len;
    return ERRCODE_SUCC;
}
#endif

/** 分区已满、UPG 校验/参数类错误重试无效 */
static int fota_ota_should_skip_retry(errcode_t ret)
{
    if (ret == ERRCODE_UPG_NO_ENOUGH_SPACE) {
        return 1;
    }
#if FOTA_ENABLE_UPG
    {
        uint32_t u = (uint32_t)ret;
        if (u == 0xFFFFFFFFU) {
            return 1;
        }
        if (u >= 0x80003040U && u <= 0x80003060U) {
            return 1;
        }
    }
#endif
    return 0;
}

static errcode_t fota_download_and_write_init(const char *url, uint32_t size)
{
    if (url == NULL || url[0] == '\0' || size == 0) {
        log_error("[OtaFwUpg] Download init: invalid params\r\n");
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] Download init: url=%s, size=%u\r\n", url, size);

    mg_sha256_init(&s_sha256_ctx);
    memset(s_sha256_hash, 0, sizeof(s_sha256_hash));
    s_fota_status.downloaded_size = 0;
    s_fota_status.total_size = size;
    s_fota_status.download_progress = 0;

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] Flash: calling uapi_upg_prepare for %u bytes\r\n", size);
    errcode_t ret = fota_upg_prepare_once(size);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    log_info("[OtaFwUpg] Flash: prepare success, ready for download\r\n");
#endif

    return ERRCODE_SUCC;
}

static errcode_t fota_download_firmware(const char *url, uint32_t size)
{
    if (url == NULL || size == 0) {
        return ERRCODE_FAIL;
    }

    errcode_t ret = fota_download_and_write_init(url, size);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_DOWNLOADING, 0, s_user_data);
    }

    log_info("[OtaFwUpg] Download: url=%s, expected_size=%u\r\n", url, size);

    struct mg_mgr mgr;
    struct mg_connection *c;
    fota_download_ctx_t ctx = {0};

    if (strlen(url) + 1 > sizeof(ctx.url)) {
        log_error("[OtaFwUpg] Download: URL too long\r\n");
        return ERRCODE_FAIL;
    }
    strncpy(ctx.url, url, sizeof(ctx.url) - 1);
    ctx.url[sizeof(ctx.url) - 1] = '\0';
    ctx.write_offset = 0;

    mg_mgr_init(&mgr);

    log_info("[OtaFwUpg] Download: connecting...\r\n");
    c = mg_http_connect(&mgr, url, fota_download_ev_handler, &ctx);
    if (c == NULL) {
        log_error("[OtaFwUpg] Download: connect failed\r\n");
        mg_mgr_free(&mgr);
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] Download: waiting for response...\r\n");
    uint64_t start = mg_millis();
    while (!ctx.complete && (mg_millis() - start < 300000)) {
        mg_mgr_poll(&mgr, 10);
        osal_msleep(FOTA_POLL_INTERVAL_MS);

        if (s_fota_status.total_size > 0) {
            s_fota_status.download_progress = (s_fota_status.downloaded_size * 100) / s_fota_status.total_size;
        }
        if (s_progress_cb) {
            s_progress_cb(FOTA_STATE_DOWNLOADING, s_fota_status.download_progress, s_user_data);
        }
    }

    mg_mgr_free(&mgr);

    log_info("[OtaFwUpg] Download: complete, downloaded=%u, success=%d\r\n",
           s_fota_status.downloaded_size, ctx.success);

    if (!ctx.success) {
        log_error("[OtaFwUpg] Download: failed\r\n");
        return ERRCODE_FAIL;
    }

    if (s_fota_status.downloaded_size < size) {
        log_error("[OtaFwUpg] Download: size mismatch (expected=%u, got=%u)\r\n",
               size, s_fota_status.downloaded_size);
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] Download: firmware downloaded\r\n");

    return ERRCODE_SUCC;
}

typedef struct {
    int complete;
    int ok;
    uint32_t content_length;
    char url[256];
} fota_probe_ctx_t;

static void fota_probe_content_length_handler(struct mg_connection *c, int ev, void *ev_data)
{
    fota_probe_ctx_t *ctx = (fota_probe_ctx_t *)c->fn_data;

    if (ev == MG_EV_CONNECT) {
        struct mg_str host = mg_url_host(ctx->url);
        mg_printf(c,
                  "GET %s HTTP/1.0\r\n"
                  "Host: %.*s\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  mg_url_uri(ctx->url),
                  (int)host.len, host.buf);
    } else if (ev == MG_EV_HTTP_HDRS) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        int st = mg_http_status(hm);
        if (st != 200) {
            log_error("[OtaFwUpg] Probe: HTTP status %d\r\n", st);
            ctx->complete = 1;
            ctx->ok = 0;
            return;
        }
        struct mg_str *cl = mg_http_get_header(hm, "Content-Length");
        if (cl != NULL) {
            uint64_t v64 = 0U;
            if (mg_str_to_num(*cl, 10, &v64, sizeof(v64)) && v64 > 0ULL && v64 <= (uint64_t)UINT32_MAX) {
                ctx->content_length = (uint32_t)v64;
                ctx->ok = 1;
            }
        }
        ctx->complete = 1;
        c->is_draining = 1;
    } else if (ev == MG_EV_ERROR) {
        log_error("[OtaFwUpg] Probe: MG_EV_ERROR: %s\r\n", (char *)ev_data);
        ctx->complete = 1;
        ctx->ok = 0;
    } else if (ev == MG_EV_CLOSE) {
        if (!ctx->complete) {
            ctx->complete = 1;
        }
    }
}

static errcode_t fota_http_probe_content_length(const char *url, uint32_t *out_len)
{
    if (url == NULL || url[0] == '\0' || out_len == NULL) {
        return ERRCODE_FAIL;
    }
    *out_len = 0U;

    struct mg_mgr mgr;
    struct mg_connection *conn;
    fota_probe_ctx_t ctx = {0};

    if (strlen(url) + 1U > sizeof(ctx.url)) {
        log_error("[OtaFwUpg] Probe: URL too long\r\n");
        return ERRCODE_FAIL;
    }
    strncpy(ctx.url, url, sizeof(ctx.url) - 1U);
    ctx.url[sizeof(ctx.url) - 1U] = '\0';

    mg_mgr_init(&mgr);
    conn = mg_http_connect(&mgr, url, fota_probe_content_length_handler, &ctx);
    if (conn == NULL) {
        mg_mgr_free(&mgr);
        return ERRCODE_FAIL;
    }

    uint64_t start = mg_millis();
    while (!ctx.complete && (mg_millis() - start < (uint64_t)FOTA_DEFAULT_TIMEOUT)) {
        mg_mgr_poll(&mgr, 10);
        osal_msleep(FOTA_POLL_INTERVAL_MS);
    }
    mg_mgr_free(&mgr);

    if (ctx.ok == 0 || ctx.content_length == 0U) {
        log_error("[OtaFwUpg] Probe: failed to get Content-Length\r\n");
        return ERRCODE_FAIL;
    }
    *out_len = ctx.content_length;
    log_info("[OtaFwUpg] Probe: Content-Length=%u\r\n", *out_len);
    return ERRCODE_SUCC;
}

#if defined(SUPPORT_LFS)
#define FOTA_OTA_LFS_DIR       "/system"
#define FOTA_OTA_LFS_BASENAME  "firmware_ota.pkg"
#define FOTA_OTA_LFS_READ_CHUNK 4096U

static void fota_lfs_ota_progress(void *arg, size_t written, size_t total)
{
    (void)arg;
    s_fota_status.downloaded_size = (uint32_t)written;
    if (total > 0U) {
        s_fota_status.total_size = (uint32_t)total;
        s_fota_status.download_progress = (uint32_t)((written * 100U) / total);
    } else {
        s_fota_status.download_progress = 0U;
    }
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_DOWNLOADING, s_fota_status.download_progress, s_user_data);
    }

    g_ota_fw_update_progress = s_fota_status.download_progress;
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
    uint8_t msg_send = eDisp_Ota_FW_Update_Progress;
    (void)osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);
#endif
}

static void fota_ota_remove_lfs_artifacts(void)
{
    char path[112];
    (void)snprintf(path, sizeof(path), "%s/%s", FOTA_OTA_LFS_DIR, FOTA_OTA_LFS_BASENAME);
    (void)fs_adapt_delete(path);
    (void)snprintf(path, sizeof(path), "%s/%s.tmp", FOTA_OTA_LFS_DIR, FOTA_OTA_LFS_BASENAME);
    (void)fs_adapt_delete(path);
}

/**
 * 升级包已完整落在 LittleFS：分块读入内存 → uapi_upg_write_package_sync（prepare 须由调用方在下载前完成）。
 * @param skip_prepare 非 0：本函数内不再调用 uapi_upg_prepare（已与落盘文件长度一致）。
 */
static errcode_t fota_flash_package_from_lfs_file(const char *path, uint32_t file_size,
                                                  const char *sha256_hex_opt, uint8_t skip_prepare)
{
    if (path == NULL || path[0] == '\0' || file_size == 0U) {
        return ERRCODE_FAIL;
    }

    s_fota_status.downloaded_size = 0U;
    s_fota_status.total_size = file_size;
    s_fota_status.download_progress = 0U;

    const int do_sha = (sha256_hex_opt != NULL && sha256_hex_opt[0] != '\0');
    if (do_sha) {
        mg_sha256_init(&s_sha256_ctx);
        memset(s_sha256_hash, 0, sizeof(s_sha256_hash));
    }

    errcode_t ret = ERRCODE_SUCC;
    if (skip_prepare == 0U) {
        log_info("[OtaFwUpg] LFS→Flash: prepare %u bytes\r\n", file_size);
        ret = fota_upg_prepare_once(file_size);
        if (ret != ERRCODE_SUCC) {
            return ret;
        }
    } else {
        log_info("[OtaFwUpg] LFS→Flash: skip prepare (already done before download)\r\n");
    }

    int fd = fs_adapt_open(path, O_RDONLY);
    if (fd < 0) {
        log_error("[OtaFwUpg] LFS→Flash: open %s failed\r\n", path);
        return ERRCODE_FAIL;
    }

    uint8_t buf[FOTA_OTA_LFS_READ_CHUNK];
    uint32_t off = 0U;
    while (off < file_size) {
        uint32_t chunk = file_size - off;
        if (chunk > FOTA_OTA_LFS_READ_CHUNK) {
            chunk = FOTA_OTA_LFS_READ_CHUNK;
        }
        int rd = fs_adapt_read(fd, (char *)buf, chunk);
        if (rd <= 0) {
            log_error("[OtaFwUpg] LFS→Flash: read failed off=%u rd=%d\r\n", off, rd);
            (void)fs_adapt_close(fd);
            return ERRCODE_FAIL;
        }
        if (do_sha) {
            mg_sha256_update(&s_sha256_ctx, buf, (size_t)rd);
        }
#if FOTA_ENABLE_UPG
        ret = uapi_upg_write_package_sync(off, buf, (uint16_t)rd);
        if (ret != ERRCODE_SUCC) {
            log_error("[OtaFwUpg] LFS→Flash: write_package_sync off=%u ret=0x%x\r\n", off, ret);
            (void)fs_adapt_close(fd);
            return ret;
        }
#endif
        off += (uint32_t)rd;
        s_fota_status.downloaded_size = off;
        if (file_size > 0U) {
            s_fota_status.download_progress = (off * 100U) / file_size;
        }
//      if (s_progress_cb) {
//          s_progress_cb(FOTA_STATE_DOWNLOADING, s_fota_status.download_progress, s_user_data);
//      }
    }
    (void)fs_adapt_close(fd);

    if (off != file_size) {
        log_error("[OtaFwUpg] LFS→Flash: size mismatch %u != %u\r\n", off, file_size);
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] LFS→Flash: all bytes written to upgrade area\r\n");
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
    uint8_t msg_send = eDisp_Ota_FW_Update_Complete;
    (void)osMessageQueuePut(g_disp_event_qid, (const void *)&msg_send, 0, 0);
#endif
    sleep(3);  // delay 3s for display update info then reboot

    s_fota_status.state = FOTA_STATE_VERIFYING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_VERIFYING, 0, s_user_data);
    }
    if (do_sha) {
        ret = fota_verify_final_sha256(sha256_hex_opt);
        if (ret != ERRCODE_SUCC) {
            s_fota_status.state = FOTA_STATE_FAILED;
            s_fota_status.last_error = -4;
            return ret;
        }
    } else {
        log_info("[OtaFwUpg] LFS→Flash: skip SHA256 (server did not provide sha256)\r\n");
    }

#if FOTA_ENABLE_UPG
    ret = fota_upg_verify_stored_package();
    if (ret != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#endif

    return ERRCODE_SUCC;
}

static errcode_t fota_direct_package_via_lfs(const char *fw_url, const char *target_version,
                                             const char *sha256_hex_opt)
{
    if (fw_url == NULL || fw_url[0] == '\0') {
        return ERRCODE_FAIL;
    }
    if (strncmp(fw_url, "http://", 7) != 0) {
        log_error("[OtaFwUpg] LFS path: only http:// supported\r\n");
        return ERRCODE_FAIL;
    }

    s_fota_status.state = FOTA_STATE_CHECKING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_CHECKING, 0, s_user_data);
    }

    /* 与 SDK 文档一致：在收包前先 prepare(包长)。长度来自 OTA JSON 的 size 或 HTTP Content-Length 探测。 */
    uint32_t planned_len = get_ota_firmware_size_pending();
    if (planned_len == 0U) {
        if (fota_http_probe_content_length(fw_url, &planned_len) != ERRCODE_SUCC) {
            log_error("[OtaFwUpg] LFS path: no firmware size in JSON and Content-Length probe failed\r\n");
            s_fota_status.state = FOTA_STATE_FAILED;
            s_fota_status.last_error = -7;
            return ERRCODE_FAIL;
        }
    }

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] uapi_upg_prepare before download, len=%u\r\n", planned_len);
    errcode_t pr = fota_upg_prepare_once(planned_len);
    if (pr != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -7;
        return pr;
    }
#endif

    fota_ota_remove_lfs_artifacts();

    http_lfs_download_param_t param = {0};
    param.url = fw_url;
    param.base_dir = FOTA_OTA_LFS_DIR;
    param.basename = FOTA_OTA_LFS_BASENAME;
    param.part_suffix = ".tmp";
    param.store_mode = HTTP_LFS_STORE_ATOMIC;
    param.progress_fn = fota_lfs_ota_progress;
    param.progress_arg = NULL;
    param.progress_every_bytes = (32U * 1024U);

    s_fota_status.state = FOTA_STATE_DOWNLOADING;
    s_fota_status.downloaded_size = 0U;
    s_fota_status.total_size = 0U;
    s_fota_status.download_progress = 0U;

    log_info("[OtaFwUpg] Download to LFS: %s/%s\r\n", FOTA_OTA_LFS_DIR, FOTA_OTA_LFS_BASENAME);
#if 0 // 调用http客户端下载OTA升级包
    if (http_lfs_download_blocking(&param) != 0) {
        log_error("[OtaFwUpg] http_lfs_download_blocking failed\r\n");
        fota_ota_remove_lfs_artifacts();
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -3;
        return ERRCODE_FAIL;
    }
#else
return ERRCODE_FAIL;
#endif

    char path_final[112];
    (void)snprintf(path_final, sizeof(path_final), "%s/%s", FOTA_OTA_LFS_DIR, FOTA_OTA_LFS_BASENAME);
    unsigned int fsz = 0U;
    if (fs_adapt_stat(path_final, &fsz) != 0 || fsz == 0U) {
        log_error("[OtaFwUpg] stat %s failed or empty\r\n", path_final);
        fota_ota_remove_lfs_artifacts();
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -7;
        return ERRCODE_FAIL;
    }
    uint32_t pkg_size = fsz;
    if ((unsigned int)pkg_size != fsz) {
        log_error("[OtaFwUpg] package too large for uint32\r\n");
        fota_ota_remove_lfs_artifacts();
        return ERRCODE_FAIL;
    }

    if (pkg_size != planned_len) {
        log_error("[OtaFwUpg] downloaded size %u != prepared length %u, abort\r\n", pkg_size, planned_len);
        fota_ota_remove_lfs_artifacts();
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -7;
        return ERRCODE_FAIL;
    }

    errcode_t ret = fota_flash_package_from_lfs_file(path_final, pkg_size, sha256_hex_opt, 1U);
    (void)fs_adapt_delete(path_final);

    if (ret != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        if (s_fota_status.last_error == 0) {
            s_fota_status.last_error = -3;
        }
        return ret;
    }

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] Direct: request upgrade (reboot)\r\n");
    s_fota_status.state = FOTA_STATE_REBOOTING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_REBOOTING, 100, s_user_data);
    }
    ret = uapi_upg_request_upgrade(true);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Direct: uapi_upg_request_upgrade failed, ret=0x%x (%s)\r\n", (unsigned)ret,
                  fota_upg_err_str(ret));
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#else
    (void)ver_save;
    s_fota_status.state = FOTA_STATE_VERIFYING;
#endif
    return ERRCODE_SUCC;
}
#endif /* SUPPORT_LFS */

static errcode_t fota_direct_package_from_url(const char *fw_url, const char *target_version,
                                              uint32_t size_hint, const char *sha256_hex_opt)
{
    if (fw_url == NULL || fw_url[0] == '\0') {
        return ERRCODE_FAIL;
    }

#if defined(SUPPORT_LFS)
    (void)size_hint;
    return fota_direct_package_via_lfs(fw_url, target_version, sha256_hex_opt);
#else
    s_fota_status.state = FOTA_STATE_CHECKING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_CHECKING, 0, s_user_data);
    }

    uint32_t pkg_size = size_hint;
    if (pkg_size == 0U) {
        if (fota_http_probe_content_length(fw_url, &pkg_size) != ERRCODE_SUCC) {
            log_error("[OtaFwUpg] Direct: need firmware size (JSON `size` or HTTP Content-Length)\r\n");
            s_fota_status.state = FOTA_STATE_FAILED;
            s_fota_status.last_error = -7;
            return ERRCODE_FAIL;
        }
    }

    s_fota_status.state = FOTA_STATE_DOWNLOADING;
    errcode_t ret = fota_download_firmware(fw_url, pkg_size);
    if (ret != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -3;
        return ret;
    }

    s_fota_status.state = FOTA_STATE_VERIFYING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_VERIFYING, 0, s_user_data);
    }

    if (sha256_hex_opt != NULL && sha256_hex_opt[0] != '\0') {
        ret = fota_verify_final_sha256(sha256_hex_opt);
        if (ret != ERRCODE_SUCC) {
            s_fota_status.state = FOTA_STATE_FAILED;
            s_fota_status.last_error = -4;
            return ret;
        }
    } else {
        log_info("[OtaFwUpg] Direct: skip SHA256 (server did not provide sha256)\r\n");
    }

#if FOTA_ENABLE_UPG
    ret = fota_upg_verify_stored_package();
    if (ret != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#endif

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] Direct: uapi_upg_request_upgrade (reboot)\r\n");
    s_fota_status.state = FOTA_STATE_REBOOTING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_REBOOTING, 100, s_user_data);
    }
    ret = uapi_upg_request_upgrade(true);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Direct: uapi_upg_request_upgrade failed, ret=0x%x (%s)\r\n", (unsigned)ret,
                  fota_upg_err_str(ret));
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#else
    (void)ver_save;
    s_fota_status.state = FOTA_STATE_VERIFYING;
#endif
    return ERRCODE_SUCC;
#endif /* SUPPORT_LFS */
}

static bool fota_is_newer_version(const char *current_version, const char *new_version)
{
    if (current_version == NULL || new_version == NULL) {
        log_error("[OtaFwUpg] Version compare: NULL pointer\r\n");
        return false;
    }
    int c_major = 0, c_minor = 0, c_patch = 0;
    int n_major = 0, n_minor = 0, n_patch = 0;
    sscanf(current_version, "%d.%d.%d", &c_major, &c_minor, &c_patch);
    sscanf(new_version, "%d.%d.%d", &n_major, &n_minor, &n_patch);

    log_info("[OtaFwUpg] Version compare: current=%s(%d.%d.%d), new=%s(%d.%d.%d)\r\n",
           current_version, c_major, c_minor, c_patch,
           new_version, n_major, n_minor, n_patch);

    if (n_major > c_major) {
        log_info("[OtaFwUpg] Version compare: new is newer (major)\r\n");
        return true;
    }
    if (n_major == c_major && n_minor > c_minor) {
        log_info("[OtaFwUpg] Version compare: new is newer (minor)\r\n");
        return true;
    }
    if (n_major == c_major && n_minor == c_minor && n_patch > c_patch) {
        log_info("[OtaFwUpg] Version compare: new is newer (patch)\r\n");
        return true;
    }
    log_info("[OtaFwUpg] Version compare: no update needed\r\n");
    return false;
}

static errcode_t fota_read_version_from_nv(char *version, uint32_t len)
{
    if (version == NULL || len == 0) {
        return ERRCODE_FAIL;
    }
    uint16_t read_len = 0;
    errcode_t ret = uapi_nv_read(FOTA_NV_KEY_VERSION, len, &read_len, (uint8_t *)version);
    if (ret == ERRCODE_SUCC) {
        if (read_len < len) {
            version[read_len] = '\0';
        }
    }
    return ret;
}

static errcode_t fota_save_version_to_nv(const char *version)
{
    if (version == NULL) {
        return ERRCODE_FAIL;
    }
    size_t ver_len = strlen(version);
    if (ver_len == 0 || ver_len >= FOTA_VERSION_MAX) {
        return ERRCODE_FAIL;
    }
    return uapi_nv_write(FOTA_NV_KEY_VERSION, (const uint8_t *)version, (uint16_t)ver_len);
}

static errcode_t fota_verify_final_sha256(const char *expected_sha256)
{
    if (expected_sha256 == NULL) {
        log_error("[OtaFwUpg] SHA256 verify: NULL expected hash\r\n");
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] SHA256 verify: computing...\r\n");
    mg_sha256_final(s_sha256_hash, &s_sha256_ctx);

    char hash_str[sizeof(s_sha256_hash) * 2 + 1] = {0};
    for (size_t i = 0; i < sizeof(s_sha256_hash); i++) {
        snprintf(hash_str + i * 2, 3, "%02x", s_sha256_hash[i]);
    }

    log_info("[OtaFwUpg] SHA256 verify: expected=%s\r\n", expected_sha256);
    log_info("[OtaFwUpg] SHA256 verify: computed=%s\r\n", hash_str);

    size_t elen = strlen(expected_sha256);
    if (elen != sizeof(s_sha256_hash) * 2U) {
        log_error("[OtaFwUpg] SHA256 verify: expected length invalid (%u)\r\n", (unsigned)elen);
        return ERRCODE_FAIL;
    }

    int sha_cmp_ok = 1;
    for (size_t i = 0; i < sizeof(s_sha256_hash) * 2U; i++) {
        char a = hash_str[i];
        char b = expected_sha256[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + 32);
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + 32);
        }
        if (a != b) {
            sha_cmp_ok = 0;
            break;
        }
    }
    if (sha_cmp_ok == 0) {
        log_error("[OtaFwUpg] SHA256 verify: FAILED\r\n");
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] SHA256 verify: SUCCESS\r\n");
    return ERRCODE_SUCC;
}

errcode_t fota_init(void)
{
    s_fota_status.state = FOTA_STATE_IDLE;
    s_fota_status.download_progress = 0;
    s_fota_status.total_size = 0;
    s_fota_status.downloaded_size = 0;
    s_fota_status.last_error = 0;

    //log_info("[OtaFwUpg] Init: initializing NV module...\r\n");
    //uapi_nv_init();
    //log_info("[OtaFwUpg] Init: NV module initialized\r\n");

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] Init: initializing UPG module...\r\n");
    errcode_t ret = uapi_upg_init(&s_upg_funcs);
    if (ret != ERRCODE_SUCC && ret != ERRCODE_UPG_ALREADY_INIT) {
        log_error("[OtaFwUpg] Init: uapi_upg_init failed, ret=0x%x\r\n", ret);
        return ret;
    }

    log_info("[OtaFwUpg] Init: resetting upgrade flag...\r\n");
    ret = uapi_upg_reset_upgrade_flag();
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Init: uapi_upg_reset_upgrade_flag failed, ret=0x%x\r\n", ret);
    }
#endif

    log_info("[OtaFwUpg] Init: done\r\n");
    return ERRCODE_SUCC;
}

#if 0
errcode_t fota_check_and_upgrade(const char *fota_info_url, fota_progress_cb cb, void *user_data)
{
    if (fota_info_url == NULL) {
        log_error("[OtaFwUpg] Check: invalid params\r\n");
        return ERRCODE_FAIL;
    }

    log_info("[OtaFwUpg] ========================================\r\n");
    log_info("[OtaFwUpg] Check: fota_info_url=%s\r\n", fota_info_url);
    log_info("[OtaFwUpg] ========================================\r\n");

    s_progress_cb = cb;
    s_user_data = user_data;
    s_fota_status.state = FOTA_STATE_CHECKING;

    log_info("[OtaFwUpg] Check: fetching fota_info.json...\r\n");
    char resp_buf[FOTA_HTTP_RESP_MAX];
    errcode_t ret = fota_http_get(fota_info_url, resp_buf, sizeof(resp_buf), FOTA_DEFAULT_TIMEOUT);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: failed to fetch fota_info.json\r\n");
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -1;
        return ret;
    }

    log_info("[OtaFwUpg] Check: parsing fota_info.json...\r\n");
    fota_firmware_info_t fw_info;
    ret = fota_parse_info_json(resp_buf, strlen(resp_buf), &fw_info);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: failed to parse fota_info.json\r\n");
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -2;
        return ret;
    }

    if (fw_info.size == 0) {
        log_error("[OtaFwUpg] Check: firmware size is 0\r\n");
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -6;
        return ERRCODE_FAIL;
    }

    char current_version[FOTA_VERSION_MAX] = {0};
    if (fota_read_version_from_nv(current_version, sizeof(current_version)) != ERRCODE_SUCC) {
        log_info("[OtaFwUpg] Check: no version in NV, default to 0.0.0\r\n");
        memcpy(current_version, "0.0.0", 6);
    }

    if (!fota_is_newer_version(current_version, fw_info.version)) {
        log_info("[OtaFwUpg] Check: no update needed, current=%s >= new=%s\r\n", current_version, fw_info.version);
        s_fota_status.state = FOTA_STATE_IDLE;
        return ERRCODE_SUCC;
    }

    log_info("[OtaFwUpg] ========================================\r\n");
    log_info("[OtaFwUpg] Check: STARTING UPGRADE\r\n");
    log_info("[OtaFwUpg]   version: %s -> %s\r\n", current_version, fw_info.version);
    log_info("[OtaFwUpg]   size: %u bytes\r\n", fw_info.size);
    log_info("[OtaFwUpg]   url: %s\r\n", fw_info.url);
    log_info("[OtaFwUpg] ========================================\r\n");

    s_fota_status.state = FOTA_STATE_DOWNLOADING;
    log_info("[OtaFwUpg] Check: downloading firmware...\r\n");
    ret = fota_download_firmware(fw_info.url, fw_info.size);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: firmware download failed\r\n");
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -3;
        return ret;
    }

    s_fota_status.state = FOTA_STATE_VERIFYING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_VERIFYING, 0, s_user_data);
    }

    log_info("[OtaFwUpg] Check: verifying firmware SHA256...\r\n");
    ret = fota_verify_final_sha256(fw_info.sha256);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: firmware SHA256 verification failed\r\n");
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -4;
        return ret;
    }

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] Check: verifying upgrade package (UPG)...\r\n");
    ret = fota_upg_verify_stored_package();
    if (ret != ERRCODE_SUCC) {
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#endif

    log_info("[OtaFwUpg] Check: saving new version to NV...\r\n");
    ret = fota_save_version_to_nv(fw_info.version);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: failed to save version, ret=0x%x\r\n", ret);
    }

#if FOTA_ENABLE_UPG
    log_info("[OtaFwUpg] ========================================\r\n");
    log_info("[OtaFwUpg] Check: STARTING UPGRADE (will reboot)...\r\n");
    log_info("[OtaFwUpg] ========================================\r\n");

    s_fota_status.state = FOTA_STATE_REBOOTING;
    if (s_progress_cb) {
        s_progress_cb(FOTA_STATE_REBOOTING, 100, s_user_data);
    }

    ret = uapi_upg_request_upgrade(true);
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] Check: uapi_upg_request_upgrade failed, ret=0x%x (%s)\r\n", (unsigned)ret,
                  fota_upg_err_str(ret));
        s_fota_status.state = FOTA_STATE_FAILED;
        s_fota_status.last_error = -5;
        return ret;
    }
#else
    log_info("[OtaFwUpg] ========================================\r\n");
    log_info("[OtaFwUpg] Check: UPG disabled, SHA256 verify done\r\n");
    log_info("[OtaFwUpg] ========================================\r\n");
    s_fota_status.state = FOTA_STATE_VERIFYING;
#endif

    return ERRCODE_SUCC;
}

errcode_t fota_get_version(char *version, uint32_t len)
{
    if (version == NULL || len == 0) {
        return ERRCODE_FAIL;
    }
    return fota_read_version_from_nv(version, len);
}

errcode_t fota_set_version(const char *version)
{
    if (version == NULL) {
        return ERRCODE_FAIL;
    }
    return fota_save_version_to_nv(version);
}

errcode_t fota_get_status(fota_status_t *status)
{
    if (status == NULL) {
        return ERRCODE_FAIL;
    }
    memcpy(status, &s_fota_status, sizeof(fota_status_t));
    return ERRCODE_SUCC;
}
#endif

static volatile uint8_t g_ota_fw_worker_running = 0;

static void ota_fw_notify_ui(uint8_t ev)
{
#if defined(SUPPORT_DISP) && (SUPPORT_DISP)
    if (g_disp_event_qid == NULL) {
        return;
    }
    (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, 50U);
#endif
}

static void ota_fw_progress_callback(fota_state_t state, uint32_t progress, void *user_data)
{
    (void)user_data;
    log_debug("[OtaFwUpg] OTA progress: state=%s, progress=%u%%\r\n", FOTA_STATE_TO_STR(state), progress);
}

static errcode_t ota_fw_try_upgrade_with_candidates(const char *ota_url)
{
    s_progress_cb = ota_fw_progress_callback;
    s_user_data = NULL;

    /* 云端仅在 OTA 检查 JSON 中返回 firmware.url 直链，不提供独立 fota_info.json，不做 manifest 探测 */
    log_info("[OtaFwUpg] OTA direct firmware URL (skip JSON manifest):\r\n\t%s\r\n", ota_url);

    char ver_pending[64];
    char sha_pending[FOTA_SHA256_MAX + 1];
    (void)memset(ver_pending, 0, sizeof(ver_pending));
    (void)memset(sha_pending, 0, sizeof(sha_pending));
    (void)get_ota_firmware_version_pending(ver_pending, sizeof(ver_pending));
    (void)get_ota_firmware_sha256_pending(sha_pending, sizeof(sha_pending));
    uint32_t sz_hint = get_ota_firmware_size_pending();

    return fota_direct_package_from_url(ota_url, ver_pending, sz_hint,
                                        (sha_pending[0] != '\0') ? sha_pending : NULL);
}

static void ota_fw_update_task(void *arg)
{
    (void)arg;

    char ota_url[OTA_ADDR_LEN_MAX] = {0};
    errcode_t ret = get_ota_firmware_address(ota_url, sizeof(ota_url));
    if ((ret != ERRCODE_SUCC) || (ota_url[0] == '\0')) {
        log_error("[OtaFwUpg] OTA update skipped: invalid firmware url, ret=%d\r\n", ret);
        clear_ota_firmware_update_pending();
        g_ota_fw_worker_running = 0;
        osThreadExit();
        return;
    }

    log_info("[OtaFwUpg] OTA update start, firmware url=%s\r\n", ota_url);
    ret = fota_init();
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] OTA fota_init failed, ret=0x%x\r\n", ret);
//      ota_fw_notify_ui((uint8_t)eDisp_Ota_Upgrade_Failed);
        clear_ota_firmware_update_pending();
        g_ota_fw_worker_running = 0;
        osThreadExit();
        return;
    }

    const int max_rounds = 3;
    for (int round = 1; round <= max_rounds; round++) {
        if (round > 1) {
            (void)fota_init();
        }
        ret = ota_fw_try_upgrade_with_candidates(ota_url);
        if (ret == ERRCODE_SUCC) {
            break;
        }
        if (fota_ota_should_skip_retry(ret)) {
            log_error("[OtaFwUpg] fota_ota_should_skip_retry, ret=0x%x\r\n", (unsigned)ret);
            break;
        }
        if (round < max_rounds) {
            uint32_t backoff_ms = (uint32_t)round * 2000U;
            log_warn("[OtaFwUpg] OTA round %d/%d failed, retry after %ums\r\n", round, max_rounds, backoff_ms);
            osal_msleep(backoff_ms);
        }
    }
    if (ret != ERRCODE_SUCC) {
        log_error("[OtaFwUpg] OTA upgrade failed after retries, ret=0x%x\r\n", ret);
//      ota_fw_notify_ui((uint8_t)eDisp_Ota_Upgrade_Failed);
        clear_ota_firmware_update_pending();
        g_ota_fw_worker_running = 0;
        osThreadExit();
        return;
    }

    log_info("[OtaFwUpg] OTA upgrade requested, waiting reboot\r\n");
    clear_ota_firmware_update_pending();
    g_ota_fw_worker_running = 0;
    osThreadExit();
}

void ota_fw_update_start(void)
{
    if (g_ota_fw_worker_running != 0U) {
        log_warn("[OtaFwUpg] OTA worker already running\r\n");
        return;
    }

    g_ota_fw_worker_running = 1U;

    task_config_t task_conf = {
        .name = "OtaFwUpdate",
        .stack_size = 1024 * 24,
        .priority = 23,
        .func = (void *)ota_fw_update_task,
    };
    if (create_task(&task_conf, NULL) == 0U) {
        g_ota_fw_worker_running = 0U;
        log_error("[OtaFwUpg] create OtaFwUpdate task failed\r\n");
    }
}

