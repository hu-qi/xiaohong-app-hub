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

#include "systick.h"
#include "protocol.h"
#include "log.h"
#include "settings.h"

#if defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_MONGOOSE)
#include "mongoose_protocol.h"
#elif defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_WEBSOCKET)
#include "websockets_protocol.h"
#endif


__attribute__((weak)) void mg_ws_set_config(protocol_t *proto,
                      const char *url,
                      const char *token,
                      const char *device_id,
                      const char *client_id,
                      int version)
{
    (void)proto;
    (void)url;
    (void)token;
    (void)device_id;
    (void)client_id;
    (void)version;
}
__attribute__((weak)) protocol_t *mongoose_protocol_create(void)
{
    return NULL;
}
__attribute__((weak)) void mongoose_protocol_destroy(void)
{
    return;
}

__attribute__((weak)) void lws_ws_set_config(protocol_t *proto,
                      const char *url,
                      const char *token,
                      const char *device_id,
                      const char *client_id,
                      int version)
{
    (void)proto;
    (void)url;
    (void)token;
    (void)device_id;
    (void)client_id;
    (void)version;
}
__attribute__((weak)) protocol_t *websockets_protocol_create(void)
{
    return NULL;
}
__attribute__((weak)) void websockets_protocol_destroy(void)
{
    return;
}

static bool protocol_can_send_text(const protocol_t *proto)
{
    return (proto != NULL &&
            proto->ops != NULL &&
            proto->ops->send_text != NULL);
}

/* ─────────── 内部：从 Settings 应用 WS URL/Token（每次建连前调用，避免 OTA 晚于首次 create） ─────────── */
void protocol_config(protocol_t *proto)
{
    char ws_url[256];
    char ws_token[256];

    ws_url[0] = '\0';
    ws_token[0] = '\0';
    (void)get_websocket_server_url(ws_url, sizeof(ws_url));
    (void)get_websocket_server_token(ws_token, sizeof(ws_token));
    if (ws_url[0] == '\0') {
        log_error("[Protocol] protocol_config ws_url is empty\r\n");
        return;
    }

    int pver = get_websocket_protocol_version();
    if (pver <= 0) {
        pver = 1;
    }
    log_info("[Protocol] protocol_config ws_url=%s ver=%d\r\n", ws_url, pver);

#if defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_MONGOOSE)
    mg_ws_set_config(proto, ws_url, ws_token, get_device_id_str(), get_device_id_str(), pver);
#elif defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_WEBSOCKET)
    lws_ws_set_config(proto, ws_url, ws_token, get_device_id_str(), get_device_id_str(), pver);
#endif
}

protocol_t* protocol_create(protocol_type_t type)
{
    switch (type) {
        case PROTOCOL_TYPE_MONGOOSE:
            log_info("[Protocol] protocol_create(%d): mongoose_protocol_create\r\n", type);
            return (protocol_t*)mongoose_protocol_create();
        case PROTOCOL_TYPE_WEBSOCKET:
            log_info("[Protocol] protocol_create(%d): websockets_protocol_create\r\n", type);
            return (protocol_t*)websockets_protocol_create();
        default:
            return NULL;
    }
}
void protocol_destroy(protocol_t* proto)
{
#if defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_MONGOOSE)
    mongoose_protocol_destroy();
#elif defined(PROTOCOL_TYPE) && (PROTOCOL_TYPE == PROTOCOL_TYPE_WEBSOCKET)
    websockets_protocol_destroy();
#endif
    proto = NULL;
    return;
}

void protocol_set_error(protocol_t* proto, const char* message) {
    if (proto == NULL) return;
    proto->error_occurred = true;
    if (proto->on_network_error != NULL) {
        proto->on_network_error(proto->user_data, message);
    }
}

void protocol_send_abort_speaking(protocol_t* proto, protocol_abort_reason_t reason) {
    if (!protocol_can_send_text(proto)) return;
    char message[256];
    int len = snprintf(message, sizeof(message),
        "{\"session_id\":\"%s\",\"type\":\"abort\"",
        proto->session_id);
    if (reason == PROTOCOL_ABORT_REASON_WAKE_WORD_DETECTED) {
        len += snprintf(message + len, sizeof(message) - len, ",\"reason\":\"wake_word_detected\"");
    }
    snprintf(message + len, sizeof(message) - len, "}");
    (void)proto->ops->send_text(proto->impl, message);
}

void protocol_send_wake_word_detected(protocol_t* proto, const char* wake_word) {
    if (!protocol_can_send_text(proto)) return;
    char json[256];
    snprintf(json, sizeof(json),
        "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"%s\"}",
        proto->session_id, wake_word ? wake_word : "");
    log_info("[Protocol] protocol_send_wake_word_detected: state=detect wake_word=%s\r\n", wake_word ? wake_word : "");
    (void)proto->ops->send_text(proto->impl, json);
}

void protocol_send_start_listening(protocol_t* proto, protocol_listening_mode_t mode) {
    if (!protocol_can_send_text(proto)) return;
    const char* mode_str = "manual";
    if (mode == PROTOCOL_LISTENING_MODE_REALTIME) {
        mode_str = "realtime";
    } else if (mode == PROTOCOL_LISTENING_MODE_AUTO_STOP) {
        mode_str = "auto";
    }
    char message[256];
    snprintf(message, sizeof(message),
        "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"start\",\"mode\":\"%s\"}",
        proto->session_id, mode_str);
    log_info("[Protocol] protocol_send_start_listening: state=start mode=%s\r\n", mode_str);
    (void)proto->ops->send_text(proto->impl, message);
}

void protocol_send_stop_listening(protocol_t* proto) {
    if (!protocol_can_send_text(proto)) return;
    char message[256];
    snprintf(message, sizeof(message),
        "{\"session_id\":\"%s\",\"type\":\"listen\",\"state\":\"stop\"}",
        proto->session_id);
    log_info("[Protocol] protocol_send_stop_listening: state=stop\r\n");
    (void)proto->ops->send_text(proto->impl, message);
}

void protocol_send_mcp_message(protocol_t* proto, const char* payload) {
    if (!protocol_can_send_text(proto) || payload == NULL) return;
    char message[512];
    snprintf(message, sizeof(message),
        "{\"session_id\":\"%s\",\"type\":\"mcp\",\"payload\":%s}",
        proto->session_id, payload);
    (void)proto->ops->send_text(proto->impl, message);
}

bool protocol_is_timeout(const protocol_t* proto) {
    if (proto == NULL) return true;
    return false;
}

void protocol_update_last_incoming(protocol_t* proto) {
    if (proto != NULL) {
    }
}

void protocol_set_on_incoming_json(protocol_t* proto, protocol_json_cb_t cb) {
    if (proto == NULL) return;
    proto->on_incoming_json = cb;
}

void protocol_set_on_incoming_audio(protocol_t* proto, protocol_audio_cb_t cb) {
    if (proto == NULL) return;
    proto->on_incoming_audio = cb;
}

void protocol_set_on_audio_channel_opened(protocol_t* proto, protocol_void_cb_t cb) {
    if (proto == NULL) return;
    proto->on_audio_channel_opened = cb;
}

void protocol_set_on_audio_channel_closed(protocol_t* proto, protocol_void_cb_t cb) {
    if (proto == NULL) return;
    proto->on_audio_channel_closed = cb;
}

void protocol_set_on_network_error(protocol_t* proto, protocol_error_cb_t cb) {
    if (proto == NULL) return;
    proto->on_network_error = cb;
}

void protocol_set_on_connected(protocol_t* proto, protocol_void_cb_t cb) {
    if (proto == NULL) return;
    proto->on_connected = cb;
}

void protocol_set_on_disconnected(protocol_t* proto, protocol_void_cb_t cb) {
    if (proto == NULL) return;
    proto->on_disconnected = cb;
}

void protocol_set_user_data(protocol_t* proto, void* user_data) {
    if (proto == NULL) return;
    proto->user_data = user_data;
}
