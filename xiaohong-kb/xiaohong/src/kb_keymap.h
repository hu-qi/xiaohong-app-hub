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

#ifndef __KB_KEYMAP_H__
#define __KB_KEYMAP_H__

#include <stdint.h>
#include <string.h>
#include "errcode.h"
#include "key_config.h"
#include "kb_keymap_config.h"

// 键位映射：每个物理键(eKey_*)可自定义为一个"动作"。
// 动作 = HID 按键序列（宏），最多 KEYMAP_MACRO_MAX_STEPS 步，
// 每步为一次 (modifier + keycode) 的 USB HID 报告。
#define KEYMAP_MACRO_MAX_STEPS  6

// NV 存储 ID：0x1001 已被 hilink_connect_info_t 占用，此处使用用户区 0x1002
#define KB_KEYMAP_NV_ID         (0x1002)
#define KB_KEYMAP_MAGIC         (0x4B4D504D)   // "KBPM"

typedef struct {
    uint8_t modifier;                          // HID modifier: bit0 LCtrl/bit1 LShift/bit2 LAlt/bit3 LGUI ...
    uint8_t keycode;                           // HID 键码（见 keymap_config.h 的 HID_KEY_*）
} keymap_step_t;

typedef struct {
    uint8_t step_count;                        // 0 = 未自定义（运行时走默认映射/系统功能）
    keymap_step_t steps[KEYMAP_MACRO_MAX_STEPS];
} keymap_action_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    keymap_action_t keys[eKey_Max];            // 下标 = eKey_* 枚举值
} keymap_config_t;

// 读取 NV 中的键位映射到内存（上电时调用一次）
errcode_t keymap_read_from_nv(void);

// 将内存中的键位映射写入 NV
errcode_t keymap_write_to_nv(void);

// 获取某键的自定义动作；返回 NULL 表示该键未自定义
const keymap_action_t *keymap_get_action(uint8_t ekey);

// 设置某键的自定义动作并写 NV；act==NULL 表示清除自定义（恢复默认）
errcode_t keymap_set_action(uint8_t ekey, const keymap_action_t *act);

// 清除全部自定义映射（恢复默认）
errcode_t keymap_reset_all(void);

#endif /* __KB_KEYMAP_H__ */
