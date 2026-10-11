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
 * @file xh_tls.h
 * @brief mongoose TLS 接线：内嵌 CA 根证书 + 按 URL 决定是否启用 TLS。
 *
 * 工程以 MG_TLS=MG_TLS_BUILTIN(3) 编译（mongoose 自带 TLS1.3 客户端），
 * 故此处不再依赖 mbedtls。所有用 mg_http_connect()/mg_ws_connect() 建连的地方，
 * 建连后立即调用 xh_tls_apply()：URL 为 https/wss 时按“严格校验”初始化 TLS，
 * 否则不动（保持明文 http/ws）。
 */

#ifndef XIAOHONG_XH_TLS_H
#define XIAOHONG_XH_TLS_H

#include <stdbool.h>

#include "mongoose.h"

/**
 * 内嵌 CA 根证书（PEM）。用于严格校验服务端证书链。
 * @return 以 NUL 结尾的 PEM 文本；始终非 NULL
 */
const char *xh_tls_ca_pem(void);

/**
 * 若 url 为 TLS 系（https/wss/mqtts/ssl/tls/tcps）则初始化 TLS：
 *   - 校验链（内嵌 CA）+ 主机名校验（SAN/CN）
 *   - 必须紧跟 mg_http_connect()/mg_ws_connect() 之后、建连事件之前调用
 *     （mongoose 的 TLS 需在 CONNECT 事件前挂上，否则会被判定为“未调用
 *      mg_tls_init”而静默降级为明文）
 *
 * @param c   连接（可为 NULL，此时直接返回）
 * @param url 建连时的原始 URL（可为 NULL）
 * @return true 表示已按 TLS 初始化；false 表示非 TLS URL 或参数无效（未改动连接）
 */
bool xh_tls_apply(struct mg_connection *c, const char *url);

#endif /* XIAOHONG_XH_TLS_H */
