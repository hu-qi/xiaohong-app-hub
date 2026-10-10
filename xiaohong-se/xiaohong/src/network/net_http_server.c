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
#include "net_wifi_config.h"
#include <stdio.h>
#include <string.h>

extern osEventFlagsId_t net_cfg_evt_id;
extern char strSSID[WIFI_SSID_LEN_MAX];
extern char strPSWD[WIFI_PSWD_LEN_MAX];
extern char strOTAA[OTA_ADDR_LEN_MAX];   // OTA地址
extern volatile char* scan_ssid_list;

/** 在 s[0..len) 内查找子串 sub（长度 sublen），不含 '\0' 依赖 */
static const char *http_mem_find(const char *s, u16_t len, const char *sub, u16_t sublen)
{
    u16_t i;

    if (sub == NULL || sublen == 0 || len < sublen) {
        return NULL;
    }
    for (i = 0; i + sublen <= len; i++) {
        if (memcmp(s + i, sub, sublen) == 0) {
            return s + i;
        }
    }
    return NULL;
}

#define HTTP_POST_BODY_MAX  (1024U)

/** 从头区 [hdr, hdr+hdr_len) 解析 Content-Length，失败返回 -1 */
static int http_parse_content_length(const char *hdr, u16_t hdr_len, u32_t *out_len)
{
    const char *cl = http_mem_find(hdr, hdr_len, "Content-Length:", 15);

    if (cl == NULL) {
        cl = http_mem_find(hdr, hdr_len, "content-length:", 15);
    }
    if (cl == NULL || out_len == NULL) {
        return -1;
    }

    const char *v = cl + 15;
    const char *hdr_end = hdr + hdr_len;

    while (v < hdr_end && (*v == ' ' || *v == '\t')) {
        v++;
    }

    u32_t n = 0U;
    bool has_digit = false;

    while (v < hdr_end && *v >= '0' && *v <= '9') {
        n = n * 10U + (u32_t)(*v - '0');
        if (n > HTTP_POST_BODY_MAX) {
            return -1;
        }
        has_digit = true;
        v++;
    }
    if (!has_digit) {
        return -1;
    }
    *out_len = n;
    return 0;
}

/** 按 Content-Length 从 TCP 流收全 POST body；body_out 由调用方提供 */
static bool http_recv_full_post_body(struct netconn *conn,
    const char *body_start, u16_t body_in_first, u32_t content_length,
    char *body_out, size_t body_out_cap, u32_t *body_out_len)
{
    if (body_out == NULL || body_out_len == NULL || content_length > body_out_cap) {
        return false;
    }
    if (body_in_first > content_length) {
        body_in_first = (u16_t)content_length;
    }
    if (body_in_first > 0U) {
        (void)memcpy_s(body_out, body_out_cap, body_start, (size_t)body_in_first);
    }

    u32_t got = (u32_t)body_in_first;
    while (got < content_length) {
        struct netbuf *more = NULL;
        err_t err = netconn_recv(conn, &more);

        if (err != ERR_OK || more == NULL) {
            printf("[HSerTask] POST body recv incomplete: err=%d got=%u need=%u\r\n",
                (int)err, (unsigned int)got, (unsigned int)content_length);
            if (more != NULL) {
                netbuf_delete(more);
            }
            return false;
        }

        char *chunk = NULL;
        u16_t chunk_len = 0;

        netbuf_data(more, (void **)&chunk, &chunk_len);
        u32_t need = content_length - got;
        u32_t copy = ((u32_t)chunk_len > need) ? need : (u32_t)chunk_len;
        if (copy > 0U && chunk != NULL) {
            (void)memcpy_s(body_out + got, body_out_cap - (size_t)got, chunk, (size_t)copy);
            got += copy;
        }
        netbuf_delete(more);
    }

    *body_out_len = content_length;
    return true;
}

static int uri_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return (int)(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a' + 10);
    }
    return -1;
}

/**
 * 从 x-www-form-urlencoded 取值写入 dst：decodeURIComponent 对应 %HH 序列，'+' 视为空格。
 * UTF-8 多字节在表单里会按字节分别 % 编码，按字节解码即可还原。
 */
static void form_copy_field_url_decoded(char *dst, size_t dst_cap, const char *val_start, const char *val_end)
{
    const char *p = val_start;
    size_t max_out = (dst_cap > 0U) ? (dst_cap - 1U) : 0U;
    size_t o = 0U;

    if (dst == NULL || max_out == 0U) {
        return;
    }
    (void)memset_s(dst, dst_cap, 0, dst_cap);

    while (p < val_end && o < max_out) {
        if (*p == '+') {
            dst[o++] = ' ';
            p++;
        } else if (*p == '%' && (p + 2) < val_end) {
            int hi = uri_hex_nibble(p[1]);
            int lo = uri_hex_nibble(p[2]);
            if (hi >= 0 && lo >= 0) {
                dst[o++] = (char)(((unsigned int)hi << 4) | (unsigned int)lo);
                p += 3;
                continue;
            }
            dst[o++] = *p++;
        } else {
            dst[o++] = *p++;
        }
    }
    dst[o] = '\0';
}

/** POST 表单字段 voice_interrupt=0|1，写入 config.json 并供 Agent listen mode 使用 */
static void http_apply_voice_interrupt_from_form(const char *body, u16_t body_len)
{
    const char *body_end = body + body_len;
    const char *p = http_mem_find(body, body_len, "voice_interrupt=", 16);
    if (p == NULL) {
        return;
    }
    const char *v = p + 16;
    const char *e = v;

    while (e < body_end && *e != '&') {
        e++;
    }
    char tmp[16];
    form_copy_field_url_decoded(tmp, sizeof(tmp), v, e);
    int on = -1;
    if (tmp[0] == '1' && tmp[1] == '\0') {
        on = 1;
    } else if (tmp[0] == '0' && tmp[1] == '\0') {
        on = 0;
    }
    if (on < 0) {
        return;
    }
    errcode_t rc = set_voice_interrupt_enabled(on);
    printf("[HSerTask] voice_interrupt POST -> %d, set_voice_interrupt_enabled rc=%d\r\n", on, (int)rc);
}

/** true：已提交 WiFi 配网，需保持连接直至成功/失败后再响应 */
static bool http_handle_post_body(const char *body, u16_t body_len)
{
    const char *body_end = body + body_len;
    const char *p_addr = http_mem_find(body, body_len, "ota_addr=", 9);
    const char *p_ssid = http_mem_find(body, body_len, "wifi_ssid=", 10);
    const char *p_pswd = http_mem_find(body, body_len, "wifi_pswd=", 10);
    bool wifi_cfg_posted = false;

    http_apply_voice_interrupt_from_form(body, body_len);

    if (p_addr != NULL) {
        const char *v = p_addr + 9;
        const char *e = v;

        while (e < body_end && *e != '&') {
            e++;
        }
        form_copy_field_url_decoded(strOTAA, OTA_ADDR_LEN_MAX, v, e);
    }

    if (p_ssid != NULL && p_pswd != NULL) {
        const char *v_ssid = p_ssid + 10;
        const char *e_ssid = v_ssid;

        while (e_ssid < body_end && *e_ssid != '&') {
            e_ssid++;
        }
        form_copy_field_url_decoded(strSSID, WIFI_SSID_LEN_MAX, v_ssid, e_ssid);

        const char *v_pswd = p_pswd + 10;
        const char *e_pswd = v_pswd;

        while (e_pswd < body_end && *e_pswd != '&') {
            e_pswd++;
        }
        form_copy_field_url_decoded(strPSWD, WIFI_PSWD_LEN_MAX, v_pswd, e_pswd);

        (void)osEventFlagsSet(net_cfg_evt_id, WIFI_CONFIG_INITIATE_EVENT);
        wifi_cfg_posted = true;
    }

    return wifi_cfg_posted;
}

typedef enum {
    HTTP_SERVE_CLOSE = 0,
    HTTP_SERVE_KEEP_FOR_WIFI_RESULT,
} http_serve_result_t;

static err_t http_netconn_write_all(struct netconn *conn, const void *data, size_t len)
{
    const char *p = (const char *)data;
    size_t sent = 0U;
    err_t err;

    while (sent < len) {
        size_t chunk = len - sent;

        if (chunk > 0xFFFFU) {
            chunk = 0xFFFFU;
        }
        err = netconn_write(conn, p + sent, (uint16_t)chunk, NETCONN_COPY);
        if (err != ERR_OK) {
            return err;
        }
        sent += chunk;
    }
    return ERR_OK;
}

static bool http_send_wifi_config_response(struct netconn *conn, bool success)
{
    static const char msg_ok[] =
        "<html><head><meta charset=\"UTF-8\"></head><body>"
        "<div style=\"background:#d4f5e0;color:#1a7f37;padding:16px;border-radius:10px;\">"
        "<h1 style=\"margin:0 0 8px;font-size:1.25rem;\">配网成功</h1>"
        "<p style=\"margin:0;font-size:15px;\">设备已联网，可以开始使用。</p></div></body></html>";
    static const char msg_fail[] =
        "<html><head><meta charset=\"UTF-8\"></head><body>"
        "<div style=\"background:#fde0e0;color:#c41e1e;padding:16px;border-radius:10px;\">"
        "<h1 style=\"margin:0 0 8px;font-size:1.25rem;\">配网失败</h1>"
        "<p style=\"margin:0;font-size:15px;\">请检查 WiFi 名称和密码后重试。</p></div></body></html>";
    const char *body;
    char hdr[192];
    int hdr_len;
    size_t body_len;
    err_t err;

    if (success) {
        body = msg_ok;
        printf("[HSerTask] notify client: config success\r\n");
    } else {
        body = msg_fail;
        printf("[HSerTask] notify client: config failure\r\n");
    }

    body_len = strlen(body);
    hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        (unsigned int)body_len);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return false;
    }

    err = http_netconn_write_all(conn, hdr, (size_t)hdr_len);
    if (err != ERR_OK) {
        printf("[HSerTask] http_send_html_response: hdr err=%d\r\n", (int)err);
        return false;
    }
    err = http_netconn_write_all(conn, body, body_len);
    if (err != ERR_OK) {
        printf("[HSerTask] http_send_html_response: body err=%d\r\n", (int)err);
        return false;
    }
    printf("[HSerTask] http_send_html_response: body OK\r\n");
    sleep(5);  /* 等手机收到结果再发送DONE_EVENT */
    (void)netconn_shutdown(conn, 0, 1);
    (void)osEventFlagsSet(net_cfg_evt_id, WIFI_CONFIG_NOTIFY_DONE_EVENT);
    return true;
}

/** osEventFlagsWait 超时/错误返回值，勿与事件位混淆 */
static bool http_event_flags_valid(uint32_t flag)
{
    return (flag != (uint32_t)osFlagsError) && (flag != (uint32_t)osFlagsErrorResource) &&
           (flag != (uint32_t)osFlagsErrorParameter) && (flag != (uint32_t)osFlagsErrorISR) &&
           (flag != (uint32_t)osFlagsErrorTimeout);
}

/** 保持 POST 连接，等待配网结果后向手机返回 HTTP 响应；返回 true 表示配网成功 */
static bool http_wait_wifi_config_result(struct netconn *client)
{
    uint32_t flag;
    uint32_t spin;

    (void)osEventFlagsClear(net_cfg_evt_id,
                            WIFI_CONFIG_SUCCESS_EVENT | WIFI_CONFIG_FAILURE_EVENT |
                            WIFI_CONFIG_CONNECTING_EVENT | WIFI_CONFIG_NOTIFY_DONE_EVENT);

    for (spin = 0U; spin < 6000U; spin++) {
        if ((osEventFlagsGet(net_cfg_evt_id) & WIFI_CONFIG_INITIATE_EVENT) == 0U) {
            break;
        }
        osDelay(10);
    }

    for (;;) {
        flag = osEventFlagsWait(net_cfg_evt_id,
                                WIFI_CONFIG_SUCCESS_EVENT | WIFI_CONFIG_FAILURE_EVENT |
                                    WIFI_CONFIG_CONNECTING_EVENT,
                                osFlagsWaitAny,
                                osWaitForever);
        if (!http_event_flags_valid(flag)) {
            continue;
        }
        if ((flag & WIFI_CONFIG_CONNECTING_EVENT) != 0U) {
            continue;
        }
        if ((flag & WIFI_CONFIG_SUCCESS_EVENT) != 0U) {
            (void)http_send_wifi_config_response(client, true);
            return true;
        }
        if ((flag & WIFI_CONFIG_FAILURE_EVENT) != 0U) {
            (void)http_send_wifi_config_response(client, false);
            printf("[HSerTask] config_result: WIFI_CONFIG_FAILURE_EVENT\r\n");
            return false;
        }
    }
}

static void http_send_config_page(struct netconn *conn,
    const char *html_header, size_t html_header_len,
    const char *html_prefix, size_t html_prefix_len,
    const char *wifi_script, size_t wifi_script_len,
    const char *html_suffix, size_t html_suffix_len)
{
    (void)netconn_write(conn, html_header, html_header_len, NETCONN_NOCOPY);
    (void)netconn_write(conn, html_prefix, html_prefix_len, NETCONN_NOCOPY);
    (void)netconn_write(conn, wifi_script, wifi_script_len, NETCONN_NOCOPY);
    (void)netconn_write(conn, html_suffix, html_suffix_len, NETCONN_NOCOPY);
}

static http_serve_result_t http_server_netconn_serve(struct netconn *conn,
    const char *html_header, size_t html_header_len,
    const char *html_prefix, size_t html_prefix_len,
    const char *wifi_script, size_t wifi_script_len,
    const char *html_suffix, size_t html_suffix_len)
{
    struct netbuf *inbuf = NULL;
    char *buf = NULL;
    u16_t buflen = 0;
    err_t err;
    bool wifi_cfg_posted = false;

    (void)netconn_set_recvtimeout(conn, 5000);

    err = netconn_recv(conn, &inbuf);
    if (err != ERR_OK) {
        goto out_close;
    }

    netbuf_data(inbuf, (void **)&buf, &buflen);

    if (buflen >= 5U && memcmp(buf, "GET /", 5) == 0 &&
        (buflen == 5U || buf[5] == ' ' || buf[5] == '?')) {
        printf("[HSerTask] GET / page: prefix[%u] script[%u] suffix[%u]\r\n",
            (unsigned int)html_prefix_len, (unsigned int)wifi_script_len,
            (unsigned int)html_suffix_len);
        http_send_config_page(conn, html_header, html_header_len,
            html_prefix, html_prefix_len, wifi_script, wifi_script_len,
            html_suffix, html_suffix_len);
    } else if (buflen >= 5U && memcmp(buf, "POST ", 5) == 0) {
        printf("[HSerTask] POST request, first recv[%u]\r\n", (unsigned int)buflen);
        const char *sep = http_mem_find(buf, buflen, "\r\n\r\n", 4);
        if (sep != NULL) {
            u16_t hdr_len = (u16_t)(sep - buf);
            const char *body = sep + 4;
            u16_t body_in_first = buflen - (u16_t)(body - buf);
            u32_t content_length = 0U;
            u32_t body_len = 0U;
            const char *body_ptr = body;
            static char post_body[HTTP_POST_BODY_MAX];

            if (http_parse_content_length(buf, hdr_len, &content_length) == 0) {
                printf("[HSerTask] POST Content-Length=%u first_chunk=%u\r\n",
                    (unsigned int)content_length, (unsigned int)body_in_first);

                if (http_recv_full_post_body(conn, body, body_in_first, content_length,
                        post_body, sizeof(post_body), &body_len)) {
                    body_ptr = post_body;
                } else {
                    body_len = (u32_t)body_in_first;
                }
            } else {
                printf("[HSerTask] POST no Content-Length, use first_chunk=%u\r\n",
                    (unsigned int)body_in_first);
                body_len = (u32_t)body_in_first;
            }
            if (body_len > 0U) {
                #if 1
                    printf("[HSerTask] POST body[%u]\r\n", (unsigned int)body_len);
                #else
                    printf("[HSerTask] POST body[%u]: %.*s\r\n",
                        (unsigned int)body_len, (int)body_len, body_ptr);
                #endif
            }
            wifi_cfg_posted = http_handle_post_body(body_ptr, (u16_t)body_len);
        } else {
            printf("[HSerTask] POST request, NOT found empty line!\r\n");
        }

        if (!wifi_cfg_posted) {
            printf("[HSerTask] POST no wifi config, send ACK!\r\n");

            static const char post_ack[] =
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=UTF-8\r\n"
                "Content-Length: 2\r\nConnection: close\r\n\r\nOK";
            (void)http_netconn_write_all(conn, post_ack, sizeof(post_ack) - 1U);
        }
    }

    if (inbuf != NULL) {
        netbuf_delete(inbuf);
        inbuf = NULL;
    }
    if (wifi_cfg_posted) {
        printf("[HSerTask] POST wifi config, keep connection for result\r\n");
        return HTTP_SERVE_KEEP_FOR_WIFI_RESULT;
    }

out_close:
    if (inbuf != NULL) {
        netbuf_delete(inbuf);
    }
    netconn_close(conn);
    return HTTP_SERVE_CLOSE;
}

#define HTTP_HTML_PREFIX_LEN  (2560)  /* 配网页静态前缀（至 script 注入点） */
/* 32 个 SSID：var ssid_list=[...] + 填充 option 脚本 */
#define WIFI_SCRIPT_LEN       (1536)

errcode_t HttpServerTask(void)
{
    err_t ret = ERR_OK;
    char ifname[] = "ap0";
    uint16_t port = 80;
    struct netconn *conn = NULL;
    struct netconn *newconn = NULL;

    struct netif *netif_p = netifapi_netif_find(ifname);
    if (netif_p == NULL) {
        printf("[HSerTask] netif_find fail\r\n");
        return ERRCODE_FAIL;
    }
    const ip_addr_t server_ip = netif_p->ip_addr;

    conn = netconn_new(NETCONN_TCP);
    if (conn == NULL) {
        printf("[HSerTask] netconn_new invalid conn\r\n");
        return ERRCODE_FAIL;
    }
    conn->recv_timeout = 100;
    printf("[HSerTask] netconn_new OK, set recv_timeout to 100ms\r\n");

    ret = netconn_bind(conn, &server_ip, port);
    if (ret != ERR_OK) {
        printf("[HSerTask] netconn_bind err:%d\r\n", ret);
        netconn_delete(conn);
        return ERRCODE_FAIL;
    }
    ret = netconn_listen(conn);
    if (ret != ERR_OK) {
        printf("[HSerTask] netconn_listen err:%d\r\n", ret);
        netconn_delete(conn);
        return ERRCODE_FAIL;
    }

    static const char http_html_hdr[] =
        "HTTP/1.1 200 OK\r\nContent-type: text/html; charset=UTF-8\r\n\r\n";
    const size_t http_html_hdr_len = sizeof(http_html_hdr) - 1U;

    char http_html_buff[HTTP_HTML_PREFIX_LEN] =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">\
<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,viewport-fit=cover\">\
<title>设备配网</title><style>\
*{box-sizing:border-box;}\
html{-webkit-text-size-adjust:100%;}\
body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,\"PingFang SC\",\"Microsoft YaHei\",sans-serif;\
font-size:16px;line-height:1.5;margin:0;padding:16px;padding-bottom:max(16px,env(safe-area-inset-bottom));\
background:#f2f2f7;color:#1c1c1e;}\
.wrap{max-width:520px;margin:0 auto;}\
h1{font-size:1.35rem;margin:8px 0 18px;text-align:center;font-weight:700;}\
h2{font-size:1rem;font-weight:600;margin:0 0 14px;color:#3a3a3c;}\
.field{margin-bottom:14px;}\
input[type=text],input[type=password],select{\
display:block;width:100%;max-width:100%;min-height:48px;padding:12px 14px;font-size:16px;\
border:1px solid #c7c7cc;border-radius:10px;background:#fff;}\
input[type=text],input[type=password]{-webkit-appearance:none;appearance:none;}\
select:invalid{color:#8e8e93;}\
button{width:100%;min-height:50px;margin-top:6px;padding:12px 16px;font-size:17px;font-weight:600;\
color:#fff;background:#007aff;border:none;border-radius:12px;cursor:pointer;-webkit-tap-highlight-color:transparent;}\
button:active{opacity:.88;}\
label.chk{display:flex;align-items:flex-start;gap:12px;margin:16px 0;line-height:1.45;font-size:15px;color:#3a3a3c;}\
label.chk input[type=checkbox]{width:22px;height:22px;min-height:22px;margin-top:2px;flex-shrink:0;}\
</style></head><body><div class=\"wrap\">\
<h1>欢迎进入配网页面</h1>\
<h2>请输入 OTA 地址（留空则用默认）、WiFi 名称和密码</h2>\
<div class=\"field\"><input type=\"text\" id=\"ota_addr\" placeholder=\"OTA 地址（可选）\" maxlength=\"80\"></div>\
<div class=\"field\"><select id=\"wifi_ssid\" required>\
<option value=\"\" disabled selected>请选择 WiFi 名称</option>\
</select></div>\
<div class=\"field\"><input type=\"password\" id=\"wifi_pswd\" placeholder=\"WiFi 密码\" maxlength=\"32\"></div>\
<label class=\"chk\"><input type=\"checkbox\" id=\"voice_interrupt\">\
<span>语音打断 TTS（开启后对话使用 realtime，需服务端支持）</span></label>\
<div id=\"cfg_status\" style=\"margin:14px 0;text-align:center;font-size:15px;min-height:1.2em;\"></div>\
<button type=\"button\" id=\"cfg_btn\" onclick=\"readInput()\">开始配网</button>\
<script>\
window.onload=function(){\n\
var select=document.getElementById(\"wifi_ssid\");\n\
";

    char wifi_script[WIFI_SCRIPT_LEN];

    if (scan_ssid_list != NULL) {
        (void)snprintf(wifi_script, sizeof(wifi_script), "var ssid_list = [%s];", scan_ssid_list);
    } else {
        (void)snprintf(wifi_script, sizeof(wifi_script), "var ssid_list = [];");
    }
    {
        size_t off = strlen(wifi_script);
        size_t rem = sizeof(wifi_script) - off;
        int w2;

        if (rem > 0U) {
            w2 = snprintf(wifi_script + off, rem,
                "for(var i=0; i<ssid_list.length; i++){"
                "    select.add(new Option(ssid_list[i], ssid_list[i]));"
                "}"
                "document.getElementById(\"voice_interrupt\").checked = %s;",
                get_voice_interrupt_enabled() ? "true" : "false");
            if (w2 < 0 || (size_t)w2 >= rem) {
                printf("[HSerTask] wifi_script truncated (need more than %u bytes)\r\n",
                    (unsigned int)WIFI_SCRIPT_LEN);
            }
        } else {
            printf("[HSerTask] wifi_script truncated (need more than %u bytes)\r\n",
                (unsigned int)WIFI_SCRIPT_LEN);
        }
        //printf("[HSerTask] wifi_script:[%d][%s]\r\n", strlen(wifi_script), wifi_script);
    }

    static const char http_html_index_end[] =
            "}\n\
            function cfgShowStatus(el, html) {\
                if (!el) return;\
                el.innerHTML = html;\
            }\
            function readInput() {\
                var input_addr = document.getElementById(\"ota_addr\");\
                var value_addr = input_addr.value;\
                var select_ssid = document.getElementById(\"wifi_ssid\");\
                var value_ssid = select_ssid.value;\
                var input_pswd = document.getElementById(\"wifi_pswd\");\
                var value_pswd = input_pswd.value;\
                var chk_vi = document.getElementById(\"voice_interrupt\");\
                var vi = (chk_vi && chk_vi.checked) ? \"1\" : \"0\";\
                var statusEl = document.getElementById(\"cfg_status\");\
                var cfgBtn = document.getElementById(\"cfg_btn\");\
                if (!value_ssid || !value_pswd) {\
                    cfgShowStatus(statusEl, \"<div style=\\\"background:#f2f2f7;color:#3a3a3c;padding:16px;border-radius:10px;\\\"><p style=\\\"margin:0;font-size:15px;\\\">请选择 WiFi 并填写密码</p></div>\");\
                    return;\
                }\
                cfgShowStatus(statusEl, \"<div style=\\\"background:#fff8e6;color:#8a6d00;padding:16px;border-radius:10px;\\\"><p style=\\\"margin:0;font-size:15px;\\\">正在连接 WiFi，请稍候…</p></div>\");\
                if (cfgBtn) cfgBtn.disabled = true;\
                fetch(\"/config\", {\
                    method: \"POST\",\
                    body: \"ota_addr=\" + encodeURIComponent(value_addr) + \
                        \"&wifi_ssid=\" + encodeURIComponent(value_ssid) + \
                        \"&wifi_pswd=\" + encodeURIComponent(value_pswd) + \
                        \"&voice_interrupt=\" + vi + \"&\",\
                    headers: {\"Content-Type\": \"application/x-www-form-urlencoded; charset=UTF-8\"}\
                }).then(function(r){\
                    if (!r.ok) throw new Error(\"http \" + r.status);\
                    return r.text();\
                }).then(function(html){\
                    if (!html) throw new Error(\"empty\");\
                    var doc = new DOMParser().parseFromString(html, \"text/html\");\
                    var box = doc.querySelector(\"div[style]\");\
                    if (!box) box = doc.querySelector(\"body div\");\
                    if (box) {\
                        cfgShowStatus(statusEl, box.outerHTML);\
                    } else {\
                        var h1 = doc.querySelector(\"h1\");\
                        var p = doc.querySelector(\"p\");\
                        var msg = h1 ? h1.textContent : html;\
                        if (p && p.textContent) msg += \" \" + p.textContent;\
                        cfgShowStatus(statusEl, \"<p style=\\\"margin:0;\\\">\" + msg + \"</p>\");\
                    }\
                }).catch(function(){\
                    cfgShowStatus(statusEl, \"<div style=\\\"background:#fde0e0;color:#c41e1e;padding:16px;border-radius:10px;\\\"><p style=\\\"margin:0;font-size:15px;\\\">连接中断，请重试</p></div>\");\
                }).finally(function(){\
                    if (cfgBtn) cfgBtn.disabled = false;\
                });\
            }\
        </script>\
    </div></body>\
    </html>";

    size_t http_html_prefix_len = strlen(http_html_buff);
    size_t wifi_script_len = strlen(wifi_script);
    size_t http_html_suffix_len = strlen(http_html_index_end);

    printf("[HSerTask] config page: prefix[%u] script[%u] suffix[%u] total[%u]\r\n",
        (unsigned int)http_html_prefix_len,
        (unsigned int)wifi_script_len,
        (unsigned int)http_html_suffix_len,
        (unsigned int)(http_html_prefix_len + wifi_script_len + http_html_suffix_len));

    do {
        ret = netconn_accept(conn, &newconn);
        if (ret == ERR_OK) {
            http_serve_result_t serve_ret;
            bool config_ok = false;

            serve_ret = http_server_netconn_serve(newconn,
                http_html_hdr, http_html_hdr_len,
                http_html_buff, http_html_prefix_len,
                wifi_script, wifi_script_len,
                http_html_index_end, http_html_suffix_len);
            if (serve_ret == HTTP_SERVE_KEEP_FOR_WIFI_RESULT) {
                config_ok = http_wait_wifi_config_result(newconn);
            }
            netconn_close(newconn);
            netconn_delete(newconn);
            newconn = NULL;
            if (config_ok) {
                printf("[HSerTask] config_result: WIFI_CONFIG_SUCCESS_EVENT\r\n");
                //sleep(2);
                break;
            }
        } else if (ret == ERR_TIMEOUT) {
            uint32_t flag = osEventFlagsWait(net_cfg_evt_id,
                                             WIFI_CONFIG_SUCCESS_EVENT,
                                             osFlagsWaitAll,
                                             0);
            if (flag == WIFI_CONFIG_SUCCESS_EVENT) {
                printf("[HSerTask] WIFI_CONFIG_SUCCESS_EVENT (no client), prepare exit\r\n");
                break;
            }
        }
    } while (1);

    if (conn != NULL) {
        netconn_close(conn);
        netconn_delete(conn);
        conn = NULL;
    }
    osThreadExit();

    return ERRCODE_SUCC;
}

uint32_t HttpServerTaskEntry(void)
{
    task_config_t task_conf = {
        "HttpServerTask",
        1024 * 10,
        24,
        (void *)HttpServerTask
    };

    return create_task(&task_conf, NULL);
}
// SYS_RUN(HttpServerTaskEntry);
// APP_FEATURE_INIT(HttpServerTaskEntry);
