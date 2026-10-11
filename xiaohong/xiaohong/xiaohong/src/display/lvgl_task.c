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

#include "timer.h"
#include "chip_core_irq.h"

#include "board_config.h"
#include "disp_driver.h"
#include "task_entry.h"
#include "net_wifi_config.h"
#include "settings.h"
#include "agent_state.h"
#include "lvgl.h"
#include "lvgl_ui_layout.h"
#include "app_hub.h"
#include "ota_fw_upgrade.h"
/* lvgl_ui_layout.h：LV_FONT_DECLARE 与 ui_text_area_config 主文本默认字体一致 */
#if defined(SUPPORT_LFS)
#include "font_download.h"
#include "lv_fs_lfs_adapt.h"
#include "lv_font_lfs_binfont.h"
#endif

#define USE_AI_VERSION  (1)

/* LVGL 局部刷新块高度（行），一般为屏高的 1/4～1/2（如 240 屏用 48～120）
   该值越大，刷新越快、占 SRAM 越多，但容易造成内存不够，导致mg_poll任务异常
*/
#if SUPPORT_OHOS
// OH6.1轻量系统
//#define LVGL_PARTIAL_BUF_LINES       (16)  // 240*16*2= 7680Byte Buffer
// OH7.0轻量系统小型化优化提升
#define LVGL_PARTIAL_BUF_LINES         (20)  // 240*20*2= 9600Byte Buffer
#else
#define LVGL_PARTIAL_BUF_LINES         (40)  // 240*40*2=19200Byte Buffer
#endif

/* RGB565 partial, see disp_driver.h */
static uint8_t disp_buffer[DISP_WIDTH * LVGL_PARTIAL_BUF_LINES * 2];
static char s_lvgl_pending_text_line[DISP_SHOW_TEXT_MAX_LEN+1] = {0};
extern osMessageQueueId_t g_disp_event_qid;
static uint8_t devId[2] = {0};
extern uint8_t g_ota_check_connect_to_server_failed;

lv_obj_t* status_bar_obj = NULL;   // 状态栏，固定显示
lv_obj_t* emoji_obj      = NULL;   // 表情部分
lv_obj_t* full_txt_obj   = NULL;   // 状态栏下面的全屏文本部分，用于显示系统配网、字库下载等提示信息
lv_obj_t* interact_obj   = NULL;   // 状态栏下面的互动文本部分，包含状态提示、半屏对话区域

lv_obj_t* interact_txt_prompt = NULL;   // 状态提示部分的文本
lv_obj_t* interact_txt_chat   = NULL;   // 半屏对话（viewport 约 9×4 行，超长逐行滚动）

lv_obj_t* full_txt            = NULL;   // 全屏文本部分，用于显示系统配网、字库下载等提示信息

#if USE_AI_VERSION
/**
 * LVGL 的 tick 必须在调用 lv_timer_handler() 的同一线程/上下文中推进。
 * 在硬件定时器 ISR 里调用 lv_tick_inc() 会与 LvglTask 并发，破坏 LVGL 内部状态（典型 fault：mtval≈0x20、a0=0）。
 */
static uint32_t xh_lvgl_elapsed_ms_since(uint32_t *prev_tick)
{
    uint32_t now = osKernelGetTickCount();
    uint32_t freq = osKernelGetTickFreq();
    uint32_t elapsed;

    if (freq == 0U) {
        freq = 1U;
    }
    elapsed = (uint32_t)(((uint64_t)(now - *prev_tick) * 1000ULL) / (uint64_t)freq);
    *prev_tick = now;
    if (elapsed == 0U) {
        elapsed = 1U;
    }
    if (elapsed > 500U) {
        elapsed = 500U;
    }
    return elapsed;
}
#else
static timer_handle_t timerHandle   = NULL;
static timer_index_t  timerIndex    = 1;
static uint32_t       timerIrqId    = TIMER_1_IRQN;
static uint32_t       timerDelay    = 1000;  // us, 1000us=1ms, 1000ms=1s
static uint16_t       timerPriority = 1;
void disp_timer_cb(uint32_t data)
{
    unused(data);
    lv_tick_inc(1);    // notice lvgl that 1ms timeout
    uapi_timer_start(timerHandle, timerDelay, disp_timer_cb, 0);    // 1000us = 1ms
}

void disp_timer_config(void)
{
    uapi_timer_init();                                            // 初始化定时器模块
    uapi_timer_adapter(timerIndex, timerIrqId, timerPriority);    // 配置定时器适配器
    uapi_timer_create(timerIndex, &timerHandle);                  // 创建定时器，并获取定时器句柄
    uapi_timer_start(timerHandle, timerDelay, disp_timer_cb, 0);  // 启动定时器，设置触发时间和回调函数
}
#endif

void disp_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1) * 2;
    disp_set_region(area->x1, area->y1, area->x2, area->y2);
    disp_write_buff(LCD_DAT, px_map, len);
    lv_display_flush_ready(disp_drv);
}

static void xh_app_hub_tick_timer_cb(lv_timer_t *t)
{
    (void)t;
    xh_app_hub_tick();
}

void LvglTask(void)
{
    osStatus_t ret = osError;
    uint8_t msg_recv = 0;

    get_device_id(devId, 2);
    disp_init_panel();
    #if !USE_AI_VERSION
    disp_timer_config();
    #endif

    lv_init();

    lv_display_t* disp_obj = lv_display_create(DISP_WIDTH, DISP_HEIGHT);
    lv_display_set_flush_cb(disp_obj, disp_flush_cb);
    lv_display_set_buffers(disp_obj, disp_buffer, NULL, sizeof(disp_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ui_common_text_style_config();
    disp_create_ui();
    xh_app_hub_init();

    /* App Hub tick timer — 每 100ms 调用 xh_app_hub_tick() */
    lv_timer_create(xh_app_hub_tick_timer_cb, 100, NULL);

    disp_clear(WHITE);
    set_blk_level(get_blk_level());

    #if USE_AI_VERSION
    uint32_t lv_prev_kernel_tick = osKernelGetTickCount();
    #endif

    while (1) {
    #if USE_AI_VERSION
        lv_tick_inc(xh_lvgl_elapsed_ms_since(&lv_prev_kernel_tick));
        /* 返回值 = 距下一次 lv_timer 的毫秒数；用于队列等待上限，便于让出 CPU、喂狗，避免 LvglTask 长时间占满 */
        uint32_t next_ms = lv_timer_handler();
        //disp_update_ui_status(status_bar_obj);

        /*
         * qwait 过大时：无队列消息会长时间阻塞，lv_timer_handler 调用间隔变大，动画/滚动发涩。
         * 上限取 5ms：无消息时最多隔 5ms 再进一轮（实际帧率仍受 lv_timer_handler + SPI 耗时限制）。
         */
        uint32_t qwait = next_ms;
        if (qwait < 1U) {
            qwait = 1U;
        }
        if (qwait > 5U) {
            qwait = 5U;
        }
        ret = osMessageQueueGet(g_disp_event_qid, (void *)&msg_recv, NULL, qwait);
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }
    #else
        lv_timer_handler();

        ret = osMessageQueueGet(g_disp_event_qid, (void *)&msg_recv, NULL, 5);  //ms
        if (ret != osOK) {
            msg_recv = 0;
            continue;
        }
    #endif

        disp_update_ui(msg_recv);
    }
}

uint32_t LvglTaskEntry(void)
{
    task_config_t task_conf = {
        "LvglTask",      // task name
        1024 * 9,        // 流式字库 + LVGL 绘制链较深；过小会 NMI、fp 回溯失败，异常表里 sp 会落到栈区外
        24,              // priority
        (void*)LvglTask  // func
    };

    return create_task(&task_conf, NULL);
}
// SYS_RUN(LvglTaskEntry);

void disp_update_ui_status(lv_obj_t* label_st)
{
    if (label_st == NULL) {
        return;
    }

    static uint8_t g_wifi_st = 0xFF;
    static uint8_t g_vol = 0xFF;
    static uint8_t g_blk = 0xFF;
    static uint8_t g_voltage = 0xFF;

    uint8_t flush = 0;
    if (g_wifi_st != get_wifi_status()) {
        g_wifi_st = get_wifi_status();
        flush = 1;
    }
    if (g_vol != get_volume()) {
        g_vol = get_volume();
        flush = 1;
    }
    if (g_blk != get_blk_level()) {
        g_blk = get_blk_level();
        flush = 1;
    }

    uint8_t level = get_voltage_level();

#if STATUS_USE_SYMBOL
    uint8_t volt = level;

    if (volt < 20) {
        volt = 0;
    } else if (volt < 40) {
        volt = 1;
    } else if (volt < 60) {
        volt = 2;
    } else if (volt < 80) {
        volt = 3;
    } else {
        volt = 4;
    }
    if (g_voltage != volt) {
        flush = 1;
        g_voltage = volt;
    }

    char* battery_symbol[5] = {
        LV_SYMBOL_BATTERY_EMPTY,
        LV_SYMBOL_BATTERY_1,
        LV_SYMBOL_BATTERY_2,
        LV_SYMBOL_BATTERY_3,
        LV_SYMBOL_BATTERY_FULL
    };

    static uint8_t g_mode = 0xFF;
    if (g_mode != get_agent_listen_mode()) {
        g_mode = get_agent_listen_mode();
        flush = 1;
    }
    if (flush) {
        if (g_vol == 0) {
            lv_label_set_text_fmt(label_st, "%s  %s   %c   %s %d  %s",
                (g_wifi_st==eWifi_St_Connected)?LV_SYMBOL_WIFI:"    ",
                ST_SYMBOL_MUTE,
                (g_mode == AGENT_LISTEN_REALTIME)?'R':'M',
                ST_SYMBOL_BRIGHTNESS, g_blk,
                battery_symbol[g_voltage]);
        } else {
            lv_label_set_text_fmt(label_st, "%s  %s %d   %c   %s %d  %s",
                (g_wifi_st==eWifi_St_Connected)?LV_SYMBOL_WIFI:"     ",
                LV_SYMBOL_VOLUME_MID, g_vol,
                (g_mode == AGENT_LISTEN_REALTIME)?'R':'M',
                ST_SYMBOL_BRIGHTNESS, g_blk,
                battery_symbol[g_voltage]);
        }
    }
#else
    if (g_voltage != level) {
        if ((level == g_voltage+1) || (level == g_voltage-1)) {
            // level changes only 1%, do NOT update status bar
        } else {
            flush = 1;
        }
        g_voltage = level;
    }

    if (flush) {
    lv_label_set_text_fmt(label_st, "WiFi:%s   Vol:%d   Blk:%d   Bat:%d",
        (g_wifi_st==eWifi_St_Connected)?"OK":((g_wifi_st==eWifi_St_DisConnected)?"NC":"##"),
        g_vol, g_blk, g_voltage);
    }
#endif
}

void disp_create_ui(void)
{
    status_bar_obj = lv_label_create(lv_scr_act());       // 状态栏，固定显示

    #if EMOJI_USE_FONT
    emoji_obj  = lv_label_create(lv_scr_act());       // 表情部分是表情文字
    #else
    emoji_obj  = lv_image_create(lv_scr_act());       // 表情部分是图片
    #endif

    interact_obj   = lv_obj_create(lv_scr_act());         // 状态栏下面的'互动文本'部分，包含状态提示、半屏对话区域两个子组件
    interact_txt_prompt = lv_label_create(interact_obj);  // '互动文本部分'中的'状态提示'文本
    interact_txt_chat   = lv_label_create(interact_obj);  // '互动文本部分'中的'半屏对话区域'文本

    full_txt_obj = lv_obj_create(lv_scr_act());      // 状态栏下面的'全屏文本'部分，用于显示系统配网、字库下载等提示信息
    full_txt     = lv_label_create(full_txt_obj);    // 状态栏下面的'全屏文本'部分，用于显示系统配网、字库下载等提示信息

    ui_status_obj_config(status_bar_obj);
    ui_emoji_obj_config(emoji_obj);
    ui_interact_obj_config(interact_obj, interact_txt_prompt, interact_txt_chat);
    ui_full_text_obj_config(full_txt_obj, full_txt);

    lv_obj_add_flag(emoji_obj, LV_OBJ_FLAG_HIDDEN);    // 隐藏表情区域
    lv_obj_add_flag(interact_obj, LV_OBJ_FLAG_HIDDEN);     // 隐藏互动文本区域
    lv_obj_remove_flag(full_txt_obj, LV_OBJ_FLAG_HIDDEN);  // 显示全屏文本区域

    // 上面初始化所有组件时，配置的默认字体都是22_Default，下面再针对性地设置'互动文本'部分两处交互文本区域的字体为流式字库
#if defined(SUPPORT_LFS)
    xh_lv_font_bin_init();
    if (xh_lv_font_bin_get() != NULL) {
        printf("[LvglTask] external font: SourceHanSerifCN_SemiBold_22 (in font.bin)\r\n");
        ui_text_area_set_default_font(interact_txt_chat, 0);
        ui_text_area_set_default_font(interact_txt_prompt, 0);
    } else {
        printf("[LvglTask] built-in font: SourceHanSerifCN_SemiBold_22_Default (no font.bin)\r\n");
        ui_text_area_set_default_font(interact_txt_chat, 1);
        ui_text_area_set_default_font(interact_txt_prompt, 1);
    }
    ui_text_area_set_default_font(full_txt, 1);
#endif
}

void disp_update_ui(uint8_t cmd)
{
    if (full_txt_obj == NULL || full_txt == NULL ||
        emoji_obj == NULL || interact_obj == NULL ||
        interact_txt_prompt == NULL || interact_txt_chat == NULL) {
        return;
    }

    /* App Hub 使用独立的 LVGL screen。必须先分流，不能让 App Hub
     * 事件触发下方 AI 页面类型判断（原代码会隐藏 AI 表情/对话区域）。 */
    if (cmd == eDisp_AppHub_Open) {
        xh_app_hub_open();
        return;
    }
    if (cmd == eDisp_AppHub_Close) {
        xh_app_hub_close();
        return;
    }
    if (cmd == eDisp_AppHub_Toggle) {
        if (xh_app_hub_is_active()) {
            xh_app_hub_close();
        } else {
            xh_app_hub_open();
        }
        return;
    }
    if (cmd == eDisp_AppHub_Tick) {
        xh_app_hub_tick();
        return;
    }
    if (cmd >= eDisp_AppHub_KeyBase) {
        xh_app_hub_handle_key(cmd - eDisp_AppHub_KeyBase);
        return;
    }

    static uint8_t page = 0;
    agent_state_t agent_st = AGENT_STATE_IDLE;
    uint8_t wifi_st  = eWifi_St_DisConnected;
    char ssid[WIFI_SSID_LEN_MAX+1] = {0};
    char emoji[5] = {0xF0, 0x9F, 0x98, 0x80, 0x00};
    // default to: 0xF09F9880: 😀 Grinning Face
    // llm set to: 0xF09F9982: 🙂 Slightly Smiling Face
	lv_color_t color = lv_color_make(0xFF, 0xFF, 0xFF);  // default white
    int font_version = 0; // get_font_version();
    int fw_version = 0;   // get_fw_version();

    if (cmd >= eDisp_Welcome) {
        lv_obj_add_flag(emoji_obj, LV_OBJ_FLAG_HIDDEN);        // 隐藏表情区域
        lv_obj_add_flag(interact_obj, LV_OBJ_FLAG_HIDDEN);     // 隐藏互动文本区域
        lv_obj_remove_flag(full_txt_obj, LV_OBJ_FLAG_HIDDEN);  // 显示全屏文本区域
        ui_set_text_line_space(-10);
        lv_obj_set_style_text_align(full_txt, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else if (cmd >= eDisp_Emoji_Update) {
        // 3/4/5
        lv_obj_remove_flag(emoji_obj, LV_OBJ_FLAG_HIDDEN);     // 显示表情区域
        lv_obj_remove_flag(interact_obj, LV_OBJ_FLAG_HIDDEN);  // 显示互动文本区域
        lv_obj_add_flag(full_txt_obj, LV_OBJ_FLAG_HIDDEN);     // 隐藏全屏文本区域
        ui_set_text_line_space(4);
    } else {
        // 0/1/2
        // eDisp_Blk_On,         // 亮屏, 与UI布局无关
        // eDisp_Blk_Off,        // 息屏, 与UI布局无关
        // eDisp_Bar_Update,     // 状态栏, 一直存在，只是内容更新，与UI布局无关
    }

    switch (cmd) {
        case eDisp_Blk_On:
            set_blk_level(get_blk_level());
            break;
        case eDisp_Blk_Off:
            if (get_wifi_status() == eWifi_St_ReConfiguring) {
                // 配网中，不熄屏
            } else {
                set_blk_level_off();
            }
            break;
        case eDisp_Bar_Update:
            disp_update_ui_status(status_bar_obj);
            break;

//////////////////////////////////////////////////////////////////////////////////////////////////
        case eDisp_Emoji_Update: {
        #if EMOJI_USE_FONT
            if (disp_take_emoji_utf8(emoji, 5)) {
                lv_label_set_text(emoji_obj, (const char*)emoji);
            }
        #else
            lv_image_set_src(emoji_obj, &img_emoji_0xF600);
        #endif
            break;
        }
        case eDisp_Prompt_Update: {
            agent_st = agent_get_state();
            if (agent_st == AGENT_STATE_SPEAKING) {
                emoji[3] = 0x83;  // 0xF09F9883: 😃 Grinning Face with Big Eyes
#if SUPPORT_OHOS
                color = lv_color_make(0xFF, 0x82, 0x14);  // RRGGBB
#else
                color = lv_color_make(0x00, 0x50, 0xFF);  // RRGGBB
#endif
                lv_obj_set_style_text_color(emoji_obj, color, 0);
                lv_label_set_text(emoji_obj, (const char*)emoji);

                lv_obj_set_style_text_color(interact_txt_prompt, color, 0);
                lv_label_set_text(interact_txt_prompt, "[ 正在说话 ]");
            } else if (agent_st == AGENT_STATE_LISTENING) {
                emoji[2] = 0x99;
                emoji[3] = 0x82;  // 0xF09F9982: 🙂 Slightly Smiling Face
#if SUPPORT_OHOS
                color = lv_color_make(0xFF, 0x82, 0x14);  // RRGGBB
#else
                color = lv_color_make(0x00, 0x50, 0xFF);  // RRGGBB
#endif
                lv_obj_set_style_text_color(emoji_obj, color, 0);
                lv_label_set_text(emoji_obj, (const char*)emoji);

                lv_obj_set_style_text_color(interact_txt_prompt, color, 0);
                lv_label_set_text(interact_txt_prompt, "[ 正在倾听 ]");

                disp_clear_text(interact_txt_chat, 1000);  // xx ms后清空对话框文本
            } else if (agent_st == AGENT_STATE_IDLE) {
                emoji[3] = 0xB4;  // 0xF09F98B4: 😴 Sleeping Face
                color = lv_color_make(0x9A, 0x9A, 0x9A);  // RRGGBB
                lv_obj_set_style_text_color(emoji_obj, color, 0);
                lv_label_set_text(emoji_obj, (const char*)emoji);

                lv_obj_set_style_text_color(interact_txt_prompt, color, 0);
                lv_label_set_text(interact_txt_prompt, "[ 等待唤醒 ]");

                disp_clear_text(interact_txt_chat, 1000);  // xx ms后清空对话框文本
            } else if (agent_st == AGENT_STATE_CONNECTING) {
                emoji[3] = 0x89;  // 0xF09F9889: 😉 Winking Face
                color = lv_color_make(0x5B, 0x5B, 0x5B);  // RRGGBB
                lv_obj_set_style_text_color(emoji_obj, color, 0);
                lv_label_set_text(emoji_obj, (const char*)emoji);

                lv_obj_set_style_text_color(interact_txt_prompt, color, 0);
                lv_label_set_text(interact_txt_prompt, "[ 正在唤醒 ]");
                lv_label_set_text(interact_txt_chat, " ");

                disp_update_ui_status(status_bar_obj);  // 唤醒时更新状态栏
            } else {
                lv_label_set_text(interact_txt_prompt, " ");
                lv_label_set_text(interact_txt_chat, " ");
            }
            break;
        }
        case eDisp_Text_Update: {
            disp_show_pending_text_utf8(interact_txt_chat);
            break;
        }
//////////////////////////////////////////////////////////////////////////////////////////////////

        case eDisp_WiFi_St_Update: {
            wifi_st = get_wifi_status();
            if (wifi_st == eWifi_St_ReConfiguring) {
                lv_obj_set_style_text_align(full_txt, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                if (page == 0) {
                    page = (page+1)&0x01;
                #if HOTSPOT_SEC_OPEN_TYPE
                    lv_label_set_text_fmt(full_txt,
                        "请按提示操作配网:\n"
                        "    按左键切换提示页\n"
                    #if SUPPORT_DELETE_FONT
                        "    按右键删除字库\n"
                    #endif
                        "1.手机连接热点: \n   %s\n"
                        "2.开放热点无密码\n",
                        get_hotspot_name());
                #else
                    lv_label_set_text_fmt(full_txt,
                            "请按提示操作配网:\n"
                            "    按左键切换提示页\n"
                    #if SUPPORT_DELETE_FONT
                            "    按右键删除字库\n"
                    #endif
                            "1.手机连接热点: \n   %s\n"
                            "2.输入热点密码: \n   12345678\n",
                            get_hotspot_name());
                #endif
                } else {
                    page = (page+1)&0x01;
                    lv_label_set_text_fmt(full_txt,
                        "3.用浏览器登录: \n   http://192.168.1.1\n"
                        "4.输入目标WiFi的SSID和密码, 点击连接");
                }
            } else {
                page = 0;
                if (wifi_st == eWifi_St_Connected) {
                    lv_label_set_text(full_txt, "\nWiFi 连接成功");
                } else if (wifi_st == eWifi_St_AutoConnecting) {
                    if(get_wifi_ssid(ssid, WIFI_SSID_LEN_MAX) == ERRCODE_SUCC) {
                        font_version = get_font_version();
                        fw_version = get_fw_version();
                        lv_label_set_text_fmt(full_txt,
                            "   设备 ID: \n[%s]\n"
                            "  固件版本: [%d.%d.%d]\n"
                            "  字库版本: [%d.%d.%d]\n"
                            "正在连接 WiFi:\n%s",
                            get_device_id_str(),
                            fw_version >> 16, (fw_version >> 8) & 0xFF, fw_version & 0xFF,
                            font_version >> 16, (font_version >> 8) & 0xFF, font_version & 0xFF,
                            ssid);
                    } else {
                        lv_label_set_text(full_txt, "\n正在连接 WiFi");
                    }
                } else {
                    lv_label_set_text(full_txt,
                        "\n"
                        "WiFi 连接失败\n"
                        "即将进入配网模式\n"
                        "请按提示操作配网");
                }
            }
            break;
        }
        case eDisp_Welcome:
            font_version = get_font_version();
            fw_version = get_fw_version();
            lv_label_set_text_fmt(full_txt,
                "   设备 ID: \n[%s]\n"
                "  固件版本: [%d.%d.%d]\n"
                "  字库版本: [%d.%d.%d]\n",
                get_device_id_str(),
                fw_version >> 16, (fw_version >> 8) & 0xFF, fw_version & 0xFF,
                font_version >> 16, (font_version >> 8) & 0xFF, font_version & 0xFF);
            break;
        case eDisp_Hand_Shaking:
            if (g_ota_check_connect_to_server_failed == 1) {
                lv_label_set_text_fmt(full_txt, "\n连接OTA服务器失败\n%s\n请检查网络后重试", get_ota_url());
            } else {
                lv_label_set_text_fmt(full_txt, "\n正在连接OTA服务器\n%s", get_ota_url());
            }
            break;
        case eDisp_Activation_Hint:
            if (disp_take_activation_hint_utf8(s_lvgl_pending_text_line, DISP_SHOW_TEXT_MAX_LEN)) {
                lv_obj_set_style_text_align(full_txt, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
                lv_label_set_text_fmt(full_txt,
                    "请登录主页:\n"
                    "%s\n"
                    "添加智能体和绑定设备\n"
                    "设备绑定码: %.8s\n"
                    "绑定完成后请重启设备\n",
                    get_server_url(),
                    s_lvgl_pending_text_line);
            }
            break;
        case eDisp_WiFi_ReConfig_Success:
            lv_label_set_text(full_txt, "\nWiFi 重新配网成功\n即将自动重启设备");
            break;

        #if defined(SUPPORT_LFS)
        case eDisp_Font_Dl_ReleaseBin:
            g_font_dl_ui_percent = 0;
            /* 释放 L:/system/font.bin 句柄，便于 WiFi 任务侧 lfs_rename / 删除旧文件 */
            xh_lv_font_bin_release_for_replace();
            font_download_notify_lvgl_released_bin();
            break;
        #endif

        case eDisp_Font_Dl_Progress: // 字库下载中：百分比由 g_font_dl_ui_percent（HTTP Content-Length）驱动
        #if defined(SUPPORT_LFS)
            if (g_font_dl_ui_percent <= 99U) {
                lv_label_set_text_fmt(full_txt, "\n正在下载字库: %d%%", g_font_dl_ui_percent);
            } else {
                lv_label_set_text(full_txt, "\n正在下载字库\n请等待…");
            }
        #endif
            break;

        case eDisp_Font_Dl_Complete:  // 字库下载完成（font_download.c 写入 marker 后投递）
            lv_label_set_text(full_txt, "\n字库下载完成\n即将自动重启设备");
            break;

        case eDisp_Font_Dl_Already:  // 字库已下载（仅标记已存在、跳过下载时）
            lv_label_set_text(full_txt, "\n字库已安装");
            break;

        case eDisp_Font_Dl_Restore: {    // 字库恢复（font_dl_ui_pause_then_restore 约 2s 后投递）
            wifi_st = get_wifi_status();
            if (wifi_st == eWifi_St_Connected) {
                lv_label_set_text(full_txt, "\n联网成功");
            } else if (wifi_st == eWifi_St_AutoConnecting) {
                lv_label_set_text(full_txt, "\n正在联网");
            } else {
                lv_label_set_text(full_txt, "\n联网失败\n请重启设备\n再按提示操作配网");
            }
            break;
        }
        case eDisp_Ota_FW_Update:{
            char version[32] = {0};
            get_ota_firmware_version_pending(version, sizeof(version));
            lv_label_set_text_fmt(full_txt, "\n检测到新固件\n[%s]", version);
            break;
        }
        case eDisp_Ota_FW_Update_Progress: {
            lv_label_set_text_fmt(full_txt, "\n正在下载固件: %d%%", g_ota_fw_update_progress);
            break;
        }
        case eDisp_Ota_FW_Update_Complete: {
            lv_label_set_text(full_txt, "\n固件下载完成\n即将重启设备\n固件升级完成后\n会自动开机");
            break;
        }
        case eDisp_Ota_FW_Update_Failed: {
            lv_label_set_text(full_txt, "\n固件升级失败\n请检查网络后重试\n或重启设备");
            break;
        }
        case eDisp_Debug_Font_Delete:
            lv_label_set_text(full_txt, "\n字库已删除\n联网后会重新下载");
            break;
        default:
            break;
    }
}