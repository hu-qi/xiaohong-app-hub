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

#ifndef __AGENT_STATE_H__
#define __AGENT_STATE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────── 设备状态 ─────────── */
typedef enum {
    AGENT_STATE_IDLE = 0,           /* 待机 */
    AGENT_STATE_WIFI_CONFIGURING,   /* 配网中 */
    AGENT_STATE_CONNECTING,         /* WebSocket 连接中 */
    AGENT_STATE_WAKEUP,             /* 唤醒中 */
    AGENT_STATE_LISTENING,          /* 监听中 */
    AGENT_STATE_SPEAKING,           /* TTS 播放中 */
    AGENT_STATE_FATAL_ERROR,        /* 不可恢复错误 */
    AGENT_STATE_MAX,
} agent_state_t;

/* ─────────── Agent 事件 ─────────── */
typedef enum {
    /* 网络事件 */
    AGENT_EVT_WIFI_CONNECTED = 0,
    AGENT_EVT_WIFI_DISCONNECTED,

    /* 用户交互事件 */
    AGENT_EVT_TOGGLE_CHAT,          /* 唤醒键短按：切换对话 */
    AGENT_EVT_START_WIFI_CONFIG,    /* 唤醒键长按：进入配网 */

    /* 唤醒词 */
    AGENT_EVT_WAKE_WORD_DETECTED,
    AGENT_EVT_INTERRUPT_SPEAKING,
    AGENT_EVT_INTO_SLEEP_MODE,

    /* 服务器协议事件 */
    AGENT_EVT_TTS_START,            /* 服务器开始 TTS */
    AGENT_EVT_TTS_STOP,             /* 服务器结束 TTS */
    AGENT_EVT_AUDIO_CHANNEL_CLOSED, /* WebSocket 断开 */

    /* 内部事件 */
    AGENT_EVT_ERROR,
    AGENT_EVT_SEND_AUDIO,           /* 有音频数据待发送 */
    /** 本段 VAD 开始：在首帧音频前补发 listen/start，避免上一轮 stop 后会话已结束 */
    AGENT_EVT_VAD_SEGMENT_START,
    /** 本段 VAD 结束；见 g_last_vad_segment_bytes 决定是否发 listen/stop */
    AGENT_EVT_VAD_SEGMENT_END,

    AGENT_EVT_MAX,
} agent_event_t;

/* ─────────── 监听模式（ListeningMode / protocol listen mode） ─────────── */
typedef enum {
    /** mode=auto：本段说完由 CI1302 VAD + listen/stop 交服务端做 ASR */
    AGENT_LISTEN_AUTO_STOP = 0,
    /** mode=manual：对标 kListeningModeManualStop，松键/结束对话等场景 */
    AGENT_LISTEN_MANUAL_STOP,
    /**
     * mode=realtime：对标 kListeningModeRealtime，TTS 播放中仍上行麦克风（见 agent_can_uplink_voice），
     * 服务端可据上行音频做语音打断；VAD 段结束仍发 listen/stop，行为与 AUTO 一致。
     */
    AGENT_LISTEN_REALTIME,
} agent_listen_mode_t;

/* ─────────── 公共 API ─────────── */

/**
 * 向 Agent 发送事件（线程安全，可从任何任务/中断中调用）。
 * @param evt 事件类型
 */
void agent_send_event(agent_event_t evt);

/**
 * 获取当前 Agent 的监听模式。
 */
agent_listen_mode_t get_agent_listen_mode(void);


/**
 * 当前是否允许向服务器发送上行音频（WS 已开且状态为 WAKEUP/LISTENING/SPEAKING）。
 * 未就绪时直接丢弃、不投递事件。
 */
bool agent_can_uplink_voice(void);

/**
 * 上行音频负载已就绪（压缩音频等字节流）：拷贝到 staging 并投递 AGENT_EVT_SEND_AUDIO。
 */
void agent_feed_audio_data(const uint8_t *audio_data, size_t len);

/**
 * 获取当前 Agent 状态。
 */
agent_state_t agent_get_state(void);

/**
 * 获取状态名称字符串（用于日志）。
 */
const char *agent_state_name(agent_state_t state);

/**
 * 在 set_voice_interrupt_enabled() 或等价地更新 voice_interrupt 后调用，使 g_listen_mode 与 settings 一致。
 */
void agent_sync_listen_mode_from_settings(void);

/**
 * 语音数据是否应送入上行：LISTENING 始终可；SPEAKING 且 g_listen_mode 为 REALTIME 时亦可（TTS 中语音打断）。
 */
bool agent_should_uplink_audio_data(void);

void agent_send_text(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __AGENT_STATE_H__ */
