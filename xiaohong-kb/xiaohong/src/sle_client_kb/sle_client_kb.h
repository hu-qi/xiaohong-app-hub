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

#ifndef __SLE_CLIENT_KB_H__
#define __SLE_CLIENT_KB_H__

#include "kb_keymap_config.h"

// Dongle 向键盘回传的HID报告数据[长度:12字节+1字节的'\0'占位符]:
// [0] report id: 0x08
// [1] report type: 0x00[atomcode 工作状态]/0x01~0x0FF[待定义与扩展]
// [2] [report type = 0x00]: atomcode 工作状态: 0x00~0x04: 0=idle/1=thinking/2=working/3=waiting_confirm/4=error
// [3~12] 保留
#define REPORT_SIZE                   13    // 13 bytes, 实际12字节+1字节的'\0'占位符
#define REPORT_ID                     0x08  // 1 byte report id, vendor 自定义指令
#define REPORT_DATA_LEN               12    // 12 bytes report data length
#define REPORT_TYPE_ATOMCODE_STATUS   0x00  // 1 byte report type, atomcode 状态


uint32_t SleClientTaskEntry(void);

#endif//__SLE_CLIENT_KB_H__