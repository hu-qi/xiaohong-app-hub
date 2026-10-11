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
 * @file protocol.h
 * @brief C 语言协议层公共接口
 *
 * 将 protocols 目录下的 C++ Protocol 类重构为 C 实现。
 * 使用函数指针实现多态和回调，通过依赖注入与外部系统集成。
 */

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cJSON.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 协议类型 */
typedef enum {
    PROTOCOL_TYPE_MONGOOSE,
    PROTOCOL_TYPE_WEBSOCKET,
    PROTOCOL_TYPE_MQTT,
} protocol_type_t;

/* 协议状态 */
typedef enum {
    PROTOCOL_STATE_DISCONNECTED,
    PROTOCOL_STATE_INIT_CONNECT,
    PROTOCOL_STATE_HANDSHAKE,
    PROTOCOL_STATE_CONNECTED,
    PROTOCOL_STATE_INVALID,
} protocol_state_t;

/* 中止原因 */
typedef enum {
    PROTOCOL_ABORT_REASON_NONE,
    PROTOCOL_ABORT_REASON_WAKE_WORD_DETECTED
} protocol_abort_reason_t;

/* 监听模式 */
typedef enum {
    PROTOCOL_LISTENING_MODE_AUTO_STOP,
    PROTOCOL_LISTENING_MODE_MANUAL_STOP,
    PROTOCOL_LISTENING_MODE_REALTIME
} protocol_listening_mode_t;

/* 音频流数据包 */
typedef struct {
    uint32_t sample_rate;
    uint32_t frame_duration;
    uint32_t timestamp;
//  uint32_t payload_size;    // ? 待确认
    uint8_t* payload;
} protocol_audio_packet_t;

/* Binary 协议结构（与 protocol.h 一致） */
typedef struct {
    uint16_t version;
    uint16_t type;          // Message type (0: OPUS, 1: JSON, 2: speex...)
    uint32_t reserved;      // Reserved for future use
    uint32_t timestamp;     // Timestamp in milliseconds (used for server-side AEC)
    uint32_t payload_size;  // Payload size in bytes
    /* 柔性数组：负载紧跟在帧头之后。不可写成指针，否则 sizeof 多算 4 字节，
     * 且 bp->payload 会被当成指针读取（calloc 后为 NULL）导致 memcpy 写地址 0 崩溃。 */
    uint8_t payload[];      // Payload data
} __attribute__((packed)) protocol_binary2_t;

typedef struct {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];      // Payload data（同上，必须为柔性数组）
} __attribute__((packed)) protocol_binary3_t;

/* 回调函数类型 */
typedef void (*protocol_void_cb_t)(void* user_data);
typedef void (*protocol_json_cb_t)(void* user_data, const cJSON* root);
typedef void (*protocol_audio_cb_t)(void* user_data, protocol_audio_packet_t* packet);
typedef void (*protocol_error_cb_t)(void* user_data, const char* message);

/* 协议操作接口（vtable） */
typedef struct protocol_ops_s protocol_ops_t;
struct protocol_ops_s {
    bool (*start)(void* proto);    // 启动WebSocket或MQTT连接
    bool (*send_text)(void* proto, const char* text);    // 发送文本消息
    bool (*send_audio)(void* proto, protocol_audio_packet_t* packet);    // 发送音频数据
    bool (*open_audio_channel)(void* proto);    // 打开音频通道
    void (*close_audio_channel)(void* proto);    // 关闭音频通道
    bool (*is_audio_channel_opened)(const void* proto);    // 检查音频通道是否打开
};

/* 协议基础结构 */
typedef struct protocol_s {
    const protocol_ops_t* ops;
    void* impl;                // 协议实现实例: MG 或 WebSocket 或 MQTT 的实例

    int server_sample_rate;   // 服务器端采样率
    int server_frame_duration; // 服务器端帧时长
    char session_id[128];      // 会话ID
    bool error_occurred;       // 错误发生标志
    int64_t last_incoming_time_ms; // 上次收到消息的时间戳

    protocol_json_cb_t  on_incoming_json;    // 收到JSON消息回调
    protocol_audio_cb_t on_incoming_audio;    // 收到音频数据回调
    protocol_void_cb_t  on_audio_channel_opened;    // 音频通道打开回调
    protocol_void_cb_t  on_audio_channel_closed;    // 音频通道关闭回调
    protocol_error_cb_t on_network_error;    // 网络错误回调
    protocol_void_cb_t  on_connected;    // 连接成功回调
    protocol_void_cb_t  on_disconnected;    // 连接断开回调
    void* user_data;                    // 用户数据
} protocol_t;

/* 协议生命周期 */
protocol_t* protocol_create(protocol_type_t type);
void protocol_destroy(protocol_t* proto);

/* 基础协议 API */
void protocol_set_error(protocol_t* proto, const char* message);
void protocol_send_abort_speaking(protocol_t* proto, protocol_abort_reason_t reason);
void protocol_send_wake_word_detected(protocol_t* proto, const char* wake_word);
void protocol_send_start_listening(protocol_t* proto, protocol_listening_mode_t mode);
void protocol_send_stop_listening(protocol_t* proto);
void protocol_send_mcp_message(protocol_t* proto, const char* payload);
bool protocol_is_timeout(const protocol_t* proto);
void protocol_update_last_incoming(protocol_t* proto);

/* 回调设置 */
void protocol_set_on_incoming_json(protocol_t* proto, protocol_json_cb_t cb);
void protocol_set_on_incoming_audio(protocol_t* proto, protocol_audio_cb_t cb);
void protocol_set_on_audio_channel_opened(protocol_t* proto, protocol_void_cb_t cb);
void protocol_set_on_audio_channel_closed(protocol_t* proto, protocol_void_cb_t cb);
void protocol_set_on_network_error(protocol_t* proto, protocol_error_cb_t cb);
void protocol_set_on_connected(protocol_t* proto, protocol_void_cb_t cb);
void protocol_set_on_disconnected(protocol_t* proto, protocol_void_cb_t cb);
void protocol_set_user_data(protocol_t* proto, void* user_data);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H__ */
