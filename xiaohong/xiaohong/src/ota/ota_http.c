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
 * @file ota_http.c
 * @brief OTA 版本检查 / 激活：统一走 mongoose（mg_http_connect），支持 http 与 https。
 *
 * 历史实现用裸 lwIP netconn，并**显式拒收 https**（打印 "[OTA-HTTP] url is https"），
 * 导致 OTA 地址一旦是 https 就必然失败。现改为与字库下载 / 固件下载 / WebSocket
 * 同一套 mongoose 客户端；TLS 由 xh_tls_apply() 按 URL 挂载（工程以
 * MG_TLS=MG_TLS_BUILTIN 编译），并内嵌 CA 做严格校验。
 */

#include "ota_http.h"
#include "log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmsis_os2.h"
#include "mongoose.h"
#include "soc_osal.h"
#include "xh_tls.h"

/* 与 http_lfs_download.c 一致：mongoose 事件循环非线程安全，poll/connect 需串行化 */
#if defined(__has_include)
#if __has_include("mongoose_protocol.h")
#include "mongoose_protocol.h"
#define OTA_MG_LOCK()   mongoose_api_lock()
#define OTA_MG_UNLOCK() mongoose_api_unlock()
#endif
#endif
#ifndef OTA_MG_LOCK
#define OTA_MG_LOCK()   ((void)0)
#define OTA_MG_UNLOCK() ((void)0)
#endif

/*
 * OTA HTTP 会话串行锁（与上面的 OTA_MG_LOCK 用途不同，勿混用）。
 *
 * OTA_MG_LOCK 只保护单次 mg_* 调用（很短）；本锁保护「整个会话」——
 * 从 mg_mgr_init 到 mg_mgr_free。原因：TLS 下每个会话要从 libc 堆申请约 15~17KB
 * （每条连接 recv 4K + send 4K + rtls 8K，另加 tls_data ≈1.4K，见 mg_alloc_conn/ioalloc），
 * 而本工程 LiteOS 堆很紧。仅改调用顺序挡不住并发：ota_startup_must_fetch_font_first()
 * 在 ota_apply_parsed_result_notify_main() 里就已投递 eSettings_Start_Font_If_Needed，
 * 字库任务随即发起自己的 OTA 查询，与 OtaCheckTask 的收尾上报重叠 → 实测 OOM：
 *   mg_iobuf_resize: 4096->8192 / mg_error: 1 256 OOM
 * 故此处让所有 OTA HTTP 会话互斥，峰值始终只有一个会话。
 *
 * 注意：本锁必须只在「会话级」持有，绝不可跨 osal_msleep 持有 mongoose_api_lock，
 * 否则会阻塞 Agent 的 WS 轮询、破坏语音链路。
 */
static osMutexId_t s_ota_http_sess_mutex = NULL;
/* 静态控制块：锁自身不在堆上分配（此刻堆已紧张） */
static uint64_t s_ota_http_sess_mu_cb[20];
static const osMutexAttr_t s_ota_http_sess_mu_attr = {
    .name = "ota_http_sess_mu",
    .attr_bits = 0U,
    .cb_mem = s_ota_http_sess_mu_cb,
    .cb_size = sizeof(s_ota_http_sess_mu_cb),
};

static void ota_http_sess_lock(void)
{
    if (s_ota_http_sess_mutex == NULL) {
        s_ota_http_sess_mutex = osMutexNew(&s_ota_http_sess_mu_attr);
        if (s_ota_http_sess_mutex == NULL) {
            log_error("[OTA-HTTP] session mutex create failed\r\n");
        }
    }
    if (s_ota_http_sess_mutex != NULL) {
        (void)osMutexAcquire(s_ota_http_sess_mutex, osWaitForever);
    }
}

static void ota_http_sess_unlock(void)
{
    if (s_ota_http_sess_mutex != NULL) {
        (void)osMutexRelease(s_ota_http_sess_mutex);
    }
}

/** 请求行+头+小 JSON POST，4KB 足够。 */
#define OTA_HTTP_REQ_BUF_MAX 4096U
/** 服务端 JSON 响应（版本检查） */
#define OTA_HTTP_RESP_BUF_MAX 8192U
#define OTA_HTTP_DEFAULT_TIMEOUT_MS 30000U

/* mongoose 事件循环轮询粒度：与 http_lfs_download.c 一致 */
#ifndef OTA_HTTP_MGR_POLL_MS
#define OTA_HTTP_MGR_POLL_MS 10
#endif
#ifndef OTA_HTTP_MGR_POLL_SLEEP_MS
#define OTA_HTTP_MGR_POLL_SLEEP_MS 1U
#endif

static void ota_copy_field(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0U) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0U;
    while (i + 1U < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/** 无 libc 格式化：无符号转十进制写入 dst，返回写入字符数（不含 \\0） */
static size_t ota_ultoa(char *dst, size_t cap, unsigned long v)
{
    if (cap == 0U) {
        return 0U;
    }
    char tmp[24];
    int n = 0;
    if (v == 0UL) {
        tmp[n++] = '0';
    } else {
        while (v > 0UL && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (int)(v % 10UL));
            v /= 10UL;
        }
    }
    size_t w = 0U;
    for (int i = n - 1; i >= 0 && w + 1U < cap; i--) {
        dst[w++] = tmp[i];
    }
    dst[w] = '\0';
    return w;
}

static int ota_append(char *buf, size_t cap, size_t *off, const char *s)
{
    if (s == NULL) {
        return 0;
    }
    size_t l = strlen(s);
    if (*off + l >= cap) {
        return -1;
    }
    memcpy(buf + *off, s, l);
    *off += l;
    return 0;
}

static int ota_append_buf(char *buf, size_t cap, size_t *off, const void *p, size_t n)
{
    if (p == NULL || n == 0U) {
        return 0;
    }
    if (*off + n >= cap) {
        return -1;
    }
    memcpy(buf + *off, p, n);
    *off += n;
    return 0;
}

/** 解析 OTA 业务 JSON 的顶层 code；无该字段返回 1（不视为错误）。 */
static int ota_http_try_get_business_code(const char *body, int *out_code)
{
    if (body == NULL || out_code == NULL) {
        return -1;
    }

    const char *p = strstr(body, "\"code\"");
    if (p == NULL) {
        return 1;
    }
    p += strlen("\"code\"");

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != ':') {
        return -1;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }

    int sign = 1;
    if (*p == '-') {
        sign = -1;
        p++;
    }
    if (*p < '0' || *p > '9') {
        return -1;
    }

    int code = 0;
    while (*p >= '0' && *p <= '9') {
        code = code * 10 + (int)(*p - '0');
        p++;
    }
    *out_code = code * sign;
    return 0;
}

typedef struct {
    ota_http_request_t req;
    char s_method[12];
    char s_av[8];
    char s_dev[96];
    char s_cli[96];
    char s_ua[96];
    char s_al[24];
    char s_sn[96];
    char s_post[2048];
    uint32_t timeout_ms;
} ota_req_embed_t;

/** 请求/响应缓冲在静态区，避免 OTA 路径再向 libc heap 申请大块。 */
static char s_ota_req_buf[OTA_HTTP_REQ_BUF_MAX];    // 4096
static char s_ota_raw_buf[OTA_HTTP_RESP_BUF_MAX];   // 8192
static ota_req_embed_t s_ota_embed;

static void ota_embed_http_req(ota_req_embed_t *c, const ota_http_request_t *hr)
{
    memset(&c->req, 0, sizeof(c->req));
    c->req.timeout_ms = (hr != NULL && hr->timeout_ms > 0U) ? hr->timeout_ms : 0U;

    if (hr == NULL) {
        return;
    }
    if (hr->method != NULL && hr->method[0] != '\0') {
        ota_copy_field(c->s_method, sizeof(c->s_method), hr->method);
        c->req.method = c->s_method;
    }
    if (hr->activation_version != NULL && hr->activation_version[0] != '\0') {
        ota_copy_field(c->s_av, sizeof(c->s_av), hr->activation_version);
        c->req.activation_version = c->s_av;
    }
    if (hr->device_id != NULL) {
        ota_copy_field(c->s_dev, sizeof(c->s_dev), hr->device_id);
        c->req.device_id = c->s_dev;
    }
    if (hr->client_id != NULL) {
        ota_copy_field(c->s_cli, sizeof(c->s_cli), hr->client_id);
        c->req.client_id = c->s_cli;
    }
    if (hr->user_agent != NULL && hr->user_agent[0] != '\0') {
        ota_copy_field(c->s_ua, sizeof(c->s_ua), hr->user_agent);
        c->req.user_agent = c->s_ua;
    }
    if (hr->accept_language != NULL && hr->accept_language[0] != '\0') {
        ota_copy_field(c->s_al, sizeof(c->s_al), hr->accept_language);
        c->req.accept_language = c->s_al;
    }
    if (hr->serial_number != NULL && hr->serial_number[0] != '\0') {
        ota_copy_field(c->s_sn, sizeof(c->s_sn), hr->serial_number);
        c->req.serial_number = c->s_sn;
    }
    if (hr->post_body != NULL && hr->post_body[0] != '\0') {
        ota_copy_field(c->s_post, sizeof(c->s_post), hr->post_body);
        c->req.post_body = c->s_post;
    }
}

static void ota_http_host_header(const char *host, uint16_t port, char *dst, size_t dstlen)
{
    if (dstlen == 0U || host == NULL) {
        return;
    }
    if (port == 80U) {
        ota_copy_field(dst, dstlen, host);
        return;
    }
    size_t hl = strlen(host);
    if (hl + 1U >= dstlen) {
        dst[0] = '\0';
        return;
    }
    memcpy(dst, host, hl);
    dst[hl] = ':';
    char pbuf[8];
    size_t pl = ota_ultoa(pbuf, sizeof(pbuf), (unsigned long)port);
    if (hl + 1U + pl >= dstlen) {
        dst[0] = '\0';
        return;
    }
    memcpy(dst + hl + 1U, pbuf, pl);
    dst[hl + 1U + pl] = '\0';
}

static int ota_parse_http_url(const char *url, char *host, size_t host_sz, uint16_t *port,
                              char *uri, size_t uri_sz)
{
    /* http:// 与 https:// 都接受：TLS 的挂载由 mongoose 按 URL 前缀自行处理 */
    bool is_ssl = false;
    const char *p;

    if (strncmp(url, "https://", 8) == 0) {
        p = url + 8;
        is_ssl = true;
    } else if (strncmp(url, "http://", 7) == 0) {
        p = url + 7;
    } else {
        return -1;
    }
    const char *slash = strchr(p, '/');
    size_t host_len = slash != NULL ? (size_t)(slash - p) : strlen(p);
    if (host_len == 0U || host_len >= host_sz) {
        return -1;
    }
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    *port = is_ssl ? 443U : 80U;
    char *colon = strchr(host, ':');
    if (colon != NULL) {
        *colon = '\0';
        *port = (uint16_t)atoi(colon + 1);
    }

    if (slash != NULL) {
        size_t plen = strlen(slash);
        if (plen >= uri_sz) {
            return -1;
        }
        memcpy(uri, slash, plen + 1U);
    } else {
        if (uri_sz < 2U) {
            return -1;
        }
        uri[0] = '/';
        uri[1] = '\0';
    }
    return 0;
}

static const char *ota_http_method(const ota_http_request_t *req)
{
    if (req != NULL && req->method != NULL && req->method[0] != '\0') {
        return req->method;
    }
    if (req != NULL && req->post_body != NULL && req->post_body[0] != '\0') {
        return "POST";
    }
    return "GET";
}

static errcode_t ota_http_build_request_buf(const char *uri, const char *host_hdr, const ota_http_request_t *req,
                                            char *buf, size_t cap, size_t *out_len)
{
    const char *method = ota_http_method(req);
    const char *av = "1";
    const char *ua = "xiaohong-ota/1.0";
    const char *al = "zh-CN";

    if (req != NULL) {
        if (req->activation_version != NULL && req->activation_version[0] != '\0') {
            av = req->activation_version;
        }
        if (req->user_agent != NULL && req->user_agent[0] != '\0') {
            ua = req->user_agent;
        }
        if (req->accept_language != NULL && req->accept_language[0] != '\0') {
            al = req->accept_language;
        }
    }

    const char *body = "";
    size_t body_len = 0;
    if (req != NULL && req->post_body != NULL) {
        body = req->post_body;
        body_len = strlen(body);
    }

    if (buf == NULL || cap == 0U || out_len == NULL) {
        return ERRCODE_FAIL;
    }

    const char *dev = "";
    const char *cli = "";
    if (req != NULL && req->device_id != NULL) {
        dev = req->device_id;
    }
    if (req != NULL && req->client_id != NULL) {
        cli = req->client_id;
    }

    size_t off = 0U;
    if (ota_append(buf, cap, &off, method) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, " ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, uri) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, " HTTP/1.1\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Host: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, host_hdr) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Connection: close\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Content-Type: application/json\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Activation-Version: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, av) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Device-Id: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, dev) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Client-Id: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, cli) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "User-Agent: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, ua) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "Accept-Language: ") != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, al) != 0) {
        return ERRCODE_FAIL;
    }
    if (ota_append(buf, cap, &off, "\r\n") != 0) {
        return ERRCODE_FAIL;
    }
    if (req != NULL && req->serial_number != NULL && req->serial_number[0] != '\0') {
        if (ota_append(buf, cap, &off, "Serial-Number: ") != 0) {
            return ERRCODE_FAIL;
        }
        if (ota_append(buf, cap, &off, req->serial_number) != 0) {
            return ERRCODE_FAIL;
        }
        if (ota_append(buf, cap, &off, "\r\n") != 0) {
            return ERRCODE_FAIL;
        }
    }
    if (body_len > 0U) {
        if (ota_append(buf, cap, &off, "Content-Length: ") != 0) {
            return ERRCODE_FAIL;
        }
        char num[16];
        size_t nl = ota_ultoa(num, sizeof(num), (unsigned long)body_len);
        if (ota_append_buf(buf, cap, &off, num, nl) != 0) {
            return ERRCODE_FAIL;
        }
        if (ota_append(buf, cap, &off, "\r\n\r\n") != 0) {
            return ERRCODE_FAIL;
        }
        if (ota_append_buf(buf, cap, &off, body, body_len) != 0) {
            return ERRCODE_FAIL;
        }
    } else {
        if (ota_append(buf, cap, &off, "\r\n") != 0) {
            return ERRCODE_FAIL;
        }
    }
    *out_len = off;
    return ERRCODE_SUCC;
}

/** mongoose 建连后的上下文：收齐原始响应字节，交给既有解析器处理（支持 chunked）。 */
typedef struct {
    char *resp;
    size_t resp_cap;
    size_t total;
    bool done;
    const char *req;
    size_t req_len;
} ota_mg_ctx_t;

static void ota_http_mg_ev_handler(struct mg_connection *c, int ev, void *ev_data)
{
    ota_mg_ctx_t *ctx = (ota_mg_ctx_t *)c->fn_data;
    if (ctx == NULL) {
        return;
    }

    switch (ev) {
        case MG_EV_CONNECT:
            /*
             * 明文连接：立刻发请求。
             * TLS 连接：此处握手尚未完成，若此时 mg_send 会以未就绪的密钥加密，
             * 故改为等 MG_EV_TLS_HS（握手成功）再发。
             */
            if (!c->is_tls) {
                (void)mg_send(c, ctx->req, ctx->req_len);
            }
            break;
        case MG_EV_TLS_HS:
            (void)mg_send(c, ctx->req, ctx->req_len);
            break;
        case MG_EV_READ: {
            size_t n = c->recv.len;
            if (n == 0U) {
                break;
            }
            if (ctx->total + n > ctx->resp_cap - 1U) {
                log_error("[OTA-HTTP] response too large\r\n");
                mg_error(c, "ota resp too large");
                break;
            }
            memcpy(ctx->resp + ctx->total, c->recv.buf, n);
            ctx->total += n;
            mg_iobuf_del(&c->recv, 0, n);
            break;
        }
        case MG_EV_ERROR:
            log_error("[OTA-HTTP] mongoose error: %s\r\n",
                      ev_data != NULL ? (const char *)ev_data : "");
            ctx->done = true;
            break;
        case MG_EV_CLOSE:
            ctx->done = true;
            break;
        default:
            break;
    }
}

/**
 * 用 mongoose 完成一次「发请求、收原始响应」。http/https 同一路径：
 * 是否挂 TLS 由 xh_tls_apply() 依 URL 前缀决定（工程 MG_TLS=MG_TLS_BUILTIN）。
 */
static errcode_t ota_http_mg_fetch(const char *url, uint32_t timeout_ms, const char *req, size_t req_len,
                                   char *resp, size_t resp_cap, size_t *resp_len_out)
{
    *resp_len_out = 0U;

    if (url == NULL || req == NULL || resp == NULL || req_len == 0U || resp_cap < 2U) {
        return ERRCODE_FAIL;
    }
    if (timeout_ms == 0U) {
        timeout_ms = OTA_HTTP_DEFAULT_TIMEOUT_MS;
    }

    /* 会话级串行：保证同一时刻只存在一个 mongoose TLS 会话（原并发导致 OOM，见上方注释） */
    ota_http_sess_lock();

    errcode_t res = ERRCODE_FAIL;
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    ota_mg_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.resp = resp;
    ctx.resp_cap = resp_cap;
    ctx.req = req;
    ctx.req_len = req_len;

    OTA_MG_LOCK();
    struct mg_connection *c = mg_connect(&mgr, url, ota_http_mg_ev_handler, &ctx);
    if (c != NULL) {
        /* 必须在 poll 前挂 TLS：否则 mongoose 判定「未调用 mg_tls_init」而静默降级为明文 */
        (void)xh_tls_apply(c, url);
    }
    OTA_MG_UNLOCK();

    if (c != NULL) {
        uint64_t start = mg_millis();
        while (!ctx.done && (mg_millis() - start < (uint64_t)timeout_ms)) {
            OTA_MG_LOCK();
            mg_mgr_poll(&mgr, OTA_HTTP_MGR_POLL_MS);
            OTA_MG_UNLOCK();
            osal_msleep(OTA_HTTP_MGR_POLL_SLEEP_MS);
        }
        if (!ctx.done) {
            log_error("[OTA-HTTP] timeout\r\n");
        }
        if (ctx.total > 0U) {
            resp[ctx.total] = '\0';
            *resp_len_out = ctx.total;
            res = ERRCODE_SUCC;
        }
    } else {
        log_error("[OTA-HTTP] connect failed\r\n");
    }

    OTA_MG_LOCK();
    mg_mgr_free(&mgr);
    OTA_MG_UNLOCK();
    ota_http_sess_unlock();
    return res;
}

/** 解析首行 "HTTP/1.x 200"，无 sscanf。 */
static int ota_parse_http_status_line(const char *raw, int *status)
{
    if (strncmp(raw, "HTTP/", 5) != 0) {
        return -1;
    }
    const char *p = raw;
    while (*p != '\0' && *p != '\r' && *p != '\n') {
        p++;
    }
    const char *line_end = p;
    p = raw;
    while (p < line_end && *p != ' ') {
        p++;
    }
    while (p < line_end && *p == ' ') {
        p++;
    }
    int code = 0;
    int nd = 0;
    while (p < line_end && *p >= '0' && *p <= '9') {
        code = code * 10 + (int)(*p - '0');
        nd++;
        p++;
    }
    if (nd == 0) {
        return -1;
    }
    *status = code;
    return 0;
}

static long ota_parse_content_length_value(const char *raw, const char *hdrend)
{
    const char *cl = strstr(raw, "Content-Length:");
    if (cl == NULL) {
        cl = strstr(raw, "content-length:");
    }
    if (cl == NULL || cl >= hdrend) {
        return -1L;
    }
    const char *v = strchr(cl, ':');
    if (v == NULL) {
        return -1L;
    }
    v++;
    while (v < hdrend && (*v == ' ' || *v == '\t')) {
        v++;
    }
    long n = 0L;
    int has = 0;
    while (v < hdrend && *v >= '0' && *v <= '9') {
        n = n * 10L + (long)(*v - '0');
        has = 1;
        v++;
    }
    return has ? n : -1L;
}

/** 头区内是否声明 Transfer-Encoding: chunked（大小写不敏感） */
static int ota_http_headers_have_chunked(const char *raw, const char *hdrend)
{
    static const char kte[] = "transfer-encoding:";
    const char *p = raw;
    while (p < hdrend) {
        const char *le = strstr(p, "\r\n");
        if (le == NULL || le > hdrend) {
            break;
        }
        size_t linelen = (size_t)(le - p);
        if (linelen >= sizeof(kte)) {
            size_t i;
            for (i = 0; i + 1U < sizeof(kte); i++) {
                char a = p[i];
                char b = kte[i];
                if (a >= 'A' && a <= 'Z') {
                    a = (char)(a + 32);
                }
                if (a != b) {
                    break;
                }
            }
            if (i + 1U == sizeof(kte)) {
                for (const char *q = p; q + 7U <= le; q++) {
                    if ((q[0] == 'c' || q[0] == 'C') && (q[1] == 'h' || q[1] == 'H') &&
                        (q[2] == 'u' || q[2] == 'U') && (q[3] == 'n' || q[3] == 'N') &&
                        (q[4] == 'k' || q[4] == 'K') && (q[5] == 'e' || q[5] == 'E') &&
                        (q[6] == 'd' || q[6] == 'D')) {
                        return 1;
                    }
                }
            }
        }
        p = le + 2;
    }
    return 0;
}

/** RFC 7230 chunked：将分块正文解码到 body_start 起址（紧凑覆盖），成功返回 0 */
static int ota_http_decode_chunked_inplace(char *body_start, size_t body_avail, size_t *out_decoded)
{
    size_t src = 0U;
    size_t dst = 0U;
    *out_decoded = 0U;

    while (src < body_avail) {
        size_t line_start = src;
        while (src < body_avail && body_start[src] != '\r') {
            src++;
        }
        if (src + 1U >= body_avail || body_start[src + 1U] != '\n') {
            return -1;
        }
        size_t hexlen = src - line_start;
        if (hexlen == 0U || hexlen >= 28U) {
            return -1;
        }
        char hb[32];
        memcpy(hb, body_start + line_start, hexlen);
        hb[hexlen] = '\0';
        {
            char *semi = strchr(hb, ';');
            if (semi != NULL) {
                *semi = '\0';
            }
        }
        char *endp = NULL;
        unsigned long chunk_sz = strtoul(hb, &endp, 16);
        if (endp == hb) {
            return -1;
        }
        src += 2U;
        if (chunk_sz == 0UL) {
            body_start[dst] = '\0';
            *out_decoded = dst;
            return 0;
        }
        if (src + chunk_sz > body_avail) {
            return -1;
        }
        if (dst != src) {
            memmove(body_start + dst, body_start + src, (size_t)chunk_sz);
        }
        dst += (size_t)chunk_sz;
        src += (size_t)chunk_sz;
        if (src + 2U > body_avail || body_start[src] != '\r' || body_start[src + 1U] != '\n') {
            return -1;
        }
        src += 2U;
    }
    return -1;
}

/**
 * 零拷贝：body 在 raw 内，末尾写 '\0'（需 body 落在 buffer 内）。
 * 支持 Transfer-Encoding: chunked（不少 HTTP/1.1 服务器对 JSON 仍用分块传输）。
 */
static int ota_http_parse_response_inplace(char *raw, size_t total, int *status, const char **body_out)
{
    if (total < 12U || raw == NULL || status == NULL || body_out == NULL) {
        return -1;
    }

    if (ota_parse_http_status_line(raw, status) != 0) {
        return -1;
    }

    const char *hdrend = strstr(raw, "\r\n\r\n");
    if (hdrend == NULL) {
        return -1;
    }
    char *body_start = (char *)hdrend + 4;
    size_t header_bytes = (size_t)(body_start - raw);
    if (header_bytes > total) {
        return -1;
    }

    long content_len = ota_parse_content_length_value(raw, hdrend);

    size_t body_avail = total - header_bytes;
    size_t body_len = body_avail;

    if (ota_http_headers_have_chunked(raw, hdrend)) {
        size_t dec = 0U;
        if (ota_http_decode_chunked_inplace(body_start, body_avail, &dec) != 0) {
            return -1;
        }
        body_len = dec;
    } else {
        if (content_len >= 0L && (size_t)content_len <= body_avail) {
            body_len = (size_t)content_len;
        }
    }

    if (header_bytes + body_len > total) {
        return -1;
    }
    if (body_len == 0U) {
        *body_out = "";
        return 0;
    }
    if (header_bytes + body_len >= OTA_HTTP_RESP_BUF_MAX) {
        return -1;
    }
    body_start[body_len] = '\0';
    *body_out = body_start;
    return 0;
}

errcode_t ota_check_version_http(const char *url, const char *current_firmware_version,
                                 const ota_http_request_t *http_req,
                                 const ota_parse_hooks_t *hooks, ota_result_t *out)
{
    if (url == NULL || url[0] == '\0' || out == NULL) {
        log_error("[OTA-HTTP] url NULL or empty\r\n");
        return ERRCODE_FAIL;
    }

    size_t url_len = strlen(url);
    if (url_len >= OTA_URL_MAX) {
        log_error("[OTA-HTTP] url length too long\r\n");
        return ERRCODE_FAIL;
    }

    char host[128];
    char uri[384];
    uint16_t port = 80;
    if (ota_parse_http_url(url, host, sizeof(host), &port, uri, sizeof(uri)) != 0) {
        log_error("[OTA-HTTP] parse url failed\r\n");
        return ERRCODE_FAIL;
    }

    char host_hdr[160];
    ota_http_host_header(host, port, host_hdr, sizeof(host_hdr));

    memset(&s_ota_embed, 0, sizeof(s_ota_embed));
    s_ota_embed.req.debug_print_io = 0U;

    s_ota_embed.timeout_ms = OTA_HTTP_DEFAULT_TIMEOUT_MS;
    if (http_req != NULL && http_req->timeout_ms > 0U) {
        s_ota_embed.timeout_ms = http_req->timeout_ms;
    }
    ota_embed_http_req(&s_ota_embed, http_req);

    size_t req_len = 0U;
    if (ota_http_build_request_buf(uri, host_hdr, &s_ota_embed.req, s_ota_req_buf, sizeof(s_ota_req_buf),
                                    &req_len) != ERRCODE_SUCC) {
        log_error("[OTA-HTTP] build request buf failed\r\n");
        return ERRCODE_FAIL;
    }

    if (s_ota_embed.req.debug_print_io != 0U) {
        log_debug("[OTA-HTTP] url=%s\r\n", url);
        if (s_ota_embed.req.post_body != NULL && s_ota_embed.req.post_body[0] != '\0') {
            log_debug("[OTA-HTTP] POST body: %s\r\n", s_ota_embed.req.post_body);
        } else {
            log_debug("[OTA-HTTP] POST body: (empty)\r\n");
        }
    }

    size_t raw_len = 0U;
    if (ota_http_mg_fetch(url, s_ota_embed.timeout_ms, s_ota_req_buf, req_len, s_ota_raw_buf,
                          sizeof(s_ota_raw_buf), &raw_len) != ERRCODE_SUCC) {
        log_error("[OTA-HTTP] fetch failed\r\n");
        return ERRCODE_FAIL;
    }

    int http_status = -1;
    const char *body = "";
    if (ota_http_parse_response_inplace(s_ota_raw_buf, raw_len, &http_status, &body) != ERRCODE_SUCC) {
        if (s_ota_embed.req.debug_print_io != 0U) {
            log_error("[OTA-HTTP] response parse fail raw_len=%u\r\n", (unsigned)raw_len);
            size_t n = raw_len < 512U ? raw_len : 512U;
            if (n > 0U && n < sizeof(s_ota_raw_buf)) {
                char tmp[520];
                memcpy(tmp, s_ota_raw_buf, n);
                tmp[n] = '\0';
                log_error("[OTA-HTTP] raw (prefix): %s\r\n", tmp);
            }
        }
        log_error("[OTA-HTTP] response parse fail\r\n");
        return ERRCODE_FAIL;
    }

    if (s_ota_embed.req.debug_print_io != 0U) {
        log_debug("[OTA-HTTP] HTTP status=%d\r\n", http_status);
        if (body != NULL && body[0] != '\0') {
        } else {
            log_debug("[OTA-HTTP] response body: (empty)\r\n");
        }
    }
    if (http_status != 200) {
        log_error("[OTA-HTTP] HTTP status=%d\r\n", http_status);
        return ERRCODE_FAIL;
    }

    int business_code = 0;
    int code_ret = ota_http_try_get_business_code(body, &business_code);
    if (code_ret == 0 && business_code != 0) {
        log_error("[OTA-HTTP] business code=%d (treat as fail)\r\n", business_code);
        return ERRCODE_FAIL;
    }

    return ota_parse_check_response(body, current_firmware_version, hooks, out);
}

errcode_t ota_activate_http(const char *check_base_url, const ota_http_request_t *http_req,
                            const char *activate_json_body, int *http_status_out, ota_result_t *parse_on_200)
{
    if (check_base_url == NULL || check_base_url[0] == '\0' || activate_json_body == NULL ||
        activate_json_body[0] == '\0') {
        return ERRCODE_FAIL;
    }

    char url[OTA_URL_MAX];
    ota_make_activate_url(check_base_url, url, sizeof(url));
    if (url[0] == '\0') {
        return ERRCODE_FAIL;
    }

    char host[128];
    char uri[384];
    uint16_t port = 80;
    if (ota_parse_http_url(url, host, sizeof(host), &port, uri, sizeof(uri)) != 0) {
        return ERRCODE_FAIL;
    }

    char host_hdr[160];
    ota_http_host_header(host, port, host_hdr, sizeof(host_hdr));

    memset(&s_ota_embed, 0, sizeof(s_ota_embed));
    s_ota_embed.timeout_ms = OTA_HTTP_DEFAULT_TIMEOUT_MS;
    if (http_req != NULL && http_req->timeout_ms > 0U) {
        s_ota_embed.timeout_ms = http_req->timeout_ms;
    }
    ota_http_request_t merged;
    memset(&merged, 0, sizeof(merged));
    if (http_req != NULL) {
        merged = *http_req;
    }
    s_ota_embed.req.debug_print_io = 1U;
    merged.debug_print_io = 1U;

    merged.method = "POST";
    merged.post_body = activate_json_body;
    ota_embed_http_req(&s_ota_embed, &merged);

    size_t req_len = 0U;
    if (ota_http_build_request_buf(uri, host_hdr, &s_ota_embed.req, s_ota_req_buf, sizeof(s_ota_req_buf),
                                   &req_len) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    if (s_ota_embed.req.debug_print_io != 0U) {
        printf("[OTA-HTTP] activate url=%s\r\n", url);
        printf("[OTA-HTTP] activate POST: %s\r\n", activate_json_body);
    }

    size_t raw_len = 0U;
    if (ota_http_mg_fetch(url, s_ota_embed.timeout_ms, s_ota_req_buf, req_len, s_ota_raw_buf,
                          sizeof(s_ota_raw_buf), &raw_len) != ERRCODE_SUCC) {
        return ERRCODE_FAIL;
    }

    int http_status = -1;
    const char *body = "";
    if (ota_http_parse_response_inplace(s_ota_raw_buf, raw_len, &http_status, &body) != 0) {
        return ERRCODE_FAIL;
    }

    if (s_ota_embed.req.debug_print_io != 0U) {
        printf("[OTA-HTTP] activate HTTP status=%d\r\n", http_status);
        if (body != NULL && body[0] != '\0') {
            printf("[OTA-HTTP] activate body: %s\r\n", body);
        }
    }

    if (http_status != 200 && http_status != 202) {
        return ERRCODE_FAIL;
    }

    if (http_status_out != NULL) {
        *http_status_out = http_status;
    }

    if (http_status == 200 && parse_on_200 != NULL && body != NULL && body[0] != '\0') {
        (void)ota_parse_check_response(body, (char*)get_fw_ver_str(), NULL, parse_on_200);
    }

    return ERRCODE_SUCC;
}
