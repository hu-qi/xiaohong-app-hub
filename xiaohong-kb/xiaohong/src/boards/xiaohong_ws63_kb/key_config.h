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

#ifndef __KEY_CONFIG_H__
#define __KEY_CONFIG_H__

#include "board_config.h"

enum {
    eKey_Released = 0,
    eKey_Mic,    // 默认拉起PC的语音输入功能，不可重定义功能
    eKey_Always, // 默认字母'a'键，可重定义功能
    eKey_No,     // 默认'ESC'键，可重定义功能
    eKey_Yes,    // 默认'Enter'键，可重定义功能
    eKey_Up,     // 默认'PageUp'键，可重定义功能
    eKey_Down,   // 默认'PageDown'键，可重定义功能

    eKey_Always_No,    // 默认断开当前连接的Dongle或刷新当前页面，可重定义功能
    eKey_Always_Yes,   // 默认连接当前高亮选中的Dongle，可重定义功能

    eKey_Always_Up,    // 默认切换键盘的工作模式，不可重定义功能
    eKey_Always_Down,  // 默认切换键盘的按键映射码，不可重定义功能
    // 以下四个组合键默认不启用
    // MIC键最高优先级，按下即打开PC端语音输入法
    // 持续按下持续输入，直至松开才停止输入
    eKey_Mic_No,
    eKey_Mic_Yes,
    eKey_Mic_Up,
    eKey_Mic_Down,
    eKey_Max,
};

uint32_t key_init(void);
uint32_t KeysTaskEntry(void);

#endif//__KEY_CONFIG_H__