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
 * @file http_lfs_download.c
 * @brief Mongoose HTTP：流式下载到 LittleFS（Range、校验、.part 原子替换），与业务无关。
 */

#include "http_lfs_download.h"

 #include <ctype.h>
 #include <fcntl.h>
 #include <limits.h>
 #include <stdbool.h>
 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 
 #include "cmsis_os2.h"
 #include "littlefs_adapt.h"
 #include "mongoose_config.h"
 #include "mongoose.h"
 #include "soc_osal.h"
 #include "xh_tls.h"
 
 #if defined(__has_include)
 #if __has_include("mongoose_protocol.h")
 #include "mongoose_protocol.h"
 #define FD_MG_LOCK()   mongoose_api_lock()
 #define FD_MG_UNLOCK() mongoose_api_unlock()
 #endif
 #endif
 #ifndef FD_MG_LOCK
 #define FD_MG_LOCK()   ((void)0)
 #define FD_MG_UNLOCK() ((void)0)
 #endif
 
#define HD_BODY_IDLE_MS      30000UL
#define HD_CONNECT_MS        30000UL
#define HD_OVERALL_MS        300000UL
#ifndef HD_MGR_POLL_MS
#define HD_MGR_POLL_MS       10
#endif
#define HD_PATH_MAX          96
#define HD_URL_MAX           256
 
 typedef enum {
     FD_BODY_NONE = 0,
     FD_BODY_CONTENT_LENGTH,
     FD_BODY_CHUNKED,
     FD_BODY_UNTIL_CLOSE,
 } fd_body_mode_t;
 
 typedef struct {
     int fd;
     int result;
     volatile bool done;
 
     fd_body_mode_t mode;
     size_t content_remaining;
     size_t chunk_payload_left;
 
     bool hdrs_ok;
     uint64_t last_body_ms;
     uint64_t connect_deadline_ms;
     size_t bytes_written;
     size_t expected_bytes;
     bool expected_known;
     bool try_range;
     size_t resume_offset;
     char base_dir[HD_PATH_MAX];
     char final_path[HD_PATH_MAX];
     char path[HD_PATH_MAX];
     char url_buf[HD_URL_MAX];
     bool direct_write;

     http_lfs_progress_fn progress_fn;
     void *progress_arg;
     size_t progress_every_bytes;
     size_t progress_steps_done;
 } hd_ctx_t;
 
 static int fd_parse_content_length(const struct mg_str *s, size_t *out)
 {
     char tmp[32];
     if (s == NULL || s->buf == NULL) {
         return -1;
     }
     size_t n = s->len < sizeof(tmp) - 1U ? s->len : sizeof(tmp) - 1U;
     memcpy(tmp, s->buf, n);
     tmp[n] = '\0';
     char *endp = NULL;
     unsigned long v = strtoul(tmp, &endp, 10);
     if (endp == tmp) {
         return -1;
     }
     *out = (size_t)v;
     return 0;
 }
 
 static bool fd_is_hex_line_char(int c)
 {
     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
 }
 
 static int fd_parse_chunk_size_line(const char *s, size_t line_len, unsigned long *size_out)
 {
     size_t i = 0;
     while (i < line_len && fd_is_hex_line_char((unsigned char)s[i])) {
         i++;
     }
     if (i == 0U) {
         return -1;
     }
     char tmp[12];
     if (i >= sizeof(tmp)) {
         return -1;
     }
     memcpy(tmp, s, i);
     tmp[i] = '\0';
     char *endp = NULL;
     unsigned long v = strtoul(tmp, &endp, 16);
     if (endp == tmp || v == ULONG_MAX) {
         return -1;
     }
     *size_out = v;
     return 0;
 }
 
 /*
  * RFC 7230：TE 可为 "gzip, chunked" 等；勿用整段等于 "chunked" 的误判。
  */
 static bool hd_te_is_chunked(const struct mg_str *te)
 {
     static const char k8[] = "chunked";
     const size_t klen = sizeof(k8) - 1U;
     if (te == NULL || te->buf == NULL || te->len < klen) {
         return false;
     }
     for (size_t i = 0; i + klen <= te->len; i++) {
         size_t j = 0;
         for (; j < klen; j++) {
             unsigned char a = (unsigned char)te->buf[i + j];
             unsigned char b = (unsigned char)k8[j];
             if (a >= 'A' && a <= 'Z') {
                 a = (unsigned char)(a + 32U);
             }
             if (a != b) {
                 break;
             }
         }
         if (j == klen) {
             return true;
         }
     }
     return false;
 }

 static int hd_write_all(int fd, const void *buf, size_t len)
 {
     const unsigned char *p = (const unsigned char *)buf;
     size_t off = 0;
     while (off < len) {
         size_t chunk = len - off;
         if (chunk > (size_t)UINT_MAX) {
             chunk = (size_t)UINT_MAX;
         }
         int w = fs_adapt_write(fd, (const char *)(p + off), (unsigned int)chunk);
         if (w < 0) {
             return -1;
         }
         if (w == 0) {
             return -1;
         }
         off += (size_t)w;
     }
     return 0;
 }

 static void hd_maybe_progress(hd_ctx_t *ctx)
 {
     size_t ev;
     size_t n;
     size_t tot;
     if (ctx->progress_fn == NULL || ctx->progress_every_bytes == 0U) {
         return;
     }
     ev = ctx->progress_every_bytes;
     n = ctx->bytes_written / ev;
     if (n > ctx->progress_steps_done) {
         ctx->progress_steps_done = n;
         tot = ctx->expected_known ? ctx->expected_bytes : 0U;
         ctx->progress_fn(ctx->progress_arg, ctx->bytes_written, tot);
     }
 }
 
 static int fd_parse_content_range_start(const struct mg_str *s, size_t *start_out)
 {
     char tmp[64];
     unsigned long start_ul = 0;
     unsigned long end_ul = 0;
     unsigned long total_ul = 0;
     if (s == NULL || s->buf == NULL || start_out == NULL) {
         return -1;
     }
     size_t n = s->len < sizeof(tmp) - 1U ? s->len : sizeof(tmp) - 1U;
     memcpy(tmp, s->buf, n);
     tmp[n] = '\0';
 
     /* e.g. "bytes 123-456/789" */
     if (sscanf(tmp, "bytes %lu-%lu/%lu", &start_ul, &end_ul, &total_ul) < 2) {
         return -1;
     }
     (void)end_ul;
     (void)total_ul;
     *start_out = (size_t)start_ul;
     return 0;
 }
 
 static void fd_abort(struct mg_connection *c, hd_ctx_t *ctx, int err)
 {
     if (ctx->done) {
         return;
     }
     if (ctx->fd >= 0) {
         (void)fs_adapt_close(ctx->fd);
         ctx->fd = -1;
     }
     ctx->result = err;
     ctx->done = true;
     c->is_closing = 1;
 }
 
 static void fd_finish_ok(struct mg_connection *c, hd_ctx_t *ctx)
 {
     unsigned int final_size = 0U;
 
     if (ctx->done) {
         return;
     }
     if (ctx->fd >= 0) {
         (void)fs_adapt_sync(ctx->fd);
         (void)fs_adapt_close(ctx->fd);
         ctx->fd = -1;
     }
 
     /*
      * 第一层完整性校验：接收写入总字节必须等于 HTTP 头推导的期望总长
      * (200 + Content-Length) 或 (206 + resume_offset + Content-Length)。
      */
     if (ctx->expected_known && (ctx->bytes_written != ctx->expected_bytes)) {
         ctx->result = -1;
         ctx->done = true;
         c->is_closing = 1;
         printf("[HttpLfs] size mismatch: got=%u expected=%u\r\n",
             (unsigned int)ctx->bytes_written, (unsigned int)ctx->expected_bytes);
         return;
     }
 
     /* ATOMIC：先删旧正式文件，再把临时文件 rename 覆盖；DIRECT：已在目标路径上写完 */
     if (!ctx->direct_write) {
         (void)fs_adapt_delete(ctx->final_path);
         if (fs_adapt_rename(ctx->path, ctx->final_path) != 0) {
             (void)fs_adapt_delete(ctx->path);
             ctx->result = -1;
             ctx->done = true;
             c->is_closing = 1;
             printf("[HttpLfs] rename failed: %s -> %s\r\n", ctx->path, ctx->final_path);
             return;
         }
     }
 
     /* 第二层完整性校验：落盘后的最终文件大小再对比一次 */
     if (ctx->expected_known) {
         if (fs_adapt_stat(ctx->final_path, &final_size) != 0 ||
             (size_t)final_size != ctx->expected_bytes) {
             (void)fs_adapt_delete(ctx->final_path);
             ctx->result = -1;
             ctx->done = true;
             c->is_closing = 1;
             printf("[HttpLfs] final file size verify failed: stat=%u expected=%u\r\n",
                 final_size, (unsigned int)ctx->expected_bytes);
             return;
         }
     }
 
     /* 总长已知且 body 小于 progress_every_bytes 时，stride 回调可能从未触发，收尾补一次进度 */
     if (ctx->progress_fn != NULL && ctx->expected_known) {
         ctx->progress_fn(ctx->progress_arg, ctx->bytes_written, ctx->expected_bytes);
     }
 
     ctx->result = 0;
     ctx->done = true;
     if (ctx->expected_known) {
         printf("[HttpLfs] http done %u/%u bytes\r\n",
             (unsigned int)ctx->bytes_written, (unsigned int)ctx->expected_bytes);
     } else {
         printf("[HttpLfs] http done %u bytes (until-close)\r\n",
             (unsigned int)ctx->bytes_written);
     }
     c->is_closing = 1;
 }
 
 static void fd_strip_http_headers(struct mg_connection *c, struct mg_http_message *hm)
 {
     size_t hdr_len;
     if (hm->body.buf != NULL && hm->head.buf != NULL &&
         (const unsigned char *)hm->body.buf >= c->recv.buf &&
         (const unsigned char *)hm->body.buf <= c->recv.buf + c->recv.len) {
         hdr_len = (size_t)(hm->body.buf - (char *)c->recv.buf);
     } else if (hm->head.buf != NULL && (const unsigned char *)hm->head.buf >= c->recv.buf &&
                (const unsigned char *)hm->head.buf <= c->recv.buf + c->recv.len) {
         hdr_len = (size_t)((hm->head.buf - (char *)c->recv.buf) + hm->head.len);
     } else {
         return;
     }
     if (hdr_len > c->recv.len) {
         return;
     }
     mg_iobuf_del(&c->recv, 0, hdr_len);
 }
 
 static void fd_consume_content_length(struct mg_connection *c, hd_ctx_t *ctx)
 {
     while (ctx->content_remaining > 0U && c->recv.len > 0U) {
         size_t n = c->recv.len;
         if (n > ctx->content_remaining) {
             n = ctx->content_remaining;
         }
         if (hd_write_all(ctx->fd, c->recv.buf, n) != 0) {
             printf("[HttpLfs] lfs write failed (content-length), off=%u n=%u\r\n",
                 (unsigned int)ctx->bytes_written, (unsigned int)n);
             fd_abort(c, ctx, -1);
             return;
         }
         mg_iobuf_del(&c->recv, 0, n);
         ctx->content_remaining -= n;
         ctx->bytes_written += n;
         ctx->last_body_ms = mg_millis();
         hd_maybe_progress(ctx);
     }
     if (ctx->content_remaining == 0U) {
         fd_finish_ok(c, ctx);
     }
 }
 
 static void fd_consume_until_close(struct mg_connection *c, hd_ctx_t *ctx)
 {
     while (c->recv.len > 0U) {
         size_t n = c->recv.len;
         if (hd_write_all(ctx->fd, c->recv.buf, n) != 0) {
             printf("[HttpLfs] lfs write failed (until-close), got=%u n=%u\r\n",
                 (unsigned int)ctx->bytes_written, (unsigned int)n);
             fd_abort(c, ctx, -1);
             return;
         }
         mg_iobuf_del(&c->recv, 0, n);
         ctx->bytes_written += n;
         ctx->last_body_ms = mg_millis();
         hd_maybe_progress(ctx);
     }
 }
 
 static void fd_consume_chunked(struct mg_connection *c, hd_ctx_t *ctx)
 {
     for (;;) {
         if (c->recv.len == 0U) {
             return;
         }
         if (ctx->chunk_payload_left == 0U) {
             char *buf = (char *)c->recv.buf;
             size_t i = 0;
             while (i + 1U < c->recv.len && !(buf[i] == '\r' && buf[i + 1U] == '\n')) {
                 i++;
             }
             if (i + 1U >= c->recv.len) {
                 return;
             }
             unsigned long csz = 0;
             if (fd_parse_chunk_size_line(buf, i, &csz) != 0) {
                 fd_abort(c, ctx, -1);
                 return;
             }
             mg_iobuf_del(&c->recv, 0, i + 2U);
             if (csz == 0UL) {
                 fd_finish_ok(c, ctx);
                 return;
             }
             ctx->chunk_payload_left = (size_t)csz;
             ctx->last_body_ms = mg_millis();
             continue;
         }
 
         size_t n = c->recv.len;
         if (n > ctx->chunk_payload_left) {
             n = ctx->chunk_payload_left;
         }
         if (hd_write_all(ctx->fd, c->recv.buf, n) != 0) {
             printf("[HttpLfs] lfs write failed (chunked), got=%u n=%u\r\n",
                 (unsigned int)ctx->bytes_written, (unsigned int)n);
             fd_abort(c, ctx, -1);
             return;
         }
         mg_iobuf_del(&c->recv, 0, n);
         ctx->chunk_payload_left -= n;
         ctx->bytes_written += n;
         ctx->last_body_ms = mg_millis();
         hd_maybe_progress(ctx);
 
         if (ctx->chunk_payload_left == 0U) {
             if (c->recv.len < 2U) {
                 return;
             }
             if (((char *)c->recv.buf)[0] != '\r' || ((char *)c->recv.buf)[1] != '\n') {
                 fd_abort(c, ctx, -1);
                 return;
             }
             mg_iobuf_del(&c->recv, 0, 2U);
         }
     }
 }
 
 static void fd_dispatch_body(struct mg_connection *c, hd_ctx_t *ctx)
 {
     switch (ctx->mode) {
         case FD_BODY_CONTENT_LENGTH:
             fd_consume_content_length(c, ctx);
             break;
         case FD_BODY_CHUNKED:
             fd_consume_chunked(c, ctx);
             break;
         case FD_BODY_UNTIL_CLOSE:
             fd_consume_until_close(c, ctx);
             break;
         default:
             break;
     }
 }
 
static void hd_ev_handler(struct mg_connection *c, int ev, void *ev_data)
 {
     hd_ctx_t *ctx = (hd_ctx_t *)c->fn_data;
     if (ctx == NULL) {
         return;
     }
 
     if (ev == MG_EV_OPEN) {
         (void)ev_data;
         ctx->connect_deadline_ms = mg_millis() + HD_CONNECT_MS;
         return;
     }
 
     if (ev == MG_EV_POLL) {
         if (!ctx->hdrs_ok) {
             if (mg_millis() > ctx->connect_deadline_ms &&
                 (c->is_connecting || c->is_resolving)) {
                 mg_error(c, "http_lfs connect timeout");
             }
             return;
         }
         if (ctx->done) {
             return;
         }
         if ((mg_millis() - ctx->last_body_ms) > HD_BODY_IDLE_MS) {
             mg_error(c, "http_lfs body idle timeout");
         }
         return;
     }
 
     if (ev == MG_EV_CONNECT) {
         /*
          * mg_url_host() 不含端口；非 80 端口必须在 Host 中带 :port，否则虚拟主机/网关可能直接断连。
          */
         struct mg_str host = mg_url_host(ctx->url_buf);
         unsigned short port = mg_url_port(ctx->url_buf);
         if (port == 80U) {
             if (ctx->try_range && ctx->resume_offset > 0U) {
                 mg_printf(c,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %.*s\r\n"
                           "Connection: close\r\n"
                           "User-Agent: Xiaohong-WS63/1.0\r\n"
                           "Accept: */*\r\n"
                           "Range: bytes=%u-\r\n"
                           "\r\n",
                           mg_url_uri(ctx->url_buf), (int)host.len, host.buf,
                           (unsigned int)ctx->resume_offset);
             } else {
                 mg_printf(c,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %.*s\r\n"
                           "Connection: close\r\n"
                           "User-Agent: Xiaohong-WS63/1.0\r\n"
                           "Accept: */*\r\n"
                           "\r\n",
                           mg_url_uri(ctx->url_buf), (int)host.len, host.buf);
             }
         } else {
             if (ctx->try_range && ctx->resume_offset > 0U) {
                 mg_printf(c,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %.*s:%u\r\n"
                           "Connection: close\r\n"
                           "User-Agent: Xiaohong-WS63/1.0\r\n"
                           "Accept: */*\r\n"
                           "Range: bytes=%u-\r\n"
                           "\r\n",
                           mg_url_uri(ctx->url_buf), (int)host.len, host.buf, (unsigned int)port,
                           (unsigned int)ctx->resume_offset);
             } else {
                 mg_printf(c,
                           "GET %s HTTP/1.1\r\n"
                           "Host: %.*s:%u\r\n"
                           "Connection: close\r\n"
                           "User-Agent: Xiaohong-WS63/1.0\r\n"
                           "Accept: */*\r\n"
                           "\r\n",
                           mg_url_uri(ctx->url_buf), (int)host.len, host.buf, (unsigned int)port);
             }
         }
         return;
     }
 
     if (ev == MG_EV_HTTP_HDRS) {
         struct mg_http_message *hm = (struct mg_http_message *)ev_data;
         int st = mg_http_status(hm);
         if (st == 416 && ctx->try_range) {
             printf("[HttpLfs] range not satisfiable, fallback full download\r\n");
             fd_abort(c, ctx, HTTP_LFS_RES_FALLBACK_FULL);
             return;
         }
         if (st != 200 && st != 206) {
             printf("[HttpLfs] unexpected http status=%d\r\n", st);
             fd_abort(c, ctx, -1);
             return;
         }
 
         struct mg_str *te = mg_http_get_header(hm, "Transfer-Encoding");
         bool chunked = hd_te_is_chunked(te);
         struct mg_str *clh = mg_http_get_header(hm, "Content-Length");
         struct mg_str *crh = mg_http_get_header(hm, "Content-Range");
         size_t cl = 0;
 
         if (clh != NULL && fd_parse_content_length(clh, &cl) == 0) {
             printf("[HttpLfs] http %d, te=%s, cl=%u, range_try=%u off=%u\r\n",
                 st, (te != NULL) ? "yes" : "no", (unsigned int)cl,
                 (unsigned int)(ctx->try_range ? 1U : 0U), (unsigned int)ctx->resume_offset);
         } else {
             printf("[HttpLfs] http %d, te=%s, cl=n/a, range_try=%u off=%u\r\n",
                 st, (te != NULL) ? "yes" : "no",
                 (unsigned int)(ctx->try_range ? 1U : 0U), (unsigned int)ctx->resume_offset);
         }
 
         if (chunked) {
             ctx->mode = FD_BODY_CHUNKED;
         } else if (clh != NULL) {
             if (fd_parse_content_length(clh, &cl) != 0) {
                 fd_abort(c, ctx, -1);
                 return;
             }
             ctx->mode = FD_BODY_CONTENT_LENGTH;
             ctx->content_remaining = cl;
             ctx->expected_known = true;
             ctx->expected_bytes = cl + ((st == 206) ? ctx->resume_offset : 0U);
         } else {
             ctx->mode = FD_BODY_UNTIL_CLOSE;
         }
 
         if (fs_adapt_mkdir(ctx->base_dir) != 0) {
             fd_abort(c, ctx, -1);
             return;
         }
 
         if (st == 206) {
             size_t range_start = 0U;
             if (ctx->resume_offset == 0U || crh == NULL ||
                 fd_parse_content_range_start(crh, &range_start) != 0 ||
                 range_start != ctx->resume_offset) {
                 printf("[HttpLfs] bad content-range, fallback full\r\n");
                 fd_abort(c, ctx, HTTP_LFS_RES_FALLBACK_FULL);
                 return;
             }
             ctx->fd = fs_adapt_open(ctx->path, O_WRONLY | O_CREAT);
         } else {
             ctx->fd = fs_adapt_open(ctx->path, O_WRONLY | O_CREAT | O_TRUNC);
         }
         if (ctx->fd < 0) {
             fd_abort(c, ctx, -1);
             return;
         }
         if (st == 206) {
             if (fs_adapt_seek(ctx->fd, (int)ctx->resume_offset, SEEK_SET) < 0) {
                 fd_abort(c, ctx, -1);
                 return;
             }
             ctx->bytes_written = ctx->resume_offset;
             printf("[HttpLfs] resume from offset=%u\r\n", (unsigned int)ctx->resume_offset);
         } else if (ctx->resume_offset > 0U) {
             printf("[HttpLfs] server ignored range, restart from zero\r\n");
         }
 
         fd_strip_http_headers(c, hm);
         ctx->hdrs_ok = true;
         ctx->last_body_ms = mg_millis();
         /* 首包写入前报告一次已确认长度/续传位置（progress_every_bytes 大时否则长时间无回调） */
         if (ctx->progress_fn != NULL) {
             size_t tot = ctx->expected_known ? ctx->expected_bytes : 0U;
             ctx->progress_fn(ctx->progress_arg, ctx->bytes_written, tot);
         }
         fd_dispatch_body(c, ctx);
         return;
     }
 
     if (ev == MG_EV_READ) {
         if (!ctx->hdrs_ok || ctx->done) {
             return;
         }
         fd_dispatch_body(c, ctx);
         return;
     }
 
     if (ev == MG_EV_CLOSE) {
         if (ctx->done) {
             return;
         }
         if (!ctx->hdrs_ok) {
             ctx->result = -1;
             ctx->done = true;
             return;
         }
         /* 与 font_download 一致：EOF 时 recv 里可能仍有 body 未进 MG_EV_READ */
         if (c->recv.len > 0U) {
             fd_dispatch_body(c, ctx);
         }
         if (ctx->done) {
             return;
         }
         if (ctx->mode == FD_BODY_UNTIL_CLOSE) {
             if (ctx->fd >= 0) {
                 fd_finish_ok(c, ctx);
             } else {
                 ctx->result = -1;
                 ctx->done = true;
             }
             return;
         }
         if (ctx->expected_known) {
             printf("[HttpLfs] http closed early: got %u/%u bytes, remain=%u\r\n",
                 (unsigned int)ctx->bytes_written, (unsigned int)ctx->expected_bytes,
                 (unsigned int)ctx->content_remaining);
         } else {
             printf("[HttpLfs] http closed early in non-until-close mode, got=%u bytes\r\n",
                 (unsigned int)ctx->bytes_written);
         }
         fd_abort(c, ctx, -1);
         return;
     }
 
     if (ev == MG_EV_ERROR) {
         if (ev_data != NULL) {
             printf("[HttpLfs] MG_EV_ERROR: %s\r\n", (const char *)ev_data);
         } else {
             printf("[HttpLfs] MG_EV_ERROR\r\n");
         }
         fd_abort(c, ctx, -1);
     }
 }
 
int http_lfs_download_blocking(const http_lfs_download_param_t *param)
{
    size_t resume_bytes = 0U;
    bool try_range_once = false;
    bool range_disabled_for_session = false;
    const char *part_suffix;
    bool direct;

    if (param == NULL || param->url == NULL || param->base_dir == NULL || param->basename == NULL) {
        return -1;
    }
    direct = (param->store_mode == HTTP_LFS_STORE_DIRECT);
    part_suffix = (param->part_suffix != NULL && param->part_suffix[0] != '\0') ? param->part_suffix : ".part";

retry_connect:
    if (param->url[0] == '\0' || param->base_dir[0] == '\0' || param->basename[0] == '\0') {
        return -1;
    }
    if (strncmp(param->url, "http://", 7) != 0 && strncmp(param->url, "https://", 8) != 0) {
        return -1;
    }
    if (strchr(param->basename, '/') != NULL || strstr(param->basename, "..") != NULL) {
        return -1;
    }
    if (strlen(param->basename) > 48U) {
        return -1;
    }
    if (strstr(param->base_dir, "..") != NULL) {
        return -1;
    }

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    hd_ctx_t *ctx = (hd_ctx_t *)calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        mg_mgr_free(&mgr);
        return -1;
    }
    ctx->fd = -1;
    ctx->result = -1;
    ctx->progress_fn = param->progress_fn;
    ctx->progress_arg = param->progress_arg;
    ctx->progress_every_bytes = param->progress_every_bytes;
    ctx->progress_steps_done = 0U;
    if (snprintf(ctx->url_buf, sizeof(ctx->url_buf), "%s", param->url) >= (int)sizeof(ctx->url_buf)) {
        free(ctx);
        mg_mgr_free(&mgr);
        return -1;
    }
    if (snprintf(ctx->base_dir, sizeof(ctx->base_dir), "%s", param->base_dir) >= (int)sizeof(ctx->base_dir)) {
        free(ctx);
        mg_mgr_free(&mgr);
        return -1;
    }
    if (snprintf(ctx->final_path, sizeof(ctx->final_path), "%s/%s", ctx->base_dir, param->basename) >=
        (int)sizeof(ctx->final_path)) {
        free(ctx);
        mg_mgr_free(&mgr);
        return -1;
    }
    ctx->direct_write = direct;
    if (direct) {
        if (snprintf(ctx->path, sizeof(ctx->path), "%s/%s", ctx->base_dir, param->basename) >= (int)sizeof(ctx->path)) {
            free(ctx);
            mg_mgr_free(&mgr);
            return -1;
        }
    } else {
        if (snprintf(ctx->path, sizeof(ctx->path), "%s/%s%s", ctx->base_dir, param->basename, part_suffix) >=
            (int)sizeof(ctx->path)) {
            free(ctx);
            mg_mgr_free(&mgr);
            return -1;
        }
    }

    /*
     * Range 续传：若存在临时文件，则先尝试从其大小处续传。
     * 416 等情况会回退全量（HTTP_LFS_RES_FALLBACK_FULL）。
     */
    {
        unsigned int sz = 0U;
        if (!range_disabled_for_session && try_range_once == false &&
            fs_adapt_stat(ctx->path, &sz) == 0 && sz > 0U) {
            resume_bytes = (size_t)sz;
            try_range_once = true;
        }
    }
    ctx->try_range = try_range_once;
    ctx->resume_offset = try_range_once ? resume_bytes : 0U;
    if (!ctx->try_range) {
        (void)fs_adapt_delete(ctx->path);
    }

    FD_MG_LOCK();
    struct mg_connection *c = mg_http_connect(&mgr, ctx->url_buf, hd_ev_handler, ctx);
    if (c != NULL) {
        /* https 时按内嵌 CA 严格校验；必须在 poll 前挂上，否则 mongoose 会静默降级为明文 */
        (void)xh_tls_apply(c, ctx->url_buf);
    }
    FD_MG_UNLOCK();
    if (c == NULL) {
        free(ctx);
        mg_mgr_free(&mgr);
        return -1;
    }

    uint64_t overall_deadline = mg_millis() + HD_OVERALL_MS;
    while (!ctx->done && mg_millis() < overall_deadline) {
        FD_MG_LOCK();
        mg_mgr_poll(&mgr, HD_MGR_POLL_MS);
        FD_MG_UNLOCK();
        osal_msleep(1U);
    }

    if (!ctx->done) {
        FD_MG_LOCK();
        mg_error(c, "http_lfs overall timeout");
        mg_mgr_poll(&mgr, HD_MGR_POLL_MS);
        FD_MG_UNLOCK();
        while (!ctx->done && mg_millis() < overall_deadline + 2000UL) {
            FD_MG_LOCK();
            mg_mgr_poll(&mgr, HD_MGR_POLL_MS);
            FD_MG_UNLOCK();
            osal_msleep(1U);
        }
    }

    int res = ctx->result;
    FD_MG_LOCK();
    mg_mgr_free(&mgr);
    FD_MG_UNLOCK();
    free(ctx);
    if (res == HTTP_LFS_RES_FALLBACK_FULL) {
        range_disabled_for_session = true;
        try_range_once = false;
        resume_bytes = 0U;
        goto retry_connect;
    }
    return res;
}
