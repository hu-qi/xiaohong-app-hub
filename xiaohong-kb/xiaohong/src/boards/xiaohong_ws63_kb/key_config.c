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

#include "board_config.h"
#include "disp_driver.h"
#include "key_config.h"
#include "task_entry.h"
#include "settings.h"
#include "log.h"
#include "sle_client.h"

errcode_t key_init(void)
{
    uapi_pin_set_mode(KEY_MIC_PIN, 0);
    uapi_gpio_set_dir(KEY_MIC_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(KEY_MIC_PIN, 1);

    uapi_pin_set_mode(KEY_ALWAYS_PIN, 0);
    uapi_gpio_set_dir(KEY_ALWAYS_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(KEY_ALWAYS_PIN, 1);

    return ERRCODE_SUCC;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 0-release[33xx or other], 1-No[<10], 2-Yes[16xx], 3-Down[22xx], 4-Up[24xx]
static uint8_t get_adc_key(uint16_t level)
{
    uint8_t key = 0;
    if (level < 50) {
        key = eKey_No;        // ~5~
    } else if ((1500 < level) && (level < 1800)) {
        key = eKey_Yes;       // ~1665~
    } else if ((2100 < level) && (level < 2350)) {
        key = eKey_Down;      // ~2220~
    } else if ((2350 < level) && (level < 2600)) {
        key = eKey_Up;        // ~2495~
    } else {
        key = eKey_Released;  // ~3300~
    }

    return key;
}

static uint8_t get_combination_key(uint8_t key_mic, uint8_t key_alw, uint8_t key_adc)
{
    if (key_mic) {
        return eKey_Mic;    // Mic不与其他键组合，只要Mic键被按下，就忽略其它按键
    }

    if (key_alw) {
        switch (key_adc) {
            case eKey_No:
                return eKey_Always_No;      // 同时按下always键和No键
            case eKey_Yes:
                return eKey_Always_Yes;     // 同时按下always键和Yes键
            case eKey_Up:
                return eKey_Always_Up;      // 同时按下always键和PageUp键
            case eKey_Down:
                return eKey_Always_Down;    // 同时按下always键和PageDown键
            default:
                return eKey_Always;
        }
    } else {
        return key_adc;
    }
}

void KeysTask(void)
{
    char* strKey[] = {
        "--------",    // 释放键
           "Mic",  "Always",     "No",      "Yes", "Up", "Down",  // 6个单键
         "Al+No",  "Al+Yes",  "Al+Up",  "Al+Down",                // 有定义的四个组合键
        "Mic+No", "Mic+Yes", "Mic+Up", "Mic+Down", "Max"          // 待定义的四个组合键
    };

    uint16_t key_adc_level = 0;
    uint8_t  key_adc = 0;      // 0-release[33xx or other], 1-No[<10], 2-Yes[16xx], 3-Up[22xx], 4-Down[24xx]
    uint8_t  key_mic = 0;      // 0-key mic released,    1- key mic pressed
    uint8_t  key_alw = 0;      // 0-key always released, 1-key always pressed

    uint8_t  key_old = 0;      // combined key old
    uint8_t  key_new = 0;      // combined key new
    uint8_t  combined_key_release_once = 0;
    const uint32_t scan_interval = 50;  // 50ms

    // 空闲自动休眠：无按键操作超过 IDLE_SLEEP_TICKS 后息屏省电，按键按下自动唤醒
    uint32_t idle_sleep_ticks = (uint32_t) get_sleep_time()*1000/scan_interval;
    uint32_t idle_ticks = 0;
    uint8_t  screen_off = 0;

    // 按键扫描任务，只发送按键消息给Key_Dispatch任务去处理，由它转发给其他任务做特定事情
    while (1) {
        if (screen_off) {
            usleep(300*1000);    // 息屏后降低按键扫描频率，进一步省电
        } else {
            usleep(scan_interval*1000);    // 扫描键盘的时间间隔
        }

        key_mic = (uapi_gpio_get_val(KEY_MIC_PIN)>0)?(0):(1);       // GPIO[ 7] 默认悬空，高电平；按下接地，低电平
        key_alw = (uapi_gpio_get_val(KEY_ALWAYS_PIN)>0)?(0):(1);    // GPIO[13] 默认悬空，高电平；按下接地，低电平

        adc_port_read(KEY_ADC_PIN_ADC_CHN, &key_adc_level);         // ADC读数
        key_adc = get_adc_key(key_adc_level);                       // ADC按键
        key_new = get_combination_key(key_mic, key_alw, key_adc);

        /* 空闲休眠检测：无任何按键按下时累计空闲时长，超时后息屏；有按键时唤醒亮屏 */
        if ((idle_sleep_ticks) && (key_new == eKey_Released)) {
            if (idle_ticks < 0xFFFFFFFFUL) {
                idle_ticks++;
            }
            if (!screen_off && (idle_ticks >= idle_sleep_ticks)) {
                screen_off = 1;
                post_msg_event(g_disp_event_qid, eDisp_Blk_Off);   // 息屏
                log_debug("[KeyConfg] idle timeout[%d], screen off\r\n", idle_sleep_ticks);
            }
        } else {
            idle_ticks = 0;                                        // 有按键，重置空闲计数
            idle_sleep_ticks = (uint32_t) get_sleep_time()*1000/scan_interval;
            if (screen_off) {
                screen_off = 0;
                post_msg_event(g_disp_event_qid, eDisp_Blk_On);    // 唤醒亮屏
                log_debug("[KeyConfg] key pressed, screen on\r\n");
            }
        }

        if (key_old != key_new) {
    #if 0
            log_debug("[KeyConfg] Mic[%d] Always[%d] adc_lvl[%4d]->key[%X][%+8s] ==>> [%X][%+8s]->[%X][%+8s]\r\n",
                key_mic, key_alw, key_adc_level, key_adc, strKey[(key_adc>=eKey_Max)?(eKey_Max):(key_adc)],
                key_old, strKey[(key_old>=eKey_Max)?(eKey_Max):(key_old)],
                key_new, strKey[(key_new>=eKey_Max)?(eKey_Max):(key_new)]);
    #endif
            if (key_new == eKey_Mic) {
                // Mic 键按下，持续按下状态，给PC拾音转文字
                post_msg_event(g_kdsp_event_qid, eKey_Mic);
            } else if ((key_old == eKey_Mic) && (key_mic == 0)) {
                // Mic 键释放，发送释放按键，PC停止拾音，完成转文字
                post_msg_event(g_kdsp_event_qid, eKey_Released);

                key_old = eKey_Released;  //恢复释放状态
                continue;
            } else if((key_old >= eKey_Always_No) && (key_new < eKey_Always_No)) {
                // 组合键->单键->松开，松开任一健后发送组合键键码+释放键码
                post_msg_event(g_kdsp_event_qid, key_old);
                combined_key_release_once = 1;
            } else if (key_new == eKey_Released) {
                if (combined_key_release_once) {    // 组合键已经release一次，第二次释放，不发送'a'键
                    combined_key_release_once = 0;
                } else {                            // 非Mic的单键按下，松开后发送按键键码+释放键码
                    post_msg_event(g_kdsp_event_qid, key_old);
                    usleep(300*1000);
                    post_msg_event(g_kdsp_event_qid, key_new);
                }
            }
            key_old = key_new;
        }
    }
}

uint32_t KeysTaskEntry(void)
{
    task_config_t task_conf = {
        "KeysTask",      // task name
        1024*4,          // stack_size
        24,              // priority
        (void*)KeysTask  // func
    };

    return create_task(&task_conf, NULL);
}