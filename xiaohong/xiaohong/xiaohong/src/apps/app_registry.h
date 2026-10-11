/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to the furnished to do so, subject to the following
conditions:

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

#ifndef __APP_REGISTRY_H__
#define __APP_REGISTRY_H__

#include <stdint.h>
#include "app_hub.h"

/*
 * 应用注册表 — 静态编译期注册
 *
 * 每个应用在此注册，app_hub 通过此表枚举所有应用。
 * 未来动态安装的应用可通过 app_installer 扩展此表。
 */

/* 应用 ID 枚举 */
enum {
    XH_APP_ID_ASSISTANT = 0, /* resident voice assistant system app */
    XH_APP_ID_SNAKE = 1,
    XH_APP_ID_TETRIS,
    XH_APP_ID_BREAKOUT,
    XH_APP_ID_LIQUID_DUCK,
    XH_APP_ID_MAX,
};

/* 获取注册表数组 */
const xh_app_entry_t *xh_app_registry_get_all(uint8_t *count);

/* 按 ID 查找应用 */
const xh_app_entry_t *xh_app_registry_find_by_id(uint8_t id);

#endif /* __APP_REGISTRY_H__ */
