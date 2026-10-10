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

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "task_entry.h"
#include "net_wifi_config.h"
#include "kb_keymap.h"
#include "log.h"

#include "lwip/api.h"
#include "lwip/ip.h"

/* ================= HTTP 工具函数（参考 xiaohong 的 net_http_server.c） ================= */

#define HTTP_POST_BODY_MAX  (2048U)

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

/** 取表单字段值（x-www-form-urlencoded 解码，'%HH' + '+'） */
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

/* ================= 键名表（HID 键码 -> 显示名，用于配置页下拉） ================= */

typedef struct {
    uint8_t code;
    const char *name;
} key_name_t;

static const key_name_t g_key_names[] = {
    {HID_KEY_A, "A"}, {HID_KEY_B, "B"}, {HID_KEY_C, "C"}, {HID_KEY_D, "D"},
    {HID_KEY_E, "E"}, {HID_KEY_F, "F"}, {HID_KEY_G, "G"}, {HID_KEY_H, "H"},
    {HID_KEY_I, "I"}, {HID_KEY_J, "J"}, {HID_KEY_K, "K"}, {HID_KEY_L, "L"},
    {HID_KEY_M, "M"}, {HID_KEY_N, "N"}, {HID_KEY_O, "O"}, {HID_KEY_P, "P"},
    {HID_KEY_Q, "Q"}, {HID_KEY_R, "R"}, {HID_KEY_S, "S"}, {HID_KEY_T, "T"},
    {HID_KEY_U, "U"}, {HID_KEY_V, "V"}, {HID_KEY_W, "W"}, {HID_KEY_X, "X"},
    {HID_KEY_Y, "Y"}, {HID_KEY_Z, "Z"},
    {HID_KEY_1, "1"}, {HID_KEY_2, "2"}, {HID_KEY_3, "3"}, {HID_KEY_4, "4"},
    {HID_KEY_5, "5"}, {HID_KEY_6, "6"}, {HID_KEY_7, "7"}, {HID_KEY_8, "8"},
    {HID_KEY_9, "9"}, {HID_KEY_0, "0"},
    {HID_KEY_ENTER, "Enter"}, {HID_KEY_ESC, "Esc"}, {HID_KEY_BACKSPACE, "Backspace"},
    {HID_KEY_TAB, "Tab"}, {HID_KEY_SPACEBAR, "Space"}, {HID_KEY_MINUS, "-"},
    {HID_KEY_EQUALS, "="}, {HID_KEY_LEFT_BRACKET, "["}, {HID_KEY_RIGHT_BRACKET, "]"},
    {HID_KEY_BACKSLASH, "\\"}, {HID_KEY_SEMICOLON, ";"}, {HID_KEY_APOSTROPHE, "'"},
    {HID_KEY_GRAVE_ACCENT, "`"}, {HID_KEY_COMMA, ","}, {HID_KEY_PERIOD, "."},
    {HID_KEY_SLASH, "/"}, {HID_KEY_CAPS_LOCK, "CapsLock"},
    {HID_KEY_F1, "F1"}, {HID_KEY_F2, "F2"}, {HID_KEY_F3, "F3"}, {HID_KEY_F4, "F4"},
    {HID_KEY_F5, "F5"}, {HID_KEY_F6, "F6"}, {HID_KEY_F7, "F7"}, {HID_KEY_F8, "F8"},
    {HID_KEY_F9, "F9"}, {HID_KEY_F10, "F10"}, {HID_KEY_F11, "F11"}, {HID_KEY_F12, "F12"},
    {HID_KEY_PRINT_SCREEN, "PrtSc"}, {HID_KEY_SCROLL_LOCK, "ScrLk"}, {HID_KEY_PAUSE, "Pause"},
    {HID_KEY_INSERT, "Insert"}, {HID_KEY_HOME, "Home"}, {HID_KEY_PAGE_UP, "PageUp"},
    {HID_KEY_DELETE, "Delete"}, {HID_KEY_END, "End"}, {HID_KEY_PAGE_DOWN, "PageDown"},
    {HID_KEY_RIGHT_ARROW, "Right"}, {HID_KEY_LEFT_ARROW, "Left"},
    {HID_KEY_DOWN_ARROW, "Down"}, {HID_KEY_UP_ARROW, "Up"},
    {HID_KEY_FN, "Fn"},
};

#define KEY_NAME_COUNT  (sizeof(g_key_names)/sizeof(g_key_names[0]))

/** 在配置页 script 里生成 HID 键码选项，供 JS 构建下拉框 */
static void build_keycode_options(char *buff, size_t cap)
{
    size_t off = 0U;
    size_t i;

    off += (size_t)snprintf(buff + off, cap - off, "var KEYS=[");
    for (i = 0; i < KEY_NAME_COUNT; i++) {
        /* JS 字符串字面量转义：\ 和 " 需要转义，避免破坏数组语法 */
        char esc[24];
        size_t e = 0U;
        const char *name = g_key_names[i].name;
        while (*name != '\0' && e + 3U < sizeof(esc)) {
            if (*name == '\\' || *name == '"') {
                esc[e++] = '\\';
            }
            esc[e++] = *name;
            name++;
        }
        esc[e] = '\0';

        int w = snprintf(buff + off, cap - off, "[0x%02X,\"%s\"],",
            g_key_names[i].code, esc);
        if (w <= 0 || (size_t)w >= cap - off) {
            break;
        }
        off += (size_t)w;
    }
    if (cap > off) {
        off += (size_t)snprintf(buff + off, cap - off, "];");
    }
    if (cap > off) {
        buff[off] = '\0';
    }
}

/* ================= 配置页处理 ================= */

#define CFG_KEYS_MAX   (eKey_Max)

/* 参与配置的键：单键 + 组合键（跳过 Mic 及其组合键，Mic 为系统功能键） */
static const uint8_t g_cfg_keys[] = {
    eKey_Always,        // O 键
    eKey_No,            // ❌ 键
    eKey_Yes,           // ✅ 键
    eKey_Up,            // 上键
    eKey_Down,          // 下键
    eKey_Always_No,     // O+❌
    eKey_Always_Yes,    // O+✅

//  以下两个组合键已经被系统占用，用于在任何情况下都可以切换：
//1.切换键盘的工作模式(Always_Up)：  0-正常工作模式(按键码发往Dongle)，1-本地工作模式(按键码不发往Dongle)
//2.切换按键的映射模式(Always_Down)：0-使用系统默认的按键编码，1-使用用户自定义的按键编码
//  因此，这两个组合键，用户不能修改默认功能，否则无法正常切换上述两个模式的功能
//  eKey_Always_Up,     // O+上
//  eKey_Always_Down,   // O+下
};
#define CFG_KEY_COUNT  (sizeof(g_cfg_keys)/sizeof(g_cfg_keys[0]))

static const char *key_display_name(uint8_t ekey)
{
    switch (ekey) {
        case eKey_Always:      return "O 键";
        case eKey_No:          return "X 键";
        case eKey_Yes:         return "V 键";
        case eKey_Up:          return "上键";
        case eKey_Down:        return "下键";
        case eKey_Always_No:   return "O+X";
        case eKey_Always_Yes:  return "O+V";
        case eKey_Always_Up:   return "O+上";
        case eKey_Always_Down: return "O+下";
        default:               return "?";
    }
}

/* ================= 服务端直出按键卡片（不依赖 JS 也能显示） =================
 *
 * 背景：手机浏览器打开配置页时只能看到说明文字与底部按钮，中间 7 个按键卡片
 * 始终为空。实测：设备端每次都能正常下发整页（日志 `GET / config page`），
 * 说明响应体本身完整；而静态文字/按钮可见、由脚本注入的卡片不可见，
 * 结论是**手机端脚本未执行**（或 render() 抛异常）。
 *
 * 对策：卡片不再由 JS 注入，改为**服务端直接输出真实 HTML**——
 * 这样即使脚本完全不执行，页面也能完整显示并可正常提交表单。
 * 脚本仍会执行 `sync()` 按“步骤数”隐藏多余行（纯增强，失败不影响可用性）。
 *
 * 排版：不申请 ~130KB 静态缓冲，改为**流式**生成（每张卡/每行分段发送），
 * 先用同一函数“计数”得到 Content-Length，再据此实际发送。
 */

/* 修饰键表（须与 JS 中 MODS 一致） */
typedef struct {
    uint8_t bit;
    const char *name;
} kb_mod_t;

static const kb_mod_t g_kb_mods[] = {
    {1U, "Ctrl"}, {2U, "Shift"}, {4U, "Alt"}, {8U, "Win"},
    {16U, "RCtrl"}, {32U, "RShift"}, {64U, "RAlt"}, {128U, "RWin"},
};
#define KB_MOD_COUNT  (sizeof(g_kb_mods)/sizeof(g_kb_mods[0]))

#define APP_EMIT_BUF  (4096U)

/** vsnprintf 包装：返回实际写入长度（超长则截断，绝不越界） */
static int app_fmt(char *buff, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;

    if ((buff == NULL) || (cap == 0U)) {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(buff, cap, fmt, ap);
    va_end(ap);
    if (n <= 0) {
        buff[0] = '\0';
        return 0;
    }
    if ((size_t)n >= cap) {
        n = (int)(cap - 1U);        /* 截断：只发送已落入缓冲的部分 */
    }
    return n;
}

/** 输出一个 <select> 内的全部按键选项（cur=当前选中键码，0 表示“(无)”） */
static int app_put_key_options(char *buff, size_t cap, uint8_t cur)
{
    int off = app_fmt(buff, cap, "<option value=\"0x00\"%s>(无)</option>",
        (cur == 0U) ? " selected" : "");
    size_t i;

    for (i = 0U; i < KEY_NAME_COUNT; i++) {
        if ((size_t)off + 64U >= cap) {
            break;      /* 空间不足：宁缺勿破（不放半个标签） */
        }
        off += app_fmt(buff + off, cap - (size_t)off,
            "<option value=\"0x%02X\"%s>%s</option>",
            (unsigned int)g_key_names[i].code,
            (g_key_names[i].code == cur) ? " selected" : "",
            g_key_names[i].name);
    }
    return off;
}

/* 缓冲将满则先“落盘”：send=true 写入 socket，false 仅累计长度并清零 */
#define APP_FLUSH(conn, buf, off, total, send)                              \
    do {                                                                    \
        if ((size_t)(off) + 512U > sizeof(buf)) {                           \
            if ((send) && ((off) > 0)) {                                    \
                (void)http_netconn_write_all((conn), (buf), (size_t)(off)); \
            }                                                               \
            (total) += (size_t)(off);                                       \
            (off) = 0;                                                      \
        }                                                                   \
    } while (0)

/* 强制“落盘”（无条件）。用于输出长块（如整段 <option> 列表，可达数 KB）之前：
 * 该块本身不调用 APP_FLUSH，若从接近尾部的 offset 开始，会被 app_put_key_options
 * 的空间守卫**静默丢弃**大半（计数与发送虽一致，内容却缺失）。 */
#define APP_FLUSH_FORCE(conn, buf, off, total, send)                        \
    do {                                                                    \
        if ((send) && ((off) > 0)) {                                        \
            (void)http_netconn_write_all((conn), (buf), (size_t)(off));     \
        }                                                                   \
        (total) += (size_t)(off);                                           \
        (off) = 0;                                                          \
    } while (0)

/**
 * 输出 7 个按键卡片。send=false 时只累计字节数（用于算 Content-Length），
 * send=true 时写入连接。返回总字节数。
 */
static size_t app_emit_cards(struct netconn *conn, bool send)
{
    static char buf[APP_EMIT_BUF];
    size_t total = 0U;
    int off = 0;
    size_t i, s, m;

    for (i = 0U; i < CFG_KEY_COUNT; i++) {
        uint8_t ekey = g_cfg_keys[i];
        const keymap_action_t *act = keymap_get_action(ekey);
        uint8_t count = (act != NULL) ? act->step_count : 0U;
        uint8_t visible = (count > (uint8_t)KEYMAP_MACRO_MAX_STEPS)
                            ? (uint8_t)KEYMAP_MACRO_MAX_STEPS : count;

        /* ── 卡片头 + 步骤数 ── */
        APP_FLUSH(conn, buf, off, total, send);
        off += app_fmt(buf + off, sizeof(buf) - (size_t)off,
            "<div class=\"card\" data-ekey=\"%u\"><h2>%s</h2>"
            "<div class=\"step step-count\"><span class=\"step-no\">步骤数:</span>"
            "<select data-count=\"0\">",
            (unsigned int)ekey, key_display_name(ekey));
        for (s = 0U; s <= (size_t)KEYMAP_MACRO_MAX_STEPS; s++) {
            APP_FLUSH(conn, buf, off, total, send);
            off += app_fmt(buf + off, sizeof(buf) - (size_t)off,
                "<option value=\"%u\"%s>%u</option>",
                (unsigned int)s, (s == (size_t)count) ? " selected" : "", (unsigned int)s);
        }
        APP_FLUSH(conn, buf, off, total, send);
        off += app_fmt(buf + off, sizeof(buf) - (size_t)off,
            "</select></div><div class=\"steps\">");

        /* ── 6 行步骤 ── */
        for (s = 0U; s < (size_t)KEYMAP_MACRO_MAX_STEPS; s++) {
            uint8_t cur_mod = 0U, cur_code = 0U;
            bool hidden = (s >= (size_t)visible);

            if ((act != NULL) && (s < (size_t)count)) {
                cur_mod = act->steps[s].modifier;
                cur_code = act->steps[s].keycode;
            }

            /* 行首：未达到“步骤数”的行先隐藏（与 JS sync() 的初始状态一致） */
            APP_FLUSH(conn, buf, off, total, send);
            off += app_fmt(buf + off, sizeof(buf) - (size_t)off,
                "<div class=\"step\"%s><span class=\"step-no\">第%u步:</span><span class=\"mods\">",
                hidden ? " style=\"display:none\"" : "", (unsigned int)(s + 1U));

            /* 修饰键复选框 */
            for (m = 0U; m < KB_MOD_COUNT; m++) {
                APP_FLUSH(conn, buf, off, total, send);
                off += app_fmt(buf + off, sizeof(buf) - (size_t)off,
                    "<label><input type=\"checkbox\" data-mod=\"%u\"%s>%s</label>",
                    (unsigned int)g_kb_mods[m].bit,
                    ((cur_mod & g_kb_mods[m].bit) != 0U) ? " checked" : "",
                    g_kb_mods[m].name);
            }

            /* 按键下拉：整段选项可达数 KB，必须先清空缓冲再写，否则会被截断 */
            APP_FLUSH(conn, buf, off, total, send);
            off += app_fmt(buf + off, sizeof(buf) - (size_t)off, "</span><select>");
            APP_FLUSH_FORCE(conn, buf, off, total, send);
            off += app_put_key_options(buf + off, sizeof(buf) - (size_t)off, cur_code);
            APP_FLUSH_FORCE(conn, buf, off, total, send);
            off += app_fmt(buf + off, sizeof(buf) - (size_t)off, "</select></div>");
        }

        APP_FLUSH(conn, buf, off, total, send);
        off += app_fmt(buf + off, sizeof(buf) - (size_t)off, "</div></div>");
    }

    if (send && (off > 0)) {
        (void)http_netconn_write_all(conn, buf, (size_t)off);
    }
    total += (size_t)off;
    return total;
}

/** 在配置页里生成当前键位配置的 JS 数据：var CFG=[[ekey,[steps]...],...] */
static void build_cfg_js(char *buff, size_t cap)
{
    size_t off = 0U;
    size_t i, s;

    off += (size_t)snprintf(buff + off, cap - off, "var CFG=[");
    for (i = 0; i < CFG_KEY_COUNT; i++) {
        uint8_t ekey = g_cfg_keys[i];
        const keymap_action_t *act = keymap_get_action(ekey);
        uint8_t count = (act != NULL) ? act->step_count : 0U;

        int w = snprintf(buff + off, cap - off, "[%u,[", (unsigned int)ekey);
        if (w <= 0 || (size_t)w >= cap - off) {
            break;
        }
        off += (size_t)w;

        for (s = 0; s < KEYMAP_MACRO_MAX_STEPS; s++) {
            uint8_t mod = 0U, code = 0U;
            if (act != NULL && s < count) {
                mod = act->steps[s].modifier;
                code = act->steps[s].keycode;
            }
            w = snprintf(buff + off, cap - off, "[0x%02X,0x%02X],", mod, code);
            if (w <= 0 || (size_t)w >= cap - off) {
                break;
            }
            off += (size_t)w;
        }
        w = snprintf(buff + off, cap - off, "]],");
        if (w <= 0 || (size_t)w >= cap - off) {
            break;
        }
        off += (size_t)w;
    }
    if (cap > off) {
        off += (size_t)snprintf(buff + off, cap - off, "];");
    }
    if (cap > off) {
        buff[off] = '\0';
    }
}

/** 解析 POST 表单，应用键位配置；返回 true 表示有键位配置被提交 */
static bool http_apply_keymap_post(const char *body, u16_t body_len)
{
    const char *body_end = body + body_len;
    size_t i;
    bool applied = false;

    for (i = 0; i < CFG_KEY_COUNT; i++) {
        uint8_t ekey = g_cfg_keys[i];
        char field[24];
        char value[16];
        const char *p;
        keymap_action_t act = {0};

        (void)snprintf(field, sizeof(field), "key%u_count=", (unsigned int)ekey);
        p = http_mem_find(body, body_len, field, (u16_t)strlen(field));
        if (p == NULL) {
            continue;
        }
        const char *v = p + strlen(field);
        const char *e = v;
        while (e < body_end && *e != '&') {
            e++;
        }
        form_copy_field_url_decoded(value, sizeof(value), v, e);
        int count = atoi(value);
        if (count < 0 || count > KEYMAP_MACRO_MAX_STEPS) {
            continue;
        }

        act.step_count = (uint8_t)count;
        for (uint8_t s = 0; s < act.step_count; s++) {
            char fmod[24], fcode[24];
            char vmod[8], vcode[8];
            const char *pm, *pc;

            (void)snprintf(fmod, sizeof(fmod), "key%u_s%u_mod=", (unsigned int)ekey, (unsigned int)s);
            (void)snprintf(fcode, sizeof(fcode), "key%u_s%u_code=", (unsigned int)ekey, (unsigned int)s);

            pm = http_mem_find(body, body_len, fmod, (u16_t)strlen(fmod));
            pc = http_mem_find(body, body_len, fcode, (u16_t)strlen(fcode));
            if (pm != NULL) {
                const char *v2 = pm + strlen(fmod);
                const char *e2 = v2;
                while (e2 < body_end && *e2 != '&') {
                    e2++;
                }
                form_copy_field_url_decoded(vmod, sizeof(vmod), v2, e2);
                act.steps[s].modifier = (uint8_t)strtol(vmod, NULL, 0);
                log_info("\t ekey[%d]step[%d].modifier[0x%02X]\r\n", i, s, act.steps[s].modifier);
            }
            if (pc != NULL) {
                const char *v2 = pc + strlen(fcode);
                const char *e2 = v2;
                while (e2 < body_end && *e2 != '&') {
                    e2++;
                }
                form_copy_field_url_decoded(vcode, sizeof(vcode), v2, e2);
                act.steps[s].keycode = (uint8_t)strtol(vcode, NULL, 0);
                log_info("\t ekey[%d]step[%d].keycode[0x%02X]\r\n", i, s, act.steps[s].keycode);
            }
        }

        if (keymap_set_action(ekey, (act.step_count > 0) ? &act : NULL) == ERRCODE_SUCC) {
            applied = true;
            log_info("[CfgSrv] apply key[%u][%s] count[%u] OK\r\n",
                (unsigned int)ekey, key_display_name(ekey), (unsigned int)act.step_count);
        }
    }
    return applied;
}

static void http_send_simple_response(struct netconn *conn, const char *title, const char *msg, bool ok)
{
    char html[512];

    (void)snprintf(html, sizeof(html),
        "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>%s</title></head><body style=\"font-family:sans-serif;padding:16px;\">"
        "<div style=\"background:%s;color:%s;padding:16px;border-radius:10px;\">"
        "<h1 style=\"margin:0 0 8px;font-size:1.25rem;\">%s</h1>"
        "<p style=\"margin:0;font-size:15px;\">%s</p></div>"
        "<p style=\"margin-top:16px;\"><a href=\"/\">返回配置页</a></p>"
        "</body></html>",
        title,
        ok ? "#d4f5e0" : "#fde0e0",
        ok ? "#1a7f37" : "#c41e1e",
        title, msg);

    char hdr[160];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n", (unsigned int)strlen(html));
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return;
    }
    (void)http_netconn_write_all(conn, hdr, (size_t)hdr_len);
    (void)http_netconn_write_all(conn, html, strlen(html));
}

#define HTML_PREFIX_CAP  (2048U)
#define CFG_JS_CAP       (2048U)
#define KEYS_JS_CAP      (2048U)
#define HTML_SUFFIX_CAP  (4096U)

/*
 * 配置页 HTML 前缀：完整页面的 <head>/<style>/<body> 开头与 <script> 起始标签。
 * 注意：必须用编译期常量字符串（而非 snprintf 格式串）——
 * 若经 snprintf 格式串处理，CSS 中 "width:100%" 的裸 % 会被当成格式符，
 * 导致输出被截断、<script> 标签丢失，浏览器会把 JS 当纯文本显示。
 */
static const char html_prefix_tpl[] =
    "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
    "<title>小鸿键盘 · 自定义键位</title><style>"
    "*{box-sizing:border-box;}body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\","
    "Roboto,\"PingFang SC\",\"Microsoft YaHei\",sans-serif;font-size:16px;line-height:1.5;"
    "margin:0;padding:16px;background:#f2f2f7;color:#1c1c1e;}"
    ".wrap{max-width:560px;margin:0 auto;}h1{font-size:1.35rem;margin:8px 0 4px;text-align:center;font-weight:700;}"
    "p.hint{text-align:center;color:#8e8e93;font-size:13px;margin:0 0 16px;}"
    ".card{background:#fff;border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 1px 3px rgba(0,0,0,.06);}"
    ".card h2{margin:0 0 10px;font-size:1rem;font-weight:600;}"
    ".step{display:flex;flex-direction:column;gap:6px;margin:8px 0;padding:8px;background:#f6f6f8;border-radius:8px;}"
    ".step .step-no{font-size:13px;color:#3a3a3c;font-weight:600;}"
    ".step .mods{display:flex;flex-wrap:wrap;gap:6px 14px;}"
    ".step .mods label{display:flex;align-items:center;gap:3px;font-size:13px;color:#3a3a3c;white-space:nowrap;}"
    ".step .mods input{width:15px;height:15px;margin:0;}"
    "select{min-height:36px;padding:6px 8px;font-size:14px;border:1px solid #c7c7cc;border-radius:8px;background:#fff;width:100%;}"
    "select[data-count]{width:auto;min-width:72px;}"
    ".step.step-count{flex-direction:row;align-items:center;gap:8px;}"
    "button{width:100%;min-height:48px;margin-top:8px;padding:12px;font-size:17px;font-weight:600;"
    "color:#fff;background:#007aff;border:none;border-radius:12px;cursor:pointer;}"
    "button.sec{background:#8e8e93;margin-top:8px;}"
    "#status{text-align:center;margin:10px 0;font-size:15px;min-height:1.2em;}"
    "</style></head><body><div class=\"wrap\">"
    "<h1>小鸿键盘 · 自定义键位</h1>"
    /* 版本标记：用于**确认手机加载的是新固件**（卡片改为服务端直出后的版本）。
     * 若手机上仍看不到卡片，请先看页面上是否有本行——没有即说明是旧固件/缓存。 */
    "<p class=\"hint\" style=\"color:#8e8e93;\">页面版本 v2（服务端直出卡片）</p>"
    "<p class=\"hint\">每个键都可以配置最多 6 步的自定义按键码（默认 0 表示使用系统默认按键码），每步可勾选修饰键和/或选择一个按键。保存后立即生效。</p>"
    "<div id=\"status\"></div>"
    /* 注意：#app 由服务端紧随其后直接输出 7 个卡片的**真实 HTML**，
     * 因此页面在**脚本未执行**时也能完整显示（详见 app_emit_cards 的注释）。 */
    "<div id=\"app\">";

/* #app 容器收尾 + <script> 开启：位于“服务端直出的卡片 HTML”与“JS 数据”之间 */
static const char html_mid_tpl[] = "</div><script>";

/*
 * 配置页 HTML 后缀：动态数据（var CFG= / var KEYS=）之后的 JS 与页面尾部。
 * 同样使用编译期常量字符串，避免 snprintf 格式串对 % 的误解析。
 */
static const char html_suffix_tpl[] =
    "var CFG_NAMES={2:\"O 键\",3:\"❌ 键\",4:\"✅ 键\",5:\"上键\",6:\"下键\","
    "7:\"O+❌\",8:\"O+✅\",9:\"O+上\",10:\"O+下\"};"
    "var MODS=[[1,\"Ctrl\"],[2,\"Shift\"],[4,\"Alt\"],[8,\"Win\"],"
    "[16,\"RCtrl\"],[32,\"RShift\"],[64,\"RAlt\"],[128,\"RWin\"]];"
    "function esc(s){return (\"\"+s).replace(/[&<>]/g,function(c){"
    "return {\"&\":\"&amp;\",\"<\":\"&lt;\",\">\":\"&gt;\"}[c];});}"
    "function modChecked(mod,bit){return (mod&bit)?\"checked\":\"\";}"
    "function optSel(code){return \"<option value=\\\"0x\"+code.toString(16)+\"\\\">\"+esc(code)+\"</option>\";}"
    "function keySelect(sel,cur){var h=\"<option value=\\\"0x00\\\">(无)</option>\";"
    "for(var i=0;i<KEYS.length;i++){h+=\"<option value=\\\"0x\"+KEYS[i][0].toString(16)+\"\\\"\"+"
    "(KEYS[i][0]==cur?\" selected\":\"\")+\">\"+esc(KEYS[i][1])+\"</option>\";}"
    "return \"<select>\"+h+\"</select>\";}"
    "function stepRow(ekey,si,step){var h=\"<div class=\\\"step\\\">\""
    "+\"<span class=\\\"step-no\\\">第\"+(si+1)+\"步:</span>\""
    "+\"<span class=\\\"mods\\\">\";"
    "for(var mi=0;mi<MODS.length;mi++){"
    "h+=\"<label><input type=\\\"checkbox\\\" data-mod=\\\"\"+MODS[mi][0]+\"\\\" \"+"
    "modChecked(step?step[0]:0,MODS[mi][0])+\">\"+MODS[mi][1]+\"</label>\";}"
    "h+=\"</span>\";"
    "h+=keySelect(null,step?step[1]:0)+\"</div>\";return h;}"
    /* 卡片 HTML 已由服务端直接输出，**无需 JS 渲染**。
     * 此处仅做增强：按“步骤数”隐藏多余行——本脚本即使完全不执行，
     * 页面（含全部卡片与全部步骤）依然完整可见、可提交。 */
    "function syncCards(){var cards=document.querySelectorAll(\".card\");"
    "for(var i=0;i<cards.length;i++){(function(card){"
    "var cs=card.querySelector(\"select[data-count]\");if(!cs){return;}"
    "var rows=card.querySelectorAll(\".steps .step\");"
    "function sync(){var n=parseInt(cs.value,10);"
    "for(var s=0;s<rows.length;s++){rows[s].style.display=(s<n)?\"\":\"none\";}}"
    "cs.onchange=sync;sync();})(cards[i]);}}"
    "function collect(){var form={};var cards=document.querySelectorAll(\".card\");"
    "for(var i=0;i<cards.length;i++){var card=cards[i];"
    /* 用服务端写好的 data-ekey，**不再靠显示名反查**（避免文案改动后静默失效） */
    "var ekey=parseInt(card.getAttribute(\"data-ekey\"),10);"
    "if(isNaN(ekey)){continue;}"
    "var countSel=card.querySelector(\"select[data-count]\");var n=parseInt(countSel.value);"
    "form[\"key\"+ekey+\"_count\"]=n;"
    "var rows=card.querySelectorAll(\".step\");var r=0;"
    "for(var s=0;s<card.querySelectorAll(\".steps .step\").length;s++){"
    "var row=card.querySelectorAll(\".steps .step\")[s];var mod=0;"
    "var cbs=row.querySelectorAll(\"input[data-mod]\");"
    "for(var mi=0;mi<cbs.length;mi++){if(cbs[mi].checked)mod|=parseInt(cbs[mi].getAttribute(\"data-mod\"));}"
    "var code=parseInt(row.querySelector(\"select\").value);"
    "form[\"key\"+ekey+\"_s\"+s+\"_mod\"]=mod;form[\"key\"+ekey+\"_s\"+s+\"_code\"]=code;}"
    "}"
    "return form;}"
    "function save(){var form=collect();var body=Object.keys(form).map(function(k){"
    "return encodeURIComponent(k)+\"=\"+encodeURIComponent(form[k]);}).join(\"&\");"
    "var st=document.getElementById(\"status\");st.innerHTML=\"<span style=\\\"color:#8a6d00;\\\">保存中…</span>\";"
    "fetch(\"/config\",{method:\"POST\",body:body,headers:{\"Content-Type\":"
    "\"application/x-www-form-urlencoded; charset=UTF-8\"}})"
    ".then(function(r){return r.text();}).then(function(html){"
    "var d=new DOMParser().parseFromString(html,\"text/html\");"
    "var h1=d.querySelector(\"h1\");var p=d.querySelector(\"p\");"
    "st.innerHTML=(h1?h1.textContent:\"\")+\" \"+(p?p.textContent:\"\");"
    "}).catch(function(){st.innerHTML=\"<span style=\\\"color:#c41e1e;\\\">保存失败，请重试</span>\";});}"
    "function reset(){var st=document.getElementById(\"status\");"
    "st.innerHTML=\"<span style=\\\"color:#8a6d00;\\\">重置中…</span>\";"
    "fetch(\"/reset\",{method:\"POST\"}).then(function(r){return r.text();}).then(function(){"
    "location.reload();}).catch(function(){st.innerHTML=\"<span style=\\\"color:#c41e1e;\\\">重置失败</span>\";});}"
    /* 卡片已由服务端直出，这里只做“按步骤数隐藏多余行”的增强。
     * 仍保留多路径触发与去重：脚本未执行/报错时，页面本身依旧完整可用。 */
    "var __kb_synced=false;"
    "function kb_boot(){if(__kb_synced){return;}__kb_synced=true;"
    "try{syncCards();}catch(err){}}"
    "if(document.readyState===\"loading\"){"
    "document.addEventListener(\"DOMContentLoaded\",kb_boot);}else{kb_boot();}"
    "window.addEventListener(\"load\",kb_boot);"
    "</script>"
    "<button onclick=\"save()\">保存配置</button>"
    "<button class=\"sec\" onclick=\"reset()\">恢复默认</button>"
    "</div></body></html>";

static void http_send_config_page(struct netconn *conn)
{
    static char cfg_js[CFG_JS_CAP];
    static char keys_js[KEYS_JS_CAP];
    size_t prefix_len, mid_len, cfg_len, keys_len, suffix_len, cards_len, total;

    build_cfg_js(cfg_js, sizeof(cfg_js));
    build_keycode_options(keys_js, sizeof(keys_js));

    prefix_len = sizeof(html_prefix_tpl) - 1U;
    mid_len = sizeof(html_mid_tpl) - 1U;
    cfg_len = strlen(cfg_js);
    keys_len = strlen(keys_js);
    suffix_len = sizeof(html_suffix_tpl) - 1U;

    /* 先“计数”一遍得到卡片字节数（不发送，只累计），据此算出 Content-Length；
     * 随后再“发送”一遍。两遍使用同一个函数，保证长度与实际输出一致。 */
    cards_len = app_emit_cards(NULL, false);
    total = prefix_len + cards_len + mid_len + cfg_len + keys_len + suffix_len;

    /* 带 Content-Length 与 Connection: close 的响应头，避免浏览器等待/报错 */
    char hdr[160];
    int hdr_len = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        (unsigned int)total);
    if (hdr_len <= 0 || hdr_len >= (int)sizeof(hdr)) {
        return;
    }
    (void)http_netconn_write_all(conn, hdr, (size_t)hdr_len);
    (void)http_netconn_write_all(conn, html_prefix_tpl, prefix_len);
    (void)app_emit_cards(conn, true);       /* 7 个卡片：直接输出的真实 HTML */
    (void)http_netconn_write_all(conn, html_mid_tpl, mid_len);
    (void)http_netconn_write_all(conn, cfg_js, cfg_len);
    (void)http_netconn_write_all(conn, keys_js, keys_len);
    (void)http_netconn_write_all(conn, html_suffix_tpl, suffix_len);
}

/* ================= HTTP 服务器任务 ================= */

/* 配置模式退出标志：置位后服务器任务退出 accept 循环并结束 */
static volatile uint8_t g_cfg_server_stop = 0;

void kb_config_server_stop(void)
{
    g_cfg_server_stop = 1;
    log_info("[CfgSrv] kb_config_server_stop: stop request\r\n");
}

static void kb_config_server_serve(struct netconn *conn)
{
    struct netbuf *inbuf = NULL;
    char *buf = NULL;
    u16_t buflen = 0;
    err_t err;

    (void)netconn_set_recvtimeout(conn, 5000);

    err = netconn_recv(conn, &inbuf);
    if (err != ERR_OK) {
        goto out_close;
    }
    netbuf_data(inbuf, (void **)&buf, &buflen);

    if (buflen >= 5U && memcmp(buf, "GET /", 5) == 0) {
        /* 打印 User-Agent：手机浏览器身份是判断“脚本为何不执行”的关键线索 */
        const char *ua = http_mem_find(buf, buflen, "User-Agent:", 11);

        if (ua != NULL) {
            char uabuf[96];
            const char *p = ua + 11;
            const char *end = ua + buflen;
            size_t n = 0U;

            while ((p < end) && ((*p == ' ') || (*p == '\t'))) {
                p++;
            }
            while ((p < end) && (*p != '\r') && (*p != '\n') && (n + 1U < sizeof(uabuf))) {
                uabuf[n++] = *p++;
            }
            uabuf[n] = '\0';
            log_info("[CfgSrv] UA[%s]\r\n", uabuf);
        }
        log_info("[CfgSrv] GET / config page (v2)\r\n");
        http_send_config_page(conn);
    } else if (buflen >= 5U && memcmp(buf, "POST /config", 12) == 0) {
        const char *sep = http_mem_find(buf, buflen, "\r\n\r\n", 4);
        u32_t content_length = 0U;
        u32_t body_len = 0U;
        static char post_body[HTTP_POST_BODY_MAX];
        bool ok = false;

        if (sep != NULL) {
            u16_t hdr_len = (u16_t)(sep - buf);
            const char *body = sep + 4;
            u16_t body_in_first = buflen - (u16_t)(body - buf);

            if (http_parse_content_length(buf, hdr_len, &content_length) == 0 &&
                http_recv_full_post_body(conn, body, body_in_first, content_length,
                    post_body, sizeof(post_body), &body_len)) {
                ok = http_apply_keymap_post(post_body, (u16_t)body_len);
            } else {
                body_len = (u32_t)body_in_first;
                if (body_len > 0U && body_len < sizeof(post_body)) {
                    memcpy_s(post_body, sizeof(post_body), body, body_len);
                    ok = http_apply_keymap_post(post_body, (u16_t)body_len);
                }
            }
        }
        log_info("[CfgSrv] POST /config applied[%d]\r\n", ok ? 1 : 0);
        http_send_simple_response(conn, ok ? "保存成功" : "没有可保存的配置",
            ok ? "键位映射已保存并生效。" : "请返回配置页重新设置。", ok);
    } else if (buflen >= 5U && memcmp(buf, "POST /reset", 11) == 0) {
        errcode_t rc = keymap_reset_all();
        log_info("[CfgSrv] POST /reset rc[%d]\r\n", (int)rc);
        http_send_simple_response(conn, "已恢复默认", "所有键位映射已恢复默认。", (rc == ERRCODE_SUCC));
    } else {
        http_send_simple_response(conn, "404", "Not Found", false);
    }

out_close:
    if (inbuf != NULL) {
        netbuf_delete(inbuf);
    }
    netconn_close(conn);
}

errcode_t KbConfigServerTask(void)
{
    err_t ret = ERR_OK;
    char ifname[] = "ap0";
    uint16_t port = 80;
    struct netconn *conn = NULL;
    struct netconn *newconn = NULL;

    struct netif *netif_p = netifapi_netif_find(ifname);
    if (netif_p == NULL) {
        log_error("[CfgSrv] netif_find[%s] fail\r\n", ifname);
        return ERRCODE_FAIL;
    }
    const ip_addr_t server_ip = netif_p->ip_addr;

    conn = netconn_new(NETCONN_TCP);
    if (conn == NULL) {
        log_error("[CfgSrv] netconn_new fail\r\n");
        return ERRCODE_FAIL;
    }
    conn->recv_timeout = 100;

    /* 允许端口复用：上次任务退出后，已关闭连接可能仍处于 TIME_WAIT 状态占用端口 80，
       若不设置 SOF_REUSEADDR，再次创建任务时 netconn_bind 会返回 ERR_USE(-8)
       struct tcp_pcb 为不完整类型，需经 struct ip_pcb 访问其内嵌的 so_options 字段 */
    if (conn->pcb.tcp != NULL) {
        ip_set_option((struct ip_pcb *)conn->pcb.tcp, SOF_REUSEADDR);
    }

    ret = netconn_bind(conn, &server_ip, port);
    if (ret != ERR_OK) {
        log_error("[CfgSrv] netconn_bind err:%d\r\n", ret);
        netconn_delete(conn);
        return ERRCODE_FAIL;
    }
    ret = netconn_listen(conn);
    if (ret != ERR_OK) {
        log_error("[CfgSrv] netconn_listen err:%d\r\n", ret);
        netconn_delete(conn);
        return ERRCODE_FAIL;
    }
    log_info("[CfgSrv] HTTP server ready on %u.%u.%u.%u:%u\r\n",
        (unsigned int)(ip4_addr1(netif_ip4_addr(netif_p))),
        (unsigned int)(ip4_addr2(netif_ip4_addr(netif_p))),
        (unsigned int)(ip4_addr3(netif_ip4_addr(netif_p))),
        (unsigned int)(ip4_addr4(netif_ip4_addr(netif_p))),
        (unsigned int)port);

    g_cfg_server_stop = 0;
    while (1) {
        if (g_cfg_server_stop) {
            log_error("[CfgSrv] config server stop, exit\r\n");
            break;
        }
        ret = netconn_accept(conn, &newconn);
        if (ret == ERR_OK) {
            kb_config_server_serve(newconn);
            netconn_close(newconn);
            netconn_delete(newconn);
            newconn = NULL;
        } else if (ret != ERR_TIMEOUT) {
            osal_msleep(50);
        }
    }
    netconn_delete(conn);
    osThreadExit();
    return ERRCODE_SUCC;
}

uint32_t KbConfigServerTaskEntry(void)
{
    task_config_t task_conf = {
        "CfgSrvTask",    // task name
        1024*6,          // stack_size
        22,              // priority
        (void*)KbConfigServerTask  // func
    };

    return create_task(&task_conf, NULL);
}
