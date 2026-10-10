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
//#include "net_wifi_config.h"
#include "settings.h"
#include "lvgl.h"
#include "lvgl_ui_layout.h"

#define LOG_TAG  "[LvglTask]"

/* LVGL 局部刷新块高度（行），一般为屏高的 1/4～1/2（如 240 屏用 48～120） */
  #define LVGL_PARTIAL_BUF_LINES         (40)  // 240*80*2=19200Byte=18KB Buffer
//#define LVGL_PARTIAL_BUF_LINES         (80)  // 240*80*2=38400Byte=36KB Buffer
static uint8_t disp_buffer[DISP_WIDTH * LVGL_PARTIAL_BUF_LINES * 2]; /* RGB565 partial, see disp_driver.h */


#define USE_AI_VERSION  (1)
#if USE_AI_VERSION
/**
 * LVGL 的 tick 必须在调用 lv_timer_handler() 的同一线程/上下文中推进。
 * 在硬件定时器 ISR 里调用 lv_tick_inc() 会与 LvglTask 并发，破坏 LVGL 内部状态（典型 fault：mtval≈0x20、a0=0）。
 */
static uint32_t lvgl_elapsed_ms_since(uint32_t *prev_tick)
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



//显示屏画面旋转配置，需要结合 lcd_st7789_driver.c 的 lcd_set_rotation() 来旋转画面:
//       正向 0°: 原始坐标 + MADCTL[0x00] OK  无旋转
// 顺时针旋转 90°: 原始坐标 + MADCTL[0x60] OK  硬件旋转正常
// 顺时针旋转180°: 原始坐标 + MADCTL[0xC0] NG, 坐标旋转 + MADCTL[0xC0] NG ==>> 硬件旋转异常
// 顺时针旋转180°: 坐标旋转 + 像素数据顺序旋转 + MADCTL[0x00] OK ==>> 放弃硬件旋转，使用软件旋转
// 顺时针旋转270°: 原始坐标 + MADCTL[0xA0] NG, 坐标旋转 + MADCTL[0xA0] NG ==>> 硬件旋转异常
// 顺时针旋转270°: 坐标旋转 + 像素数据顺序旋转 + MADCTL[0x60] OK ==>> 软件旋转180度 + 硬件旋转90度
void disp_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
    uint16_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    uint32_t pixel_cnt = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    if ((get_rotation() == LCD_PANEL_ROTATION_180) ||
        (get_rotation() == LCD_PANEL_ROTATION_270)) {
        /* 180°/270°: 坐标旋转，MADCTL 硬件在窗口内局部不旋转/旋转90° */
        /* flip the window position to the opposite corner (180°) */
        x1 = DISP_WIDTH  - 1 - area->x2;
        y1 = DISP_HEIGHT - 1 - area->y2;
        x2 = DISP_WIDTH  - 1 - area->x1;
        y2 = DISP_HEIGHT - 1 - area->y1;

        /* reverse pixel data order (MADCTL=0x00, so we do it in software) */
        for (uint32_t i = 0; i < pixel_cnt / 2; i++) {
            uint32_t j = pixel_cnt - 1 - i;
            uint8_t tmp_l = px_map[i * 2];
            uint8_t tmp_h = px_map[i * 2 + 1];
            px_map[i * 2]     = px_map[j * 2];
            px_map[i * 2 + 1] = px_map[j * 2 + 1];
            px_map[j * 2]     = tmp_l;
            px_map[j * 2 + 1] = tmp_h;
        }
    } else {
        /* 0°/90°: 原始坐标，MADCTL 硬件在窗口内局部旋转 */
        x1 = area->x1;
        y1 = area->y1;
        x2 = area->x2;
        y2 = area->y2;
    }

    disp_set_region(x1, y1, x2, y2);
    disp_write_buff(LCD_DAT, px_map, pixel_cnt*2);
    lv_display_flush_ready(disp_drv);
}

void LvglTask(void)
{
    osStatus_t ret = osError;
    uint8_t msg_recv = 0;

    disp_init_panel();
    disp_set_rotation(get_rotation());
    #if !USE_AI_VERSION
    disp_timer_config();
    #endif

    lv_init();

    lv_display_t* disp_obj = lv_display_create(DISP_WIDTH, DISP_HEIGHT);
    lv_display_set_flush_cb(disp_obj, disp_flush_cb);
    lv_display_set_buffers(disp_obj, disp_buffer, NULL, sizeof(disp_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

    disp_create_ui();
    disp_set_backlight_level(get_blk_level());

//  disp_clear(WHITE);

    #if USE_AI_VERSION
    uint32_t lv_prev_kernel_tick = osKernelGetTickCount();
    #endif

    disp_update_ui(eDisp_All_Update);

    while (1) {
    #if USE_AI_VERSION
        lv_tick_inc(lvgl_elapsed_ms_since(&lv_prev_kernel_tick));
        /* 返回值 = 距下一次 lv_timer 的毫秒数；用于队列等待上限，便于让出 CPU、喂狗，避免 LvglTask 长时间占满 */
        uint32_t next_ms = lv_timer_handler();

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
        1024 * 16,        // 流式字库 + LVGL 绘制链较深；过小会 NMI、fp 回溯失败，异常表里 sp 会落到栈区外
        24,              // priority
        (void*)LvglTask  // func
    };

    return create_task(&task_conf, NULL);
}

void disp_update_ui(uint8_t cmd)
{
    static uint8_t current_page = eDisp_Show_Main_Page;
    disp_update_ui_status();
    switch (cmd) {
        case eDisp_Blk_On:
            /* 唤醒亮屏：恢复背光 */
            disp_set_backlight_level(get_blk_level());
            break;
        case eDisp_Blk_Off:
            /* 息屏省电：关闭背光（PWM 输出 0） */
            disp_set_backlight_level(0);
            break;
        case eDisp_All_Update:{
            post_msg_event(g_disp_event_qid, current_page);
            break;
        }

        case eDisp_Set_Rotation: {
            disp_set_rotation(get_rotation());
            lv_obj_invalidate(lv_scr_act());       /* 标记整个屏幕为"脏"，触发重绘 */
            lv_refr_now(NULL);                     /* 立即刷新 */
            break;
        }
// 开机欢迎页面
        case eDisp_Show_Main_Page: {
            current_page = eDisp_Show_Main_Page;
            disp_create_main_page();
            break;
        }

// 本地模式主页面
        case eDisp_Show_Local_Page: {
            if (current_page == eDisp_Show_KeyRemap_Page) {
                disable_key_remap();
            }
            current_page = eDisp_Show_Local_Page;
            disp_create_local_page();
            break;
        }
// 设置页面
        case eDisp_Show_Setting_Page: {
            current_page = eDisp_Show_Setting_Page;
            disp_create_settings_page();
            break;
        }
// 按键重映射配置页面
        case eDisp_Show_KeyRemap_Page: {
            current_page = eDisp_Show_KeyRemap_Page;
            disp_create_key_remap_page();
            break;
        }
// WiFi配置页面
        case eDisp_Show_WiFi_Page: {
            current_page = eDisp_Show_WiFi_Page;
            disp_create_wifi_page();
            break;
        }

// 正常模式主页面
        case eDisp_Show_Normal_Page: {
            if (current_page == eDisp_Show_KeyRemap_Page) {
                disable_key_remap();
            }
            current_page = eDisp_Show_Normal_Page;
            disp_create_normal_page();
            break;
        }
// SLE配对页面
        case eDisp_Show_Sle_Pairing_Page: {
            if (current_page == eDisp_Show_KeyRemap_Page) {
                disable_key_remap();
            }
            current_page = eDisp_Show_Sle_Pairing_Page;
            disp_create_sle_scan_pairing_page();
            break;
        }


        case eDisp_HiLi_Up: {
            if (current_page == eDisp_Show_Local_Page) {
                disp_local_page_cursor_move(-1);
            } else if (current_page == eDisp_Show_Sle_Pairing_Page) {
                disp_sle_scan_pairing_cursor_move(-1);
            } else if (current_page == eDisp_Show_Setting_Page) {
                disp_settings_cursor_move(-1);
            } else if (current_page == eDisp_Show_KeyRemap_Page) {
                disp_key_remap_cursor_move(-1);
            }
            break;
        }
        case eDisp_HiLi_Down: {
            if (current_page == eDisp_Show_Local_Page) {
                disp_local_page_cursor_move(1);
            } else if (current_page == eDisp_Show_Sle_Pairing_Page) {
                disp_sle_scan_pairing_cursor_move(1);
            } else if (current_page == eDisp_Show_Setting_Page) {
                disp_settings_cursor_move(1);
            } else if (current_page == eDisp_Show_KeyRemap_Page) {
                disp_key_remap_cursor_move(1);
            }
            break;
        }
        case eDisp_HiLi_Confirm: {
            if (current_page == eDisp_Show_Local_Page) {
                disp_local_page_confirm();
            } else if (current_page == eDisp_Show_Sle_Pairing_Page) {
                disp_sle_scan_pairing_confirm();
            } else if (current_page == eDisp_Show_Setting_Page) {
                disp_settings_confirm();
            }
            break;
        }

        case eDisp_LowBattery: {
            /* 低电量：全屏文本区显示充电提示 */
            //lv_label_set_text_fmt(full_txt_obj, "电量低\n请充电");
            break;
        }
        case eDisp_LowBatteryClear: {
            //lv_label_set_text_fmt(full_txt_obj, "小鸿\nVibeCoding\nKeyBorad");
            break;
        }
        default:
            break;
    }
}