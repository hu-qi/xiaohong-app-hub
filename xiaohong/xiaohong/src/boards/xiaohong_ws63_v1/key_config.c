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
#include "key_config.h"
#include "log.h"
#include <stddef.h>


static key_event_callback_t send_key_event_cb = NULL;
uint8_t register_key_event_callback(key_event_callback_t func_cb)
{
    send_key_event_cb = func_cb;
    return ERRCODE_SUCC;
}
static void send_key_event(uint8_t event)
{
    if (send_key_event_cb) {
        send_key_event_cb(event);
    }
}

static void volup_key_event_cb(pin_t pin, uintptr_t param)
{
    (void)pin;
    (void)param;
    static uint32_t g_key_volup_time_begin = 0;
    if ((g_key_volup_time_begin == 0) && (0 == uapi_gpio_get_val(KEY_VOLUP_PIN))) {
        g_key_volup_time_begin = uapi_tcxo_get_ms();
    } else {
        if (g_key_volup_time_begin != 0) {
            uint32_t g_key_volup_time_end = uapi_tcxo_get_ms();
            if ((g_key_volup_time_end - g_key_volup_time_begin) >= KEY_LONG_PRESS_INTERVAL) {
                log_info("[KeyConfg] VolUpKey LongPress:   [%d]\r\n", eKey_VolUp_LongPressed);
                send_key_event(eKey_VolUp_LongPressed);
            } else {
                log_info("[KeyConfg] VolUpKey ShortPress:  [%d]\r\n", eKey_VolUp_ShortPressed);
                send_key_event(eKey_VolUp_ShortPressed);
            }
            g_key_volup_time_begin = 0;
        }
    }
}

static void voldw_key_event_cb(pin_t pin, uintptr_t param)
{
    (void)pin;
    (void)param;
    static uint32_t g_key_voldw_time_begin = 0;
    if ((g_key_voldw_time_begin == 0) && (0 == uapi_gpio_get_val(KEY_VOLDW_PIN))) {
        g_key_voldw_time_begin = uapi_tcxo_get_ms();
    } else {
        if (g_key_voldw_time_begin != 0) {
            uint32_t g_key_voldw_time_end = uapi_tcxo_get_ms();
            if ((g_key_voldw_time_end - g_key_voldw_time_begin) >= KEY_LONG_PRESS_INTERVAL) {
                log_info("[KeyConfg] VolDwKey LongPress:   [%d]\r\n", eKey_VolDw_LongPressed);
                send_key_event(eKey_VolDw_LongPressed);
            } else {
                log_info("[KeyConfg] VolDwKey ShortPress:  [%d]\r\n", eKey_VolDw_ShortPressed);
                send_key_event(eKey_VolDw_ShortPressed);
            }
            g_key_voldw_time_begin = 0;
        }
    }
}

static void wakeup_key_event_cb(pin_t pin, uintptr_t param)
{
    (void)pin;
    (void)param;
    static uint32_t g_key_wakeup_time_begin = 0;
    if ((g_key_wakeup_time_begin == 0) && (0 == uapi_gpio_get_val(KEY_WAKEUP_PIN))) {
        g_key_wakeup_time_begin = uapi_tcxo_get_ms();
    } else {
      if (g_key_wakeup_time_begin != 0) {
        uint32_t g_key_wakeup_time_end = uapi_tcxo_get_ms();
        if ((g_key_wakeup_time_end - g_key_wakeup_time_begin) >= KEY_LONG_PRESS_INTERVAL_5S) {
            log_info("[KeyConfg] WakupKey LongPress_5s:[%d]\r\n", eKey_Wakeup_LongPressed_5S);
            send_key_event(eKey_Wakeup_LongPressed_5S);
        } else if ((g_key_wakeup_time_end - g_key_wakeup_time_begin) >= KEY_LONG_PRESS_INTERVAL) {
            log_info("[KeyConfg] WakupKey LongPress:   [%d]\r\n", eKey_Wakeup_LongPressed);
            send_key_event(eKey_Wakeup_LongPressed);
        } else {
            /* Wakeup double-click: two short presses within 400ms = toggle App Hub */
            static uint32_t last_wakeup_short_ms = 0;
            uint32_t now_ms = uapi_tcxo_get_ms();
            if (last_wakeup_short_ms != 0 && (now_ms - last_wakeup_short_ms) < 400U) {
                log_info("[KeyConfg] WakupKey DoubleClick: [%d]\r\n", eKey_AppHub_Toggle);
                send_key_event(eKey_AppHub_Toggle);
                last_wakeup_short_ms = 0;
            } else {
                log_info("[KeyConfg] WakupKey ShortPress:  [%d]\r\n", eKey_Wakeup_ShortPressed);
                send_key_event(eKey_Wakeup_ShortPressed);
                last_wakeup_short_ms = now_ms;
            }
        }
        g_key_wakeup_time_begin = 0;
      }
    }
}

errcode_t key_init(void)
{
    uapi_pin_set_mode(KEY_VOLUP_PIN, 0);
    uapi_gpio_set_dir(KEY_VOLUP_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(KEY_VOLUP_PIN, 1);
    uapi_gpio_register_isr_func(KEY_VOLUP_PIN, GPIO_INTERRUPT_DEDGE, volup_key_event_cb);

    uapi_pin_set_mode(KEY_VOLDW_PIN, 0);
    uapi_gpio_set_dir(KEY_VOLDW_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(KEY_VOLDW_PIN, 1);
    uapi_gpio_register_isr_func(KEY_VOLDW_PIN, GPIO_INTERRUPT_DEDGE, voldw_key_event_cb);

    uapi_pin_set_mode(KEY_WAKEUP_PIN, 0);
    uapi_gpio_set_dir(KEY_WAKEUP_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(KEY_WAKEUP_PIN, 1);
    uapi_gpio_register_isr_func(KEY_WAKEUP_PIN, GPIO_INTERRUPT_DEDGE, wakeup_key_event_cb);

    return ERRCODE_SUCC;
}
