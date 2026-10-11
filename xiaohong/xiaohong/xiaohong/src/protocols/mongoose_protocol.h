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
 * @file mongoose_protocol.h
 * @brief 基于 Mongoose 的 WebSocket 协议实现
 *
 * - WebSocket 连接建立（含 Authorization / Protocol-Version / Device-Id / Client-Id 请求头）
 * - hello 握手与 server hello 解析
 * - 音频二进制帧收发
 */

#ifndef __MONGOOSE_PROTOCOL_H__
#define __MONGOOSE_PROTOCOL_H__

#include "mongoose.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WebSocket 配置 */
#define MG_WS_URL_MAX       256
#define MG_WS_TOKEN_MAX     256
#define MG_WS_DEVID_MAX     64
#define MG_WS_CLIENTID_MAX  64

/* Mongoose 协议上下文——承载一次 WebSocket 会话的全部状态 */
typedef struct {
    struct mg_mgr mgr;
    struct mg_connection *conn;
    protocol_t *proto;              /* 反向引用协议基础结构 */

    /* 配置 */
    char url[MG_WS_URL_MAX];
    char token[MG_WS_TOKEN_MAX];
    char device_id[MG_WS_DEVID_MAX];
    char client_id[MG_WS_CLIENTID_MAX];
    int  version;                   /* 协议版本（1/2/3） */

    /* 运行时状态 */
    volatile bool ws_connected;
    volatile bool server_hello_received;
    volatile bool audio_channel_opened;
    /* 对端断开/错误时置位，供 open_audio_channel 内等待循环立即退出，避免长时间阻塞 */
    volatile bool handshake_aborted;
} mg_ctx_t;

/**
 * 创建基于 Mongoose 的协议实例。
 * 调用方通过 mg_ws_set_config() 设置连接参数后，
 * 再调用 protocol ops 的 open_audio_channel 发起连接。
 */
protocol_t *mongoose_protocol_create(void);

/**
 * 配置 WebSocket 连接参数（在 open_audio_channel 之前调用）。
 * @param proto   协议实例
 * @param url     WebSocket 服务器地址，如 "wss://api.example.com/xiaohong/v1/"
 * @param token   授权令牌（若不含空格，自动加 "Bearer " 前缀）
 * @param device_id 设备唯一标识（MAC 地址等）
 * @param client_id 客户端 UUID
 * @param version 协议版本号（默认 1）
 */
void mg_ws_set_config(protocol_t *proto,
                      const char *url,
                      const char *token,
                      const char *device_id,
                      const char *client_id,
                      int version);

/**
 * 全局串行化所有 Mongoose API 与 lwIP BSD 套接字路径（多 mg_mgr 实例也必须互斥）。
 * OTA HTTP、mg_poll 线程、open_audio_channel 等必须成对使用。
 */
void mongoose_api_lock(void);
void mongoose_api_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __MONGOOSE_PROTOCOL_H__ */
