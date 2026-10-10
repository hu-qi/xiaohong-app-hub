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
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "errcode.h"
#include "common_def.h"
#include "systick.h"
#include "soc_osal.h"
#include "interrupt/osal_interrupt.h"

#include "pinctrl.h"
#include "gpio.h"
#include "hal_gpio.h"
#include "hal_gpio_v150_comm.h"
#include "hal_gpio_v150_regs_op.h"

#include "log.h"
#include "task_entry.h"
#include "board_config.h"
#include "settings.h"
#include "led_config.h"

#define LOG_TAG  "[led_config]"

// 每颗灯珠3-Bytes[GRB/RGB]控制数据 * LED_WS2812_NUM
#define LED_WS2812_BUFF_SIZE   (3u * LED_WS2812_NUM)

/* 短按循环：0 关闭，1-冷白， 2-暖白 3-三灯三色 */
#define WS2812_SCENE_COUNT   4

static uint32_t s_ch;
static uint32_t s_group;
static uint32_t s_gpin;
static pin_t    s_pin;
static bool     s_cache_ok;

/* 返回 true=已缓存寄存器，发码走快速路径
   返回 false=会退回 hal_gpio_output(易白) */
bool ws2812_din_gpio_cache_init(pin_t pin)
{
    errcode_t e;
    s_pin = pin;
    e = hal_gpio_v150_pin_info_get(pin, &s_ch, &s_group, &s_gpin);
    s_cache_ok = (e == ERRCODE_SUCC);
    return s_cache_ok;
}

void ws2812_din_set_fast(gpio_level_t level)
{
    if (!s_cache_ok) {
        (void)hal_gpio_output(s_pin, level);
        return;
    }
    if (level == GPIO_LEVEL_LOW) {
        hal_gpio_gpio_data_clr_set_bit(s_ch, s_group, s_gpin, 1U);
    } else {
        hal_gpio_gpio_data_set_set_bit(s_ch, s_group, s_gpin, 1U);
    }
}


static inline void ws2812_din_set_level(gpio_level_t lv)
{
    ws2812_din_set_fast(lv);
}

#if LED_WS2812_TIMING_RDCYCLE
static inline uint32_t ws2812_rdcycle32(void)
{
    uint32_t c;
    __asm__ __volatile__("rdcycle %0" : "=r"(c));
    return c;
}

static inline void ws2812_wait_cycles_from(uint32_t t0, uint32_t cyc)
{
    while ((ws2812_rdcycle32() - t0) < cyc) {
    }
}
#endif
#if !LED_WS2812_TIMING_RDCYCLE
static void pin_delay_nops(volatile uint32_t n)
{
    while (n-- > 0) {
        __asm__ __volatile__("nop; nop" ::: "memory");
    }
}
#endif

static inline void send_bit0(void)
{
#if LED_WS2812_TIMING_RDCYCLE
    uint32_t t;
    ws2812_din_set_level(GPIO_LEVEL_HIGH);
    t = ws2812_rdcycle32();
    ws2812_wait_cycles_from(t, WS2812_CYC_T0H);
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    t = ws2812_rdcycle32();
    ws2812_wait_cycles_from(t, WS2812_CYC_T0L);
#else
    ws2812_din_set_level(GPIO_LEVEL_HIGH);
    pin_delay_nops(WS2812_T0H_NOPS);
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    pin_delay_nops(WS2812_T0L_NOPS);
#endif
}

static inline void send_bit1(void)
{
#if LED_WS2812_TIMING_RDCYCLE
    uint32_t t;
    ws2812_din_set_level(GPIO_LEVEL_HIGH);
    t = ws2812_rdcycle32();
    ws2812_wait_cycles_from(t, WS2812_CYC_T1H);
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    t = ws2812_rdcycle32();
    ws2812_wait_cycles_from(t, WS2812_CYC_T1L);
#else
    ws2812_din_set_level(GPIO_LEVEL_HIGH);
    pin_delay_nops(WS2812_T1H_NOPS);
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    pin_delay_nops(WS2812_T1L_NOPS);
#endif
}

static void send_byte_msb_first(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        if ((b & (1u << (uint32_t)i)) != 0) {
            send_bit1();
        } else {
            send_bit0();
        }
    }
}

static void pixel_set(uint8_t *buf, uint32_t idx, uint8_t g, uint8_t r, uint8_t b)
{
    if (idx >= (uint32_t)LED_WS2812_NUM) {
        return;
    }
    uint32_t o = idx * 3u;

#if LED_WS2812_WIRE_RGB
    buf[o + 0u] = LED_DIM(r);
    buf[o + 1u] = LED_DIM(g);
    buf[o + 2u] = LED_DIM(b);
#else
    buf[o + 0u] = LED_DIM(g);
    buf[o + 1u] = LED_DIM(r);
    buf[o + 2u] = LED_DIM(b);
#endif
    log_debug("pixel_set: idx[%d]: R[0x%02X] G[0x%02X] B[0x%02X]\r\n", idx, LED_DIM(r), LED_DIM(g), LED_DIM(b));
}

static void strip_show(const uint8_t *grb, uint32_t len)
{
    uint32_t il = osal_irq_lock();
    for (uint32_t i = 0; i < len; i++) {
        send_byte_msb_first(grb[i]);
    }
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    osal_irq_restore(il);
    osal_udelay(LED_WS2812_RESET_US);
}

/* 连续打两帧同色：首颗 IC 偶发只锁存半帧或受上一 RESET 影响时，双帧可消除「第一颗恒绿/与后面色不一致」 */
static void strip_show_twice(const uint8_t *grb, uint32_t len)
{
    strip_show(grb, len);
    strip_show(grb, len);
}

/* 发全黑帧清链；上电或切场景前多打几帧可减少误锁存导致的「单路发绿」 */
static void ws2812_show_black_burst(unsigned times)
{
    uint8_t data[LED_WS2812_BUFF_SIZE] = {0};
    memset(data, 0, LED_WS2812_BUFF_SIZE);
    while (times-- > 0U) {
        strip_show(data, LED_WS2812_BUFF_SIZE);
        osal_udelay(50);
    }
}

static void led_ws2812_din_gpio_init(void)
{
    uapi_pin_set_mode(LED_WS2812_DI_PIN, LED_WS2812_DI_PIN_MODE);
    gpio_select_core(LED_WS2812_DI_PIN, CORES_APPS_CORE);
    uapi_pin_set_ds(LED_WS2812_DI_PIN, PIN_DS_7);          // 设置管脚驱动能力第7级
    uapi_gpio_set_dir(LED_WS2812_DI_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LED_WS2812_DI_PIN, GPIO_LEVEL_LOW);

    if (ws2812_din_gpio_cache_init(LED_WS2812_DI_PIN)) {
        log_info("%s %s: din pin cache OK\r\n", LOG_TAG, __func__);
    } else {
        log_error("%s %s: din pin cache NG\r\n", LOG_TAG, __func__);
    }
}

/* 逻辑灯序 0..N-1 映射到链上物理下标（近 DIN 为 0） */
static uint8_t ws2812_phys_led_i(uint8_t logical_i)
{
    uint8_t n = (uint8_t)LED_WS2812_NUM;
    if (logical_i >= n) {
        return 0;
    }
#if LED_WS2812_REVERSE
    return (uint8_t)(n - 1U - logical_i);
#else
    return logical_i;
#endif
}

static void LedsTask(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint8_t msg_recv_curr = 0, msg_recv_prev = 0;
    uint8_t buf[LED_WS2812_BUFF_SIZE];

    uapi_pin_init();
    uapi_gpio_init();
    uapi_systick_init();

    led_ws2812_din_gpio_init();

    /* DIN 先长时间低再发全黑，清上电毛刺/误锁存（否则易出现某颗 G 异常亮） */
    ws2812_din_set_level(GPIO_LEVEL_LOW);
    osal_udelay(400);
    ws2812_show_black_burst(4U);

    memset(buf, 0, LED_WS2812_BUFF_SIZE);
    strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
    osal_msleep(150);

    log_info("%s %s: WS2812 DIN=GPIO%u, leds=%u, reverse=%d, wire=%s\r\n",
           LOG_TAG, __func__,
           (unsigned)LED_WS2812_DI_PIN, (unsigned)LED_WS2812_NUM, (int)LED_WS2812_REVERSE,
           (LED_WS2812_WIRE_RGB != 0) ? "RGB" : "GRB");

    while (1) {
        ret = osMessageQueueGet(g_leds_event_qid, (void *)&msg_recv_curr, NULL, 40);
        if (ret != osOK) {
            msg_recv_curr = 0;
        #if 1  // 对AtomCode Status事件持续/反复刷新LED状态，达到闪烁的效果
            if ((eLed_AtomCode_Idle <= msg_recv_prev) && (msg_recv_prev <= eLed_AtomCode_Error)) {
                msg_recv_curr = msg_recv_prev;
            } else
        #endif
            {
                continue;
            }
        }

        if (msg_recv_prev != msg_recv_curr) {
            log_info("%s %s: msg_recv_prev[%d] ====>>>> msg_recv_curr[%d]\r\n",
                LOG_TAG, __func__, msg_recv_prev, msg_recv_curr);
        }

        if (eLed_Refresh != msg_recv_curr) {
            msg_recv_prev = msg_recv_curr;
        }
        switch (msg_recv_curr) {
            case eLed_AllOff: {
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                break;
            }
            case eLed_AllOn: {
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                  //pixel_set(buf, ws2812_phys_led_i(i), 40u, 52u, 50u);   /* 冷白：略压 G，减轻廉价灯珠「发白偏绿」 */
                  //pixel_set(buf, ws2812_phys_led_i(i), 22u, 96u, 10u);   /* 暖白：红强、压低绿/蓝 */
                    pixel_set(buf, ws2812_phys_led_i(i), 255u, 255u, 255u);
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                break;
            }
            case eLed_Refresh: {      // 刷新所有灯珠
                if (msg_recv_prev == eLed_AllOff) {
                    post_msg_event(g_leds_event_qid, eLed_AllOn);
                } else {
                    post_msg_event(g_leds_event_qid, msg_recv_prev);
                }
                break;
            }
/////////////////////////////////////
// AtomCode Status
            case eLed_AtomCode_Idle: {  // 绿色慢闪烁
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);             // 熄灭
                osal_msleep(800);

                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                    pixel_set(buf, ws2812_phys_led_i(i), 255u, 0u, 0u);  // 绿色
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                osal_msleep(800);
                break;
            }
            case eLed_AtomCode_Thinking: {  // 黄色中速闪烁
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);               // 熄灭
                osal_msleep(400);

                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                    pixel_set(buf, ws2812_phys_led_i(i), 255u, 255u, 0u);  // 黄色
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                osal_msleep(400);
                break;
            }
            case eLed_AtomCode_Working: {  // 蓝色快闪烁
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);               // 熄灭
                osal_msleep(200);

                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                    pixel_set(buf, ws2812_phys_led_i(i), 0u, 0u, 255u);    // 蓝色
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                osal_msleep(200);
                break;
            }
            case eLed_AtomCode_Waiting_Confirm: {  // 紫色中速闪烁
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);               // 熄灭
                osal_msleep(400);

                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                    pixel_set(buf, ws2812_phys_led_i(i), 255u, 0u, 255u);  // 紫色
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                osal_msleep(400);
                break;
            }
            case eLed_AtomCode_Error: {  // 红色常亮
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                for (uint8_t i = 0; i < LED_WS2812_NUM; i++) {
                    pixel_set(buf, ws2812_phys_led_i(i), 0u, 255u, 0u);    // 红色
                }
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                break;
            }
// AtomCode Status
/////////////////////////////////////

            case eLed_LowBattery:
                /* 低电量告警：红色闪烁 3 次 */
                for (uint8_t i = 0; i < 3U; i++) {
                    memset(buf, 0, LED_WS2812_BUFF_SIZE);
                    strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                    osal_msleep(100);

                    for (uint8_t j = 0; j < (uint8_t)LED_WS2812_NUM; j++) {
                        pixel_set(buf, ws2812_phys_led_i(j), 0u, 255u, 0u);  // 红
                    }
                    strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                    osal_msleep(100);
                }
                break;
            default:    // default Off Leds
                memset(buf, 0, LED_WS2812_BUFF_SIZE);
                strip_show_twice(buf, LED_WS2812_BUFF_SIZE);
                break;
        }
    }
}

uint32_t LedsTaskEntry(void)
{
    task_config_t task_conf = {
        "LedsTask",      // task name
        1024*2,          // stack_size
        24,              // priority
        (void*)LedsTask  // func
    };

    return create_task(&task_conf, NULL);
}