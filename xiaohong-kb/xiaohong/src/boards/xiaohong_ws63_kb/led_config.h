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

#ifndef __LED_CONFIG_WS2812_H__
#define __LED_CONFIG_WS2812_H__

#include "board_config.h"

// LED控制管脚
#ifndef LED_WS2812_DI_PIN
#define LED_WS2812_DI_PIN       LED_DI_PIN    // see "board_config.h" => GPIO-0
#define LED_WS2812_DI_PIN_MODE  (0)           // GPIO-0 Mode 0
#endif

// LED灯珠数量：下[首颗]中[中间]上[末颗]
#ifndef LED_WS2812_NUM
#define LED_WS2812_NUM       LED_NUM       // see "board_config.h" => 3
#endif

/* 手册≥50µs；不少 clone 需 200µs+，偏短易残留上一帧或误解析成「发绿」；
 * 首颗对 RESET/首字节更敏感，略加长可减少「仅第一颗偏绿/色错位」 */
#ifndef LED_WS2812_RESET_US
#define LED_WS2812_RESET_US  380U
#endif

// 灯带丝印/视线顺序与“数据从DIN进先到达的IC”顺序是否相反，[1]相反，点亮位置左右对调
#ifndef LED_WS2812_REVERSE
#define LED_WS2812_REVERSE   0
#endif

// 控制数据每灯3字节的顺序: [0]WS2812标准GRB, [1]部分模组为RGB
#ifndef LED_WS2812_WIRE_RGB
#define LED_WS2812_WIRE_RGB  0
#endif

// MCU/CPU 的主频率(Hz)。
// 发码 rdcycle 用；若仍常白可试 40000000/160000000/48000000
#ifndef LED_WS2812_CPU_HZ
#define LED_WS2812_CPU_HZ 80000000U
#endif

// [1]用rdcycle定 0/1 脉宽(推荐); [0]用 nop(依赖猜主频,易全判为1->常白)
#ifndef LED_WS2812_USE_RDCYCLE
#define LED_WS2812_USE_RDCYCLE 1
#endif

#if (LED_WS2812_USE_RDCYCLE) && (defined(__riscv) || defined(__riscv__) || defined(__RISCV))
    #define LED_WS2812_TIMING_RDCYCLE 1

    #define WS2812_NS_TO_CYC(ns)                                                                           \
    ((uint32_t)((((uint64_t)LED_WS2812_CPU_HZ * (uint64_t)(ns) + 500000000ULL) / 1000000000ULL) < 1u       \
            ? 1u                                                                                           \
            : (uint32_t)(((uint64_t)LED_WS2812_CPU_HZ * (uint64_t)(ns) + 500000000ULL) / 1000000000ULL)))

    #define WS2812_NS_T0H   300U
    #define WS2812_NS_T0L   600U
    #define WS2812_NS_T1H   600U
    #define WS2812_NS_T1L   300U
    #define WS2812_CYC_T0H  WS2812_NS_TO_CYC(WS2812_NS_T0H)
    #define WS2812_CYC_T0L  WS2812_NS_TO_CYC(WS2812_NS_T0L)
    #define WS2812_CYC_T1H  WS2812_NS_TO_CYC(WS2812_NS_T1H)
    #define WS2812_CYC_T1L  WS2812_NS_TO_CYC(WS2812_NS_T1L)

#else
    #define LED_WS2812_TIMING_RDCYCLE 0
    #ifndef WS2812_TIME_SCALE
    #define WS2812_TIME_SCALE 1u
    #endif

    #define WS2812_T0H_BASE   4u
    #define WS2812_T0L_BASE   20u
    #define WS2812_T1H_BASE   18u
    #define WS2812_T1L_BASE   6u
    #define WS2812_T0H_NOPS   (WS2812_T0H_BASE * WS2812_TIME_SCALE)
    #define WS2812_T0L_NOPS   (WS2812_T0L_BASE * WS2812_TIME_SCALE)
    #define WS2812_T1H_NOPS   (WS2812_T1H_BASE * WS2812_TIME_SCALE)
    #define WS2812_T1L_NOPS   (WS2812_T1L_BASE * WS2812_TIME_SCALE)
#endif


// LED灯的亮度跟随显示屏的亮度百分比, 但100%还是太亮了，除以500比较合适
// 显示屏亮度为 1 时，LED灯会熄灭，+2 可以避免熄灭且保持合适的亮度
#if defined(LED_DIMMING_ENABLE) && (LED_DIMMING_ENABLE)
#define LED_DIM(n)  ((uint8_t)((uint16_t)n * get_blk_level()/500 + 2))
#else
#define LED_DIM(n)  (n)    // LED灯的亮度固定
#endif

enum {
    eLed_AllOff = 0,   // 熄灭所有灯珠
    eLed_AllOn,        // 点亮所有灯珠
    eLed_Refresh,      // 刷新所有灯珠
    eLed_LowBattery,   // 低电量告警

//  AtomCode 的状态，在对应的case里配置LED灯的行为
    eLed_AtomCode_Idle,
    eLed_AtomCode_Thinking,
    eLed_AtomCode_Working,
    eLed_AtomCode_Waiting_Confirm,
    eLed_AtomCode_Error,

    eLed_Max,
};

uint32_t LedsTaskEntry(void);

#endif
