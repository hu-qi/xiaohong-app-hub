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
#include <stdbool.h>
#include "task_entry.h"
#include "settings.h"
#include "net_wifi_config.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "audio_config.h"
#include "ota.h"
#include "log.h"
#include <inttypes.h>
#include <time.h>
#include "cmsis_os2.h"
#include "protocol.h"
#include "mongoose_protocol.h"
#include "agent_state.h"
#include "ci1302_tts_downlink.h"
#include "ci1302_opus_decode.h"

#define TAG "[AgentTsk]"

/* 进入 WAKEUP 时在 on_state_changed 中随 detect 上报的展示文案（可与 CI1302 唤醒词对齐） */
static const char k_wake_word_report_text[] = "你好";

/* ─────────── 外部消息队列 ─────────── */
extern osMessageQueueId_t g_agent_event_qid;
extern osMessageQueueId_t g_disp_event_qid;
extern osMessageQueueId_t g_audx_event_qid;

/* ─────────── 模块状态 ─────────── */
static volatile agent_state_t g_state = AGENT_STATE_IDLE;

// 语音芯片每次上报的压缩音频数据为80字节，MCU解析和截取出来后，放到这个缓存中，然后上报给服务器
// 所以这个缓存大小为80字节即可，不需要设置为5K字节
#define AGENT_UPLINK_AUDIO_DATA_MAX  (80)   // (5 * 1024)
static uint8_t g_uplink_audio_data_staging[AGENT_UPLINK_AUDIO_DATA_MAX];
static size_t g_uplink_audio_data_len;
static protocol_t *g_protocol = NULL;
/**
 * 本轮对话的 listen 模式；不在此写入 settings。
 * 仅在从 Idle 发起新会话时由 get_voice_interrupt_enabled() 映射赋值（见 handle_evt_toggle_chat / wake 的 Idle 分支）。
 */
static agent_listen_mode_t g_listen_mode = AGENT_LISTEN_AUTO_STOP;
/** tts/start 后待发送 0x0201：在首包下行音频入队后再发，而非在收到 tts JSON 时发 */
static bool s_tts_pending_start_play;
/** 0x0106 已收到，待上行 staging 清空后再发 listen/stop，避免 WS 上 stop 先于末帧音频 */
static bool s_pending_listen_stop_after_uplink;
/** 单段去重：短时间窗内仅允许一次 listen/stop，抑制抖动/重复 0x0106 */
#define AGENT_VAD_STOP_DEDUP_WINDOW_MS (250U)
static uint32_t s_last_vad_stop_tick;

static const char *STATE_NAMES[] = {
    "Idle",
    "WifiConfig",
    "Connecting",
    "Wakeup",
    "Listening",
    "Speaking",
    "FatalError",
};

/* 协议对象防护：避免并发/异常路径下空指针解引用导致 AgntTask 崩溃。 */
static bool protocol_has_open_checker(const protocol_t *p)
{
    return (p != NULL && p->ops != NULL && p->ops->is_audio_channel_opened != NULL);
}

static bool protocol_channel_opened_safe(const protocol_t *p)
{
    if (!protocol_has_open_checker(p)) {
        return false;
    }
    return p->ops->is_audio_channel_opened(p->impl);
}

static bool protocol_has_start(const protocol_t *p)
{
    return (p != NULL && p->ops != NULL && p->ops->start != NULL);
}

static bool protocol_has_open_audio(const protocol_t *p)
{
    return (p != NULL && p->ops != NULL && p->ops->open_audio_channel != NULL);
}

static bool protocol_has_close_audio(const protocol_t *p)
{
    return (p != NULL && p->ops != NULL && p->ops->close_audio_channel != NULL);
}

/* ─────────── 公共 API ─────────── */

void agent_send_event(agent_event_t evt)
{
    if (g_agent_event_qid == NULL) return;
    uint8_t e = (uint8_t)evt;
    osMessageQueuePut(g_agent_event_qid, &e, 0, 0);
}

bool agent_can_uplink_voice(void)
{
    if (!protocol_channel_opened_safe(g_protocol)) {
        return false;
    }
    if (g_state != AGENT_STATE_LISTENING && g_state != AGENT_STATE_SPEAKING &&
        g_state != AGENT_STATE_WAKEUP) {
        return false;
    }
    return true;
}

agent_listen_mode_t get_agent_listen_mode(void)
{
    return g_listen_mode;
}

// 语音芯片上报的音频数据帧，经MCU解析出音频数据后，通过这里上报给服务器
void agent_feed_audio_data(const uint8_t *audio_data, size_t len)
{
    if (audio_data == NULL || len == 0) {
        return;
    }
    /* WS 未就绪或 Idle 时不入队，避免 CI1302 持续上报导致海量 event 9 与无效丢弃日志 */
    if (!agent_can_uplink_voice()) {
        return;
    }
    if (len > sizeof(g_uplink_audio_data_staging)) {
        len = sizeof(g_uplink_audio_data_staging);
    }
    /* 上一段尚未被 AgentTask 发出则阻塞等待，避免连续 feed 覆盖 staging */
    uint32_t wait = 0;
    while (g_uplink_audio_data_len != 0U && wait < 5000U) {
        (void)osDelay(1);
        wait++;
    }
    if (g_uplink_audio_data_len != 0U) {
        log_error("%s uplink staging busy, drop %lu bytes\r\n", TAG, (unsigned long)len);
        return;
    }
    memcpy(g_uplink_audio_data_staging, audio_data, len);  // 拷贝音频数据到缓存，80字节
    g_uplink_audio_data_len = len;
    if (g_agent_event_qid == NULL) {
        g_uplink_audio_data_len = 0U;
        return;
    }
    uint8_t e = (uint8_t)AGENT_EVT_SEND_AUDIO;
    osStatus_t qst = osMessageQueuePut(g_agent_event_qid, &e, 0, 0);
    if (qst != osOK) {
        log_error("%s agent event queue full, drop audio %lu bytes\r\n", TAG, (unsigned long)len);
        g_uplink_audio_data_len = 0U;
    }
}

agent_state_t agent_get_state(void)
{
    return g_state;
}

const char *agent_state_name(agent_state_t s)
{
    if (s < AGENT_STATE_MAX) return STATE_NAMES[s];
    return "Unknown";
}

/* ─────────── 内部：状态转换 ─────────── */

static protocol_listening_mode_t agent_listen_mode_to_protocol(agent_listen_mode_t m)
{
    switch (m) {
    case AGENT_LISTEN_MANUAL_STOP:
        return PROTOCOL_LISTENING_MODE_MANUAL_STOP;
    case AGENT_LISTEN_REALTIME:
        return PROTOCOL_LISTENING_MODE_REALTIME;
    default:
        return PROTOCOL_LISTENING_MODE_AUTO_STOP;
    }
}

/** 是否在用户说完一段后自动发 listen/stop（与 AUTO、REALTIME 一致；MANUAL 不发） */
static bool agent_listen_mode_uses_vad_segment_stop(agent_listen_mode_t m)
{
    return (m == AGENT_LISTEN_AUTO_STOP || m == AGENT_LISTEN_REALTIME);
}

/**
 * 只读 settings，不修改持久化。策略：默认 auto；voice_interrupt 仅配网页或唤醒键长按(＜5s) 可改。
 */
static agent_listen_mode_t agent_listen_mode_from_settings(void)
{
    return get_voice_interrupt_enabled() ? AGENT_LISTEN_REALTIME : AGENT_LISTEN_AUTO_STOP;
}

void agent_sync_listen_mode_from_settings(void)
{
    g_listen_mode = agent_listen_mode_from_settings();
}

bool agent_should_uplink_ci1302_opus(void)
{
    if (g_state == AGENT_STATE_LISTENING) {
        return true;
    }
    if (g_state == AGENT_STATE_SPEAKING && g_listen_mode == AGENT_LISTEN_REALTIME) {
        return true;
    }
    return false;
}

static bool set_state(agent_state_t new_state);
static void on_state_changed(agent_state_t old_st, agent_state_t new_st);
static void try_flush_pending_listen_stop(void);

static bool set_state(agent_state_t new_state)
{
    agent_state_t old = g_state;
    if (old == new_state) return true;

    /* 合法性校验（对标 device_state_machine.cc IsValidTransition） */
    bool valid = false;
    switch (old) {
    case AGENT_STATE_IDLE:
        valid = (new_state == AGENT_STATE_CONNECTING ||
                 new_state == AGENT_STATE_LISTENING  ||
                 new_state == AGENT_STATE_WIFI_CONFIGURING ||
                 new_state == AGENT_STATE_FATAL_ERROR);
        break;
    case AGENT_STATE_WIFI_CONFIGURING:
        valid = (new_state == AGENT_STATE_IDLE);
        break;
    case AGENT_STATE_CONNECTING:
        valid = (new_state == AGENT_STATE_IDLE ||
                 new_state == AGENT_STATE_LISTENING ||
                 new_state == AGENT_STATE_WAKEUP);
        break;
    case AGENT_STATE_WAKEUP:
        valid = (new_state == AGENT_STATE_IDLE ||
                 new_state == AGENT_STATE_LISTENING);
        break;
    case AGENT_STATE_LISTENING:
        valid = (new_state == AGENT_STATE_SPEAKING ||
                 new_state == AGENT_STATE_IDLE);
        break;
    case AGENT_STATE_SPEAKING:
        valid = (new_state == AGENT_STATE_LISTENING ||
                 new_state == AGENT_STATE_IDLE);
        break;
    case AGENT_STATE_FATAL_ERROR:
        valid = false;
        break;
    default:
        valid = false;
        break;
    }

    if (!valid) {
        log_error("%s invalid transition: %s -> %s\r\n", TAG,
               agent_state_name(old), agent_state_name(new_state));
        return false;
    }

    g_state = new_state;
    log_info("%s state: %d->%d (%s -> %s)\r\n", TAG,
           (int)old, (int)new_state, agent_state_name(old), agent_state_name(new_state));

#if defined(SUPPORT_LVGL)
    if (g_disp_event_qid != NULL) {
        uint8_t ev = eDisp_Prompt_Update;
        (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, 0);
    }
#endif
    on_state_changed(old, new_state);
    return true;
}

/* ─────────── 内部：状态进入时的副作用（对标 HandleStateChangedEvent） ─────────── */

static void on_state_changed(agent_state_t old_st, agent_state_t new_st)
{
    uint8_t disp_evt;

    switch (new_st) {
    case AGENT_STATE_IDLE:
        ci1302_tts_reset();
        /* TODO: 停止音频录制，开启唤醒词检测 */
        break;

    case AGENT_STATE_CONNECTING:
        /* TODO: 显示"连接中" */
        break;

    case AGENT_STATE_LISTENING:
        /* 发送 start_listening(mode: auto/manual/realtime) */
        if (g_protocol) {
            protocol_send_start_listening(g_protocol, agent_listen_mode_to_protocol(g_listen_mode));
        }
        /* 若 VAD 段结束发生在 SPEAKING（打断窗口），回到 LISTENING 后立即补发 listen/stop。 */
        try_flush_pending_listen_stop();
        /* TODO: 开启 CI1302 音频数据转发 */
        break;

    case AGENT_STATE_SPEAKING:
        /* TODO: 停止音频录制（除非 realtime 模式） */
        break;

    case AGENT_STATE_WIFI_CONFIGURING:
        /* 已由 main.c 触发配网流程 */
        break;

    case AGENT_STATE_WAKEUP:
        /* 由 set_state(WAKEUP) 唯一触发上报，避免在事件 handler 里重复堆协议逻辑 */
        if (g_protocol)
            protocol_send_wake_word_detected(g_protocol, k_wake_word_report_text);
        break;

    default:
        break;
    }
}

/* ─────────── 内部：协议回调（运行在 Mongoose poll 线程） ─────────── */
static void on_json_message(void *user_data, const cJSON *root)
{
    (void)user_data;
    if (root == NULL) {
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) return;

    if (strcmp(type->valuestring, "tts") == 0) {
        const cJSON *st = cJSON_GetObjectItem(root, "state");
        /* 带 text 的 tts（如 sentence_start）：写入待显缓冲并投递 eDisp_Text_Update；纯 start/stop 控制帧不刷屏 */
        #if defined(SUPPORT_LVGL)
        const cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text)) {
            const bool skip_ui_text =
                cJSON_IsString(st) &&
                (strcmp(st->valuestring, "start") == 0 || strcmp(st->valuestring, "stop") == 0);
            if (!skip_ui_text) {
                disp_set_pending_text_utf8(text->valuestring);
                if (g_disp_event_qid != NULL) {
                    uint8_t ev = eDisp_Text_Update;
                    (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, 0);
                }
            }
        }
        #endif

        if (cJSON_IsString(st)) {
            if (strcmp(st->valuestring, "start") == 0) {
                /* 与二进制帧同序：先清空再投递，避免随后到的 Opus 在 AgntTask 尚未 SPEAKING 时被丢弃 */
                ci1302_tts_reset();
                s_tts_pending_start_play = true;
                agent_send_event(AGENT_EVT_TTS_START);
            } else if (strcmp(st->valuestring, "stop") == 0) {
                agent_send_event(AGENT_EVT_TTS_STOP);
            }
        }
    }
}

#if CI1302_TTS_DECODE_OPUS_TO_PCM
/* 单帧 Opus 解码输出缓冲：16kHz 单声道最大帧 120ms = 1920 样本（与 libopus 一致） */
#define AGENT_TTS_OPUS_PCM_MAX_SAMPLES 1920
// 服务器下发的压缩音频数据opus，经过解码后，得到的PCM数据为960个样本，每个样本为2字节，所以缓存为1920字节

/*
 * on_audio_data 在 mg_poll 线程执行；pcm 已用静态 s_tts_opus_pcm_scratch。mg_poll 栈见 mongoose_protocol
 * mg_ensure_poll_thread（当前 32KiB），勿在回调内再开大栈数组。
 */
static int16_t s_tts_opus_pcm_scratch[AGENT_TTS_OPUS_PCM_MAX_SAMPLES];
#endif
//static uint64_t s_audio_play_start_time = 0;
//static uint64_t s_audio_vad_end_time = 0;
static void on_audio_data(void *user_data, protocol_audio_packet_t *packet)
{
    (void)user_data;
    if (packet == NULL || packet->payload == NULL || packet->frame_duration == 0U) {
        return;
    }
#if CI1302_TTS_DECODE_OPUS_TO_PCM
    /* 云端固定 16 kHz Opus → 解码 16 kHz PCM；以 uint8_t 字节流入队，供 0x020B 原样下发 */
    const int sr = CI1302_020B_PCM_SAMPLE_RATE_HZ;
    int n = ci1302_opus_decode_pcm(sr, 1, packet->payload, (int)packet->frame_duration,
                                   s_tts_opus_pcm_scratch, AGENT_TTS_OPUS_PCM_MAX_SAMPLES);
    if (n <= 0) {
        log_error("%s on_audio_data: opus->pcm decode failed (%d)\r\n", TAG, n);
        return;
    }

    uint32_t pcm_bytes = (uint32_t)n * (uint32_t)sizeof(int16_t);
    if (!ci1302_tts_push((const uint8_t *)s_tts_opus_pcm_scratch, pcm_bytes)) {
        log_error("%s on_audio_data: tts push failed\r\n", TAG);
        return;
    }
#else
    log_error("%s on_audio_data: need CI1302_TTS_DECODE_OPUS_TO_PCM=1 (PCM to CI1302, not Opus)\r\n", TAG);
    return;
#endif
    /* 运行在 mg_poll 线程，常早于 AgntTask 切到 SPEAKING；tts/start 已在 JSON 回调里 reset */
    if (s_tts_pending_start_play && g_audx_event_qid != NULL) {
        log_debug("%s on_audio_data:    type=[Aud]  send:[eAud_StartPlay]\r\n", TAG);
        uint8_t m = eAud_StartPlay;
        osMessageQueuePut(g_audx_event_qid, &m, 0, 0);
        s_tts_pending_start_play = false;
    }
}

static void on_channel_closed(void *user_data)
{
    (void)user_data;
    log_info("%s on_channel_closed: audio channel closed\r\n", TAG);
    agent_send_event(AGENT_EVT_AUDIO_CHANNEL_CLOSED);
}

static void on_channel_opened(void *user_data)
{
    (void)user_data;
    log_info("%s on_channel_opened: audio channel opened\r\n", TAG);
}

/* ─────────── 内部：从 Settings 应用 WS URL/Token（每次建连前调用，避免 OTA 晚于首次 create） ─────────── */

static void apply_mg_ws_config_from_settings(protocol_t *proto)
{
    char ws_url[MG_WS_URL_MAX];
    char ws_token[MG_WS_TOKEN_MAX];

    ws_url[0] = '\0';
    ws_token[0] = '\0';
    (void)get_websocket_server_url(ws_url, sizeof(ws_url));
    (void)get_websocket_server_token(ws_token, sizeof(ws_token));
    if (ws_url[0] == '\0') {
        log_error("%s mg_ws url is empty\r\n", TAG);
        return;
    }

    int pver = get_websocket_protocol_version();
    if (pver <= 0) {
        pver = 1;
    }
    log_info("%s mg_ws url=%s ver=%d\r\n", TAG, ws_url, pver);
    mg_ws_set_config(proto, ws_url, ws_token, get_device_id_str(), get_device_id_str(), pver);
}

/* ─────────── 内部：创建并配置协议实例 ─────────── */

static protocol_t *create_protocol(void)
{
    protocol_t *proto = protocol_create(PROTOCOL_TYPE_MONGOOSE);
    if (proto == NULL) return NULL;

    apply_mg_ws_config_from_settings(proto);

    /* 注册回调 */
    protocol_set_on_incoming_json(proto, on_json_message);
    protocol_set_on_incoming_audio(proto, on_audio_data);
    protocol_set_on_audio_channel_opened(proto, on_channel_opened);
    protocol_set_on_audio_channel_closed(proto, on_channel_closed);

    return proto;
}

/* ─────────── 内部：建立连接并进入监听（对标 ContinueOpenAudioChannel） ─────────── */

/** 建立 WS + hello；成功返回 true（不改变 Agent 状态）。失败返回 false。 */
static bool open_audio_channel_only(void)
{
    if (g_protocol == NULL) {
        return false;
    }

    /* OTA 可能在首次 create_protocol 之后才返回：建连前再刷一遍 URL/token */
    apply_mg_ws_config_from_settings(g_protocol);

    if (!protocol_has_start(g_protocol) || !g_protocol->ops->start(g_protocol->impl)) {
        log_error("%s protocol start failed (mg_poll 线程未创建，见上一行 [MG_WS])\r\n", TAG);
        protocol_set_error(g_protocol, "Protocol start failed");
        return false;
    }

    if (!protocol_has_open_audio(g_protocol) || !g_protocol->ops->open_audio_channel(g_protocol->impl)) {
        log_error("%s open_audio_channel failed\r\n", TAG);
        return false;
    }

    return true;
}

/**
 * 建链成功后：WAKEUP（on_state_changed 发 detect）→ LISTENING（发 listen/start）。
 * 失败则回到 IDLE。调用方需已处于 CONNECTING 并完成 protocol/WiFi 等前置条件。
 */
static void open_audio_channel_wakeup_then_listen(void)
{
    if (!open_audio_channel_only()) {
        set_state(AGENT_STATE_IDLE);
        return;
    }
    set_state(AGENT_STATE_WAKEUP);
    set_state(AGENT_STATE_LISTENING);
}

/* ─────────── 事件处理器（对标 Application 各 Handle* 方法） ─────────── */

static void handle_evt_toggle_chat(void)
{
    agent_state_t st = g_state;

    switch (st) {
    case AGENT_STATE_IDLE:
        if (get_wifi_status() != eWifi_St_Connected) {
            log_error("%s toggle_chat: WiFi not connected\r\n", TAG);
            return;
        }
        if (g_protocol == NULL) {
            g_protocol = create_protocol();
            if (g_protocol == NULL) {
                log_error("%s create_protocol failed\r\n", TAG);
                return;
            }
        }
        /* 新会话：仅此处置 g_listen_mode，与 settings 中 voice_interrupt 对齐 */
        g_listen_mode = agent_listen_mode_from_settings();
        set_state(AGENT_STATE_CONNECTING);
        open_audio_channel_wakeup_then_listen();
        break;

    case AGENT_STATE_SPEAKING:
        /* 打断 TTS → 回到 LISTENING（对标 AbortSpeaking） */
        if (g_protocol) {
            protocol_send_abort_speaking(g_protocol, PROTOCOL_ABORT_REASON_NONE);
        }
        set_state(AGENT_STATE_LISTENING);
        break;

    case AGENT_STATE_LISTENING:
        /* 结束对话 → 关闭通道 → IDLE */
        if (g_protocol) {
            if (protocol_has_close_audio(g_protocol)) {
                g_protocol->ops->close_audio_channel(g_protocol->impl);
            }
        }
        set_state(AGENT_STATE_IDLE);
        break;

    case AGENT_STATE_WAKEUP:
        /* 唤醒后尚未进入 LISTENING 时短按：结束 */
        if (g_protocol) {
            if (protocol_has_close_audio(g_protocol)) {
                g_protocol->ops->close_audio_channel(g_protocol->impl);
            }
        }
        set_state(AGENT_STATE_IDLE);
        break;

    case AGENT_STATE_CONNECTING:
        /* 用户取消连接 → IDLE（连接函数阻塞中，事件排队，连接返回后处理） */
        break;

    default:
        break;
    }
}

static void handle_evt_wake_word(void)
{
    agent_state_t st = g_state;

    switch (st) {
    case AGENT_STATE_IDLE:
        if (get_wifi_status() != eWifi_St_Connected) {
            log_error("%s wake_word: WiFi not connected\r\n", TAG);
            return;
        }
        if (g_protocol == NULL) {
            g_protocol = create_protocol();
            if (g_protocol == NULL) return;
        }
        /* 新会话：仅此处置 g_listen_mode，与 settings 中 voice_interrupt 对齐 */
        g_listen_mode = agent_listen_mode_from_settings();
        set_state(AGENT_STATE_CONNECTING);
        open_audio_channel_wakeup_then_listen();
        break;
    case AGENT_STATE_WAKEUP:
        /* 已在 WAKEUP 时再次唤醒：仅推进到 LISTENING（detect 仅在进入 WAKEUP 时发过一次） */
        set_state(AGENT_STATE_LISTENING);
        break;
    case AGENT_STATE_SPEAKING:
        /* 打断 TTS → 回到 LISTENING */
        if (g_protocol) {
            protocol_send_abort_speaking(g_protocol, PROTOCOL_ABORT_REASON_WAKE_WORD_DETECTED);
        }
        set_state(AGENT_STATE_LISTENING);
        break;

    case AGENT_STATE_LISTENING:
        /* 已在监听：VAD/重复事件只重发 start_listening，不能转 WAKEUP（状态机不允许 Listening→Wakeup） */
        if (g_protocol) {
            protocol_send_start_listening(g_protocol, agent_listen_mode_to_protocol(g_listen_mode));
        }
        break;
    default:
        break;
    }
}

static void handle_evt_wifi_connected(void)
{
    log_info("%s WiFi connected\r\n", TAG);

    if (g_state == AGENT_STATE_WIFI_CONFIGURING) {
        set_state(AGENT_STATE_IDLE);
    }
    /* 其他状态下仅记录，不触发转换 */
}

static void handle_evt_wifi_disconnected(void)
{
    log_info("%s WiFi disconnected\r\n", TAG);
    agent_state_t st = g_state;

    if (st == AGENT_STATE_CONNECTING || st == AGENT_STATE_WAKEUP || st == AGENT_STATE_LISTENING ||
        st == AGENT_STATE_SPEAKING) {
        s_pending_listen_stop_after_uplink = false;
        if (g_protocol) {
            if (protocol_has_close_audio(g_protocol)) {
                g_protocol->ops->close_audio_channel(g_protocol->impl);
            }
        }
        set_state(AGENT_STATE_IDLE);
    }
}

static void handle_evt_start_wifi_config(void)
{
    if (g_state == AGENT_STATE_IDLE) {
        set_state(AGENT_STATE_WIFI_CONFIGURING);
    }
}

static void handle_evt_tts_start(void)
{
    if (g_state != AGENT_STATE_LISTENING) {
        return;
    }
    /* 不在此 reset：避免清掉 JSON tts/start 之后、本事件之前已入队的下行音频 */
    if (!set_state(AGENT_STATE_SPEAKING)) {
        return;
    }
    /* eAud_StartPlay 在 on_audio_data 首包入队后发送 */
}

static void handle_evt_tts_stop(void)
{
    s_tts_pending_start_play = false;
    /* 将合并缓冲中不足 1024 的尾段推入槽，再判断是否已空 */
    ci1302_tts_flush_pending();
    /* 已空则立即 EndPlay；否则置位，等 CI1302 把剩余 PCM 拉完（all_drained）再 EndPlay，避免误用「短暂空」 */
    ci1302_tts_set_pending_endplay_after_drain(true);

    /* 不在此处 reset：避免丢弃尚未下发的缓存；reset 在 eAud_EndPlay 发完 0x020C 后执行 */
    if (g_state == AGENT_STATE_SPEAKING) {
        if (g_listen_mode == AGENT_LISTEN_MANUAL_STOP) {
            set_state(AGENT_STATE_IDLE);
        } else {
            set_state(AGENT_STATE_LISTENING);
        }
    }
}

static void try_flush_pending_listen_stop(void)
{
    if (!s_pending_listen_stop_after_uplink) {
        return;
    }
    if (g_uplink_audio_data_len != 0U) {
        return;
    }
    if (!protocol_channel_opened_safe(g_protocol)) {
        s_pending_listen_stop_after_uplink = false;
        return;
    }
    if (!agent_listen_mode_uses_vad_segment_stop(g_listen_mode)) {
        s_pending_listen_stop_after_uplink = false;
        return;
    }
    if (g_state != AGENT_STATE_LISTENING && g_state != AGENT_STATE_WAKEUP) {
        /* SPEAKING/CONNECTING 等暂不可发 stop，保留 pending，待回到 LISTENING 再 flush。 */
        return;
    }
    log_info("%s listen/stop (after uplink staging drained)\r\n", TAG);
    protocol_send_stop_listening(g_protocol);
    s_pending_listen_stop_after_uplink = false;
}

static void handle_evt_channel_closed(void)
{
    s_tts_pending_start_play = false;
    s_pending_listen_stop_after_uplink = false;
    if (g_audx_event_qid != NULL && g_state == AGENT_STATE_SPEAKING) {
        uint8_t m = eAud_EndPlay;
        log_info("%s handle_evt_channel_closed: Audio channel closed send EndPlay\r\n", TAG);
        (void)osMessageQueuePut(g_audx_event_qid, &m, 0, 0);
    } else {
        log_info("%s handle_evt_channel_closed: Audio channel closed\r\n", TAG);
    }
    ci1302_tts_reset();
    agent_state_t st = g_state;
    if (st == AGENT_STATE_CONNECTING || st == AGENT_STATE_WAKEUP || st == AGENT_STATE_LISTENING ||
        st == AGENT_STATE_SPEAKING) {
        set_state(AGENT_STATE_IDLE);
    }
}

static void handle_evt_send_audio(void)
{
    if (g_protocol == NULL || g_uplink_audio_data_len == 0) {
        return;
    }
    if (!protocol_channel_opened_safe(g_protocol)) {
        static uint8_t log_ch;
        if (log_ch < 3U) {
            log_warn("%s send_audio: channel not open, drop %lu bytes\r\n", TAG,
                      (unsigned long)g_uplink_audio_data_len);
            log_ch++;
        }
        g_uplink_audio_data_len = 0;
        try_flush_pending_listen_stop();
        return;
    }
    /*
     * 原仅允许 LISTENING：TTS 播放中状态为 SPEAKING，CI1302 仍可能持续上报 Opus，会被静默丢弃。
     * 允许 WAKEUP / LISTENING / SPEAKING 时上行，由服务端决定是否处理（打断/全双工等）。
     */
    if (g_state != AGENT_STATE_LISTENING &&
        g_state != AGENT_STATE_SPEAKING &&
        g_state != AGENT_STATE_WAKEUP) {
        static uint8_t log_st;
        if (log_st < 5U) {
            log_warn("%s send_audio: state=%s, drop %lu bytes\r\n", TAG, agent_state_name(g_state),
                      (unsigned long)g_uplink_audio_data_len);
            log_st++;
        }
        g_uplink_audio_data_len = 0;
        try_flush_pending_listen_stop();
        return;
    }

    protocol_audio_packet_t pkt;
    pkt.sample_rate = (uint32_t)(g_protocol->server_sample_rate > 0 ? g_protocol->server_sample_rate : 16000);
    pkt.frame_duration = (uint32_t)g_uplink_audio_data_len;  // 音频数据帧长度，80字节
    pkt.timestamp = 0;
    pkt.payload = g_uplink_audio_data_staging;  // 音频数据缓存，80字节
    if (g_protocol->ops == NULL || g_protocol->ops->send_audio == NULL ||
        !g_protocol->ops->send_audio(g_protocol->impl, &pkt)) {
        log_error("%s send_audio: protocol send failed (len=%lu)\r\n", TAG,
               (unsigned long)g_uplink_audio_data_len);
    }
    g_uplink_audio_data_len = 0;
    try_flush_pending_listen_stop();
}

/**
 * CI1302 本地 VAD 段结束（0x0106）：置位 pending，在「上行 staging 已空」后再发 listen/stop。
 * 避免 Agent 队列里 0x0106 先于末次 SEND_AUDIO 处理时，WS 上 JSON stop 先于末帧二进制音频发出。
 */
static void handle_evt_ci1302_vad_segment_end(void)
{
    uint32_t seg_bytes = g_ci1302_last_vad_segment_bytes;
    uint32_t now_tick = osKernelGetTickCount();

    if (g_protocol == NULL) {
        return;
    }
    if (!protocol_channel_opened_safe(g_protocol)) {
        return;
    }
    if (!agent_listen_mode_uses_vad_segment_stop(g_listen_mode)) {
        return;
    }
    /* 过滤空语音段：CI1302 可能重复上报 0x0106（第二次 total=0），若继续发 stop 会造成“空段 stop”扰动 ASR。 */
    if (seg_bytes == 0U) {
        log_info("%s ignore listen/stop: empty vad segment\r\n", TAG);
        return;
    }
    if (s_last_vad_stop_tick != 0U) {
        uint32_t delta = now_tick - s_last_vad_stop_tick;
        if (delta < AGENT_VAD_STOP_DEDUP_WINDOW_MS) {
            log_info("%s ignore listen/stop: duplicate vad segment (%lu ms)\r\n", TAG, (unsigned long)delta);
            return;
        }
    }
    s_last_vad_stop_tick = now_tick;
    /* 无论当前是否 SPEAKING，都先挂起：可避免打断窗口丢失 0x0106 导致服务端不出结果。 */
    s_pending_listen_stop_after_uplink = true;
    log_info("%s vad segment end: bytes=%lu, pending listen/stop\r\n", TAG, (unsigned long)seg_bytes);
    try_flush_pending_listen_stop();
}

static void handle_evt_error(void)
{
    log_error("%s error occurred\r\n", TAG);
    agent_state_t st = g_state;
    if (st != AGENT_STATE_IDLE && st != AGENT_STATE_FATAL_ERROR) {
        set_state(AGENT_STATE_IDLE);
    }
}

/* ─────────── Agent 主任务 ─────────── */

void AgentTask(void)
{
    uint8_t evt_raw = 0;

    log_info("%s started, trigger wake_word event ......\r\n", TAG);
    agent_send_event(AGENT_EVT_WAKE_WORD_DETECTED);

    while (1) {
        osStatus_t ret = osMessageQueueGet(g_agent_event_qid, &evt_raw, NULL, 100);
        if (ret != osOK) {
            continue;
        }

        switch (evt_raw) {
        case AGENT_EVT_TOGGLE_CHAT:    // 按键触发唤醒对话
            handle_evt_toggle_chat();
            break;
        case AGENT_EVT_WAKE_WORD_DETECTED:    // 唤醒词触发唤醒对话
            handle_evt_wake_word();
            break;
        case AGENT_EVT_WIFI_CONNECTED:    // WiFi 连接成功
            handle_evt_wifi_connected();
            break;
        case AGENT_EVT_WIFI_DISCONNECTED:    // WiFi 连接断开
            handle_evt_wifi_disconnected();
            break;
        case AGENT_EVT_START_WIFI_CONFIG:    // 开始 WiFi 配置
            handle_evt_start_wifi_config();
            break;
        case AGENT_EVT_TTS_START:    // TTS 开始播放
            handle_evt_tts_start();
            break;
        case AGENT_EVT_TTS_STOP:    // TTS 停止播放
            handle_evt_tts_stop();
            break;
        case AGENT_EVT_AUDIO_CHANNEL_CLOSED:    // 音频通道关闭
            handle_evt_channel_closed();
            break;
        case AGENT_EVT_ERROR:    // 错误事件
            handle_evt_error();
            break;
        case AGENT_EVT_SEND_AUDIO:    // 发送音频数据到服务器进行处理
            handle_evt_send_audio();
            break;
        case AGENT_EVT_CI1302_VAD_SEGMENT_END:    // CI1302 VAD 段结束
            handle_evt_ci1302_vad_segment_end();
            break;
        default:
            break;
        }
    }
}

uint32_t AgentTaskEntry(void)
{
    task_config_t task_conf = {
        "AgntTask",
        1024 * 6,       /* WebSocket 握手需要较大栈 */
        24,
        (void *)AgentTask,
    };
    return create_task(&task_conf, NULL);
}
//SYS_RUN(AgentTaskEntry);
