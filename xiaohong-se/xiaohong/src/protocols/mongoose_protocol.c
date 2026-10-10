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
 * @file mongoose_protocol.c
 * @brief 基于 Mongoose 的 WebSocket 协议实现
 *
 * 纯 C 实现：
 * 1. 根据配置获取 WebSocket URL
 * 2. 通过 mg_ws_connect 的 fmt 参数注入 Authorization / Protocol-Version /
 *    Device-Id / Client-Id 请求头
 * 3. 建立 WebSocket 连接
 * 4. 发送 hello 握手，等待 server hello
 * 5. 处理 JSON 消息与二进制音频帧
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include "cmsis_os2.h"
#include "soc_osal.h"
#include "protocol.h"
#include "mongoose_config.h"
#include "mongoose.h"
#include "mongoose_protocol.h"
#include "task_entry.h"

#define TAG "[MG____WS]"

/*
 * CI1302 经 UART 上行为 PCM（见 ci1302_data_process 0x0105），不是 Opus。
 * 若 hello 仍声明 "opus" 而负载为 PCM，服务端会按 Opus 解码 → corrupted stream / Silero 报错。
 * 默认声明 "pcm"；若日后在设备侧做 Opus 编码再上报，编译加 -DMG_WS_UPLINK_AUDIO_FORMAT_STR=opus
 */
#ifndef MG_WS_UPLINK_AUDIO_FORMAT_STR
#define MG_WS_UPLINK_AUDIO_FORMAT_STR "opus"
#endif

/*
 * 建连前缓冲等待（ms）：用于规避 OTA/并发创建后 lwIP 短时 ENOBUFS。
 * 过大将明显拉长“唤醒词->可对话”的首包时延。
 */
#ifndef MG_WS_CONNECT_PRE_DELAY_MS
#define MG_WS_CONNECT_PRE_DELAY_MS 120U
#endif

/* Mongoose 回调跑在 mg_poll 线程；与 AgntTask 并发 printf 易拖垮 newlib 堆。调试可改为 printf */
#ifndef MG_WS_USE_PRINTF
#define MG_WS_USE_PRINTF 0
#endif
#if MG_WS_USE_PRINTF
#define MG_WS_LOG(...) printf(__VA_ARGS__)
#else
#define MG_WS_LOG(...) ((void)0)
#endif

/**
 * 单调时钟纳秒（优先 clock_gettime MONOTONIC，精度取决于平台 hrtimer；失败则回退 RTOS tick）。
 * 用于日志比 osKernelGetTick 换算的毫秒更细。
 */
uint64_t mg_mono_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    }
    {
        uint32_t f = osKernelGetTickFreq();
        if (f == 0U) {
            f = 1U;
        }
        return (uint64_t)osKernelGetTickCount() * 1000000000ULL / (uint64_t)f;
    }
}

/* mg_ws_connect 仅支持 ws:// / wss://；误填 http(s) 会导致握手异常甚至崩溃 */
static void mg_normalize_ws_url(char *url, size_t cap)
{
    char tmp[WS_URL_MAX];
    int n;

    if (url == NULL || cap == 0 || url[0] == '\0') {
        return;
    }
    if (strncmp(url, "https://", 8) == 0) {
        n = snprintf(tmp, sizeof(tmp), "wss://%s", url + 8);
        if (n > 0 && (size_t)n < sizeof(tmp)) {
            snprintf(url, cap, "%s", tmp);
        }
    } else if (strncmp(url, "http://", 7) == 0) {
        n = snprintf(tmp, sizeof(tmp), "ws://%s", url + 7);
        if (n > 0 && (size_t)n < sizeof(tmp)) {
            snprintf(url, cap, "%s", tmp);
        }
    }
}

/* ──────────────────── 前向声明 ──────────────────── */
static bool mg_start(void *impl);
static bool mg_send_text(void *impl, const char *text);
static bool mg_send_audio(void *impl, protocol_audio_packet_t *packet);
static bool mg_open_audio_channel(void *impl);
static void mg_close_audio_channel(void *impl);
static bool mg_is_audio_channel_opened(const void *impl);

static const protocol_ops_t mg_ops = {
    .start                = mg_start,
    .send_text            = mg_send_text,
    .send_audio           = mg_send_audio,
    .open_audio_channel   = mg_open_audio_channel,
    .close_audio_channel  = mg_close_audio_channel,
    .is_audio_channel_opened = mg_is_audio_channel_opened,
};

/* ──────────────────── 全局单例 ──────────────────── */
static mg_ctx_t   *g_ctx  = NULL;
static protocol_t *g_proto = NULL;
static osThreadId_t g_poll_tid = NULL;

/* Mongoose 非线程安全：mg_mgr_poll 与其它 mg_* 必须互斥（OTA/mg_poll/Agent 共用 lwIP） */
static osMutexId_t g_mg_api_mutex = NULL;
/* 避免运行时堆分配失败：为 Mongoose API 互斥锁提供静态控制块内存。 */
static uint64_t s_mg_api_mu_cb[20];
static const osMutexAttr_t s_mg_api_mu_attr = {
    .name = "mg_api_mu",
    .attr_bits = 0U,
    .cb_mem = s_mg_api_mu_cb,
    .cb_size = sizeof(s_mg_api_mu_cb),
};

void mongoose_api_lock(void)
{
    if (g_mg_api_mutex == NULL) {
        g_mg_api_mutex = osMutexNew(&s_mg_api_mu_attr);
        if (g_mg_api_mutex == NULL) {
            printf("%s mongoose_api_lock: osMutexNew failed\r\n", TAG);
        }
    }
    if (g_mg_api_mutex != NULL) {
        (void)osMutexAcquire(g_mg_api_mutex, osWaitForever);
    }
}

void mongoose_api_unlock(void)
{
    if (g_mg_api_mutex != NULL) {
        (void)osMutexRelease(g_mg_api_mutex);
    }
}

static void mg_api_lock(void)
{
    mongoose_api_lock();
}

static void mg_api_unlock(void)
{
    mongoose_api_unlock();
}

/* ──────────────────── Mongoose poll 线程 ──────────────────── */
static void mg_poll_thread(void *arg)
{
    mg_ctx_t *ctx = (mg_ctx_t *)arg;
    while (true) {
        mg_api_lock();
        mg_mgr_poll(&ctx->mgr, 100);
        mg_api_unlock();
        /*
         * 部分 lwIP 移植上 mg_iotest/mg_mgr_poll 会几乎立即返回，while(1) 无休眠会占满 CPU
         *（任务统计里 mg_poll CPUP≈100%），易触发 NMI/看门狗或栈异常。此处强制让出时间片。
         */
        osal_msleep(1U);
    }
}

static bool mg_ensure_poll_thread(mg_ctx_t *ctx)
{
    if (g_poll_tid != NULL) {
        return true;
    }
    /* stack_size: JSON/cJSON/WS + Agent 回调里 Opus 解码，15KiB 会栈溢出；32KiB 留余量 */
    /*   priority: 与 AgntTask 同档即可；过高优先级会加重与 tcpip 的耦合，且 poll 循环需配合 osal_msleep 防空转 */
    task_config_t task_conf = {
        "mg_poll",       // task name
        1024 * 24,       // stack_size
        24,              // priority
        (void*)mg_poll_thread  // func
    };

    g_poll_tid = (osThreadId_t) create_task(&task_conf, (void*)ctx);
    if (g_poll_tid == 0) {
        printf("%s mg_poll create_task NG, stack_size[%d]\r\n", TAG, task_conf.stack_size);
    }
    return (g_poll_tid != 0);
}

/* ──────────────────── hello 消息构造 ──────────────────── */
static int mg_build_hello(char *buf, size_t bufsize, int version)
{
    return snprintf(buf, bufsize,
        "{\"type\":\"hello\","
         "\"version\":%d,"
         "\"features\":{\"mcp\":true},"
         "\"transport\":\"websocket\","
         "\"audio_params\":{"
           "\"format\":\"%s\","
           "\"sample_rate\":16000,"
           "\"channels\":1,"
           "\"frame_duration\":40"
         "}"
        "}", version, MG_WS_UPLINK_AUDIO_FORMAT_STR);
}

/* ──────────────────── server hello 解析 ──────────────────── */
static void mg_parse_server_hello(mg_ctx_t *ctx, const cJSON *root)
{
    const cJSON *transport = cJSON_GetObjectItem(root, "transport");
    if (transport != NULL &&
        (!cJSON_IsString(transport) || strcmp(transport->valuestring, "websocket") != 0)) {
        printf("%s server hello: unsupported transport\r\n", TAG);
        return;
    }

    const cJSON *sid = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(sid) && ctx->proto) {
        snprintf(ctx->proto->session_id, sizeof(ctx->proto->session_id),
                 "%s", sid->valuestring);
        MG_WS_LOG("%s session_id: %s\r\n", TAG, ctx->proto->session_id);
    }

    const cJSON *ap = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(ap) && ctx->proto) {
        const cJSON *sr = cJSON_GetObjectItem(ap, "sample_rate");
        if (cJSON_IsNumber(sr)) ctx->proto->server_sample_rate = sr->valueint;
        const cJSON *fd = cJSON_GetObjectItem(ap, "frame_duration");
        if (cJSON_IsNumber(fd)) ctx->proto->server_frame_duration = fd->valueint;
    }

    ctx->server_hello_received = true;
    MG_WS_LOG("%s server hello received\r\n", TAG);
}

/* ──────────────────── 收到 JSON 消息 ──────────────────── */
static void mg_on_ws_text(mg_ctx_t *ctx, const char *data, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        MG_WS_LOG("%s JSON parse failed\r\n", TAG);
        printf("%s JSON parse failed (len=%u)\r\n", TAG, (unsigned)len);
        return;
    }

    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "hello") == 0) {
        mg_parse_server_hello(ctx, root);
    } else {
        if (ctx->proto && ctx->proto->on_incoming_json) {
            ctx->proto->on_incoming_json(ctx->proto->user_data, root);
        }
    }
    protocol_update_last_incoming(ctx->proto);
    cJSON_Delete(root);
}

/* ──────────────────── 收到二进制音频帧 ──────────────────── */
static void mg_on_ws_binary(mg_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if (ctx == NULL || ctx->proto == NULL || ctx->proto->on_incoming_audio == NULL) {
        return;
    }
    if (data == NULL || len == 0U) {
        return;
    }

    protocol_audio_packet_t pkt = {0};
    pkt.sample_rate = (uint32_t)(ctx->proto->server_sample_rate > 0 ? ctx->proto->server_sample_rate : 16000);

    if (ctx->version == 2) {
        /* 线格式：16 字节头 + payload（与 mg_send_audio / BinaryProtocol2 一致） */
        const size_t hdr = 16U;
        if (len >= hdr) {
            uint32_t psz = mg_ntohl(*(uint32_t *)&data[12]);
            if (psz > 0U && len >= hdr + (size_t)psz) {
                pkt.timestamp = mg_ntohl(*(uint32_t *)&data[8]);
                pkt.payload = (uint8_t *)(data + hdr);
                pkt.frame_duration = psz;
            } else {
                /* 头长与 payload_size 不一致时按裸 Opus 处理，避免静默丢包 */
                pkt.payload = (uint8_t *)data;
                pkt.frame_duration = (uint32_t)len;
                pkt.timestamp = 0;
            }
        } else {
            pkt.payload = (uint8_t *)data;
            pkt.frame_duration = (uint32_t)len;
            pkt.timestamp = 0;
        }
    } else if (ctx->version == 3) {
        const size_t hdr = 4U;
        if (len >= hdr) {
            uint16_t psz = mg_ntohs(*(uint16_t *)&data[2]);
            if (psz > 0U && len >= hdr + (size_t)psz) {
                pkt.timestamp = 0;
                pkt.payload = (uint8_t *)(data + hdr);
                pkt.frame_duration = (uint32_t)psz;
            } else {
                pkt.payload = (uint8_t *)data;
                pkt.frame_duration = (uint32_t)len;
                pkt.timestamp = 0;
            }
        } else {
            pkt.payload = (uint8_t *)data;
            pkt.frame_duration = (uint32_t)len;
            pkt.timestamp = 0;
        }
    } else {
        /* v1：整帧为裸 Opus */
        pkt.payload = (uint8_t *)data;
        pkt.frame_duration = (uint32_t)len;
        pkt.timestamp = 0;
    }

    ctx->proto->on_incoming_audio(ctx->proto->user_data, &pkt);
    protocol_update_last_incoming(ctx->proto);
}

/* ──────────────────── Mongoose 事件处理器 ──────────────────── */
static void mg_ws_event_handler(struct mg_connection *c, int ev, void *ev_data)
{
    mg_ctx_t *ctx = (mg_ctx_t *)(c ? c->fn_data : NULL);
    if (ctx == NULL) {
        return;
    }

    if (ev == MG_EV_ERROR) {
        ctx->handshake_aborted = true;
        if (ctx->proto) {
            protocol_set_error(ctx->proto,
                ev_data != NULL ? (char *)ev_data : (char *)"WebSocket error");
        }
        /* 不在此调用 on_audio_channel_closed，避免与随后 MG_EV_CLOSE 重复；
         * 且握手未成功时不应向 Agent 发关闭事件（见 MG_EV_CLOSE 分支）。 */
    } else if (ev == MG_EV_WS_OPEN) {
        MG_WS_LOG("%s WebSocket connected\r\n", TAG);
        ctx->ws_connected = true;
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        if (wm->flags & WEBSOCKET_OP_BINARY) {
            mg_on_ws_binary(ctx, (const uint8_t *)wm->data.buf, wm->data.len);
        } else {
            mg_on_ws_text(ctx, wm->data.buf, wm->data.len);
        }
    } else if (ev == MG_EV_CLOSE) {
        ctx->handshake_aborted = true;
        /* 仅在「会话已建立」时通知 Agent：避免握手失败路径上从 mg_poll 投递事件，
         * 与 AgntTask 内阻塞的 open_audio_channel 交叉，降低异常/堆损坏风险。 */
        const bool session_was_up = ctx->audio_channel_opened || ctx->ws_connected;
        ctx->ws_connected = false;
        ctx->audio_channel_opened = false;
        ctx->conn = NULL;
        if (session_was_up && ctx->proto && ctx->proto->on_audio_channel_closed) {
            ctx->proto->on_audio_channel_closed(ctx->proto->user_data);
        }
    }
}

/* ──────────────────── protocol ops 实现 ──────────────────── */

/**
 * mg_start: 仅初始化 mg_mgr 并启动 poll 线程，不建立连接。
 * 实际的 WebSocket 连接在 mg_open_audio_channel 中发起。
 */
static bool mg_start(void *impl)
{
    mg_ctx_t *ctx = (mg_ctx_t *)impl;
    if (ctx == NULL) return false;
    return mg_ensure_poll_thread(ctx);
}

/**
 * mg_open_audio_channel: 对标 WebsocketProtocol::OpenAudioChannel()
 *
 * 1. 通过 mg_ws_connect 的 fmt 注入四个 HTTP 请求头
 * 2. 等待 MG_EV_WS_OPEN（WebSocket 握手完成）
 * 3. 发送 hello JSON
 * 4. 等待 server hello
 */
static bool mg_open_audio_channel(void *impl)
{
    mg_ctx_t *ctx = (mg_ctx_t *)impl;
    if (ctx == NULL || ctx->url[0] == '\0') {
        MG_WS_LOG("%s open_audio_channel: no URL configured\r\n", TAG);
        return false;
    }

    /* 重置状态 */
    ctx->ws_connected = false;
    ctx->server_hello_received = false;
    ctx->audio_channel_opened = false;
    ctx->handshake_aborted = false;
    if (ctx->proto) ctx->proto->error_occurred = false;

    /* 构造 Authorization 头值 */
    char auth_value[WS_TOKEN_MAX + 8];
    auth_value[0] = '\0';
    if (ctx->token[0] != '\0') {
        if (strchr(ctx->token, ' ') == NULL) {
            snprintf(auth_value, sizeof(auth_value), "Bearer %s", ctx->token);
        } else {
            snprintf(auth_value, sizeof(auth_value), "%s", ctx->token);
        }
    }

    /*
     * mg_ws_connect 的最后一个变参是附加的 HTTP 请求头（printf 风格）。
     * 每个头用 \r\n 结尾，Mongoose 会将它们拼接到 WebSocket 升级请求中。
     */
    MG_WS_LOG("%s connecting to %s (version=%d)\r\n", TAG, ctx->url, ctx->version);

    /*
     * OTA 刚结束或任务扎堆创建时立刻 socket()，lwIP 上常见 errno=105(ENOBUFS)。
     * 保留短等待以稳住建连，同时避免固定 1s 额外时延。
     */
    osal_msleep(MG_WS_CONNECT_PRE_DELAY_MS);

    mg_api_lock();
    if (auth_value[0] != '\0') {
        ctx->conn = mg_ws_connect(&ctx->mgr, ctx->url,
            mg_ws_event_handler, ctx,
            "Authorization: %s\r\n"
            "Protocol-Version: %d\r\n"
            "Device-Id: %s\r\n"
            "Client-Id: %s\r\n",
            auth_value, ctx->version, ctx->device_id, ctx->client_id);
    } else {
        ctx->conn = mg_ws_connect(&ctx->mgr, ctx->url,
            mg_ws_event_handler, ctx,
            "Protocol-Version: %d\r\n"
            "Device-Id: %s\r\n"
            "Client-Id: %s\r\n",
            ctx->version, ctx->device_id, ctx->client_id);
    }
    mg_api_unlock();

    if (ctx->conn == NULL) {
        MG_WS_LOG("%s mg_ws_connect failed\r\n", TAG);
        printf("%s open_audio_channel: mg_ws_connect failed\r\n", TAG);
        protocol_set_error(ctx->proto, "WebSocket connect failed");
        return false;
    }

    /* recv 侧 n=-2 为 MG_IO_WAIT（非阻塞暂无数据），Mongoose 调试里 err=11 多为 EAGAIN，均属正常 */
    printf("%s WS upgrade sent, waiting for HTTP 101 (read_conn n=-2 = no data yet, OK)\r\n", TAG);

    /* 等待 WebSocket 握手完成（最多 10 秒）；缩小轮询粒度，降低额外等待抖动。 */
    for (int i = 0; i < 500 && !ctx->ws_connected && !ctx->handshake_aborted; i++) {
        osDelay(20);  /* 20ms */
    }
    if (!ctx->ws_connected) {
        /* 让 mg_poll 线程跑完 mg_close_conn，避免与下面 AgntTask 的 printf/堆操作紧耦合 */
        osDelay(30);
        if (ctx->handshake_aborted) {
            MG_WS_LOG("%s WebSocket handshake aborted (peer closed or error)\r\n", TAG);
            printf("%s handshake aborted (peer closed or error)\r\n", TAG);
            protocol_set_error(ctx->proto, "WebSocket handshake aborted");
        } else {
            MG_WS_LOG("%s WebSocket handshake timeout\r\n", TAG);
            printf("%s handshake timeout (no MG_EV_WS_OPEN)\r\n", TAG);
            protocol_set_error(ctx->proto, "WebSocket handshake timeout");
        }
        return false;
    }

    /* 已进入 WS 已连接阶段，清标志；第二段等待仅依赖本轮内的断开事件 */
    ctx->handshake_aborted = false;

    /* 发送 hello 消息 */
    char hello[768];
    mg_build_hello(hello, sizeof(hello), ctx->version);
    printf("%s WebSocket UP, sending client hello (version=%d)\r\n", TAG, ctx->version);
    if (!mg_send_text(impl, hello)) {
        printf("%s send client hello failed\r\n", TAG);
        return false;
    }

    /* 等待 server hello（最多 10 秒）；缩小轮询粒度，尽快进入可对话状态。 */
    for (int i = 0; i < 500 && !ctx->server_hello_received && !ctx->handshake_aborted; i++) {
        osDelay(20);
    }
    if (!ctx->server_hello_received) {
        osDelay(30);
        if (ctx->handshake_aborted) {
            MG_WS_LOG("%s server hello aborted (connection closed)\r\n", TAG);
            printf("%s server hello aborted (connection closed)\r\n", TAG);
            protocol_set_error(ctx->proto, "Server hello aborted");
        } else {
            MG_WS_LOG("%s server hello timeout\r\n", TAG);
            printf("%s server hello timeout (no type=hello in time)\r\n", TAG);
            protocol_set_error(ctx->proto, "Server hello timeout");
        }
        return false;
    }

    ctx->audio_channel_opened = true;
    if (ctx->proto && ctx->proto->on_audio_channel_opened) {
        ctx->proto->on_audio_channel_opened(ctx->proto->user_data);
    }

    MG_WS_LOG("%s audio channel opened\r\n", TAG);
    return true;
}

static void mg_close_audio_channel(void *impl)
{
    mg_ctx_t *ctx = (mg_ctx_t *)impl;
    if (ctx == NULL) return;

    mg_api_lock();
    if (ctx->conn != NULL) {
        ctx->conn->is_closing = 1;  /* Mongoose 将在下轮 poll 时关闭连接 */
        ctx->conn = NULL;
    }
    mg_api_unlock();
    ctx->ws_connected = false;
    ctx->server_hello_received = false;
    ctx->audio_channel_opened = false;
}

static bool mg_is_audio_channel_opened(const void *impl)
{
    const mg_ctx_t *ctx = (const mg_ctx_t *)impl;
    if (ctx == NULL) return false;
    return ctx->audio_channel_opened && ctx->ws_connected;
}

static bool mg_send_text(void *impl, const char *text)
{
    mg_ctx_t *ctx = (mg_ctx_t *)impl;
    if (ctx == NULL || ctx->conn == NULL || !ctx->ws_connected || text == NULL) {
        MG_WS_LOG("%s send_text: not connected\r\n", TAG);
        return false;
    }
    size_t len = strlen(text);
    mg_api_lock();
    mg_ws_send(ctx->conn, text, len, WEBSOCKET_OP_TEXT);
    mg_api_unlock();
    return true;
}

static bool mg_send_audio(void *impl, protocol_audio_packet_t *packet)
{
    mg_ctx_t *ctx = (mg_ctx_t *)impl;
    if (ctx == NULL || ctx->conn == NULL || !ctx->ws_connected || packet == NULL) {
        return false;
    }

    if (ctx->version == 2) {
        uint32_t payload_size = packet->frame_duration; /* 由调用方约定的有效载荷长度 */
        size_t total = sizeof(protocol_binary2_t) + payload_size;
        uint8_t *buf = (uint8_t *)calloc(1, total);
        if (buf == NULL) return false;

        protocol_binary2_t *bp = (protocol_binary2_t *)buf;
        bp->version      = htons((uint16_t)ctx->version);
        bp->type         = 0;
        bp->reserved     = 0;
        bp->timestamp    = htonl(packet->timestamp);
        bp->payload_size = htonl(payload_size);
        memcpy(bp->payload, packet->payload, payload_size);

        mg_api_lock();
        mg_ws_send(ctx->conn, (const char *)buf, total, WEBSOCKET_OP_BINARY);
        mg_api_unlock();
        free(buf);
    } else if (ctx->version == 3) {
        uint16_t payload_size = (uint16_t)packet->frame_duration;
        size_t total = sizeof(protocol_binary3_t) + payload_size;
        uint8_t *buf = (uint8_t *)calloc(1, total);
        if (buf == NULL) return false;

        protocol_binary3_t *bp = (protocol_binary3_t *)buf;
        bp->type         = 0;
        bp->reserved     = 0;
        bp->payload_size = htons(payload_size);
        memcpy(bp->payload, packet->payload, payload_size);

        mg_api_lock();
        mg_ws_send(ctx->conn, (const char *)buf, total, WEBSOCKET_OP_BINARY);
        mg_api_unlock();
        free(buf);
    } else {
        /* version 1: 裸 opus 帧（拷贝后再发，避免 staging 被下一帧覆盖） */
        uint32_t nbytes = (uint32_t)packet->frame_duration;
        if (nbytes == 0U || packet->payload == NULL) {
            return false;
        }
        uint8_t scratch[256];
        if (nbytes > sizeof(scratch)) {
            nbytes = (uint32_t)sizeof(scratch);
        }
        memcpy(scratch, packet->payload, nbytes);
        mg_api_lock();
        mg_ws_send(ctx->conn, (const char *)scratch, nbytes, WEBSOCKET_OP_BINARY);
        mg_api_unlock();
    }

    return true;
}

/* ──────────────────── 公共 API ──────────────────── */

protocol_t *mongoose_protocol_create(void)
{
    if (g_ctx == NULL) {
        g_ctx = (mg_ctx_t *)calloc(1, sizeof(mg_ctx_t));
    }
    if (g_proto == NULL) {
        g_proto = (protocol_t *)calloc(1, sizeof(protocol_t));
    }
    if (g_ctx == NULL || g_proto == NULL) {
        return NULL;
    }

    mongoose_api_lock();
    mg_mgr_init(&g_ctx->mgr);
    /* mongoose.c 默认 mg_log_level=DEBUG，read_conn/write_conn 的 MG_DEBUG 与 DataPars/Lvgl 等并发 printf 会串行乱码 */
    mg_log_level = MG_LL_INFO;
    mongoose_api_unlock();
    g_ctx->version = 1;            /* 默认协议版本 */
    g_ctx->proto   = g_proto;

    g_proto->ops  = &mg_ops;
    g_proto->impl = (void *)g_ctx;

    printf("[I][Protocol] %s OK, return g_proto\r\n", __func__);
    return g_proto;
}

void mongoose_protocol_destroy(void)
{
    if (g_ctx != NULL) {
        free(g_ctx);
        g_ctx = NULL;
    }
    if (g_proto != NULL) {
        free(g_proto);
        g_proto = NULL;
    }
    printf("[I][Protocol] %s OK\r\n", __func__);
}

void mg_ws_set_config(protocol_t *proto,
                      const char *url,
                      const char *token,
                      const char *device_id,
                      const char *client_id,
                      int version)
{
    if (proto == NULL || proto->impl == NULL) return;
    mg_ctx_t *ctx = (mg_ctx_t *)proto->impl;

    if (url) {
        snprintf(ctx->url, sizeof(ctx->url), "%s", url);
        mg_normalize_ws_url(ctx->url, sizeof(ctx->url));
    }
    if (token)     snprintf(ctx->token,     sizeof(ctx->token),     "%s", token);
    if (device_id) snprintf(ctx->device_id, sizeof(ctx->device_id), "%s", device_id);
    if (client_id) snprintf(ctx->client_id, sizeof(ctx->client_id), "%s", client_id);
    if (version > 0) ctx->version = version;
}
