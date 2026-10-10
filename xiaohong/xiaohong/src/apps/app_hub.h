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

#ifndef __APP_HUB_H__
#define __APP_HUB_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/*
 * XiaoHong App Hub — 应用大厅核心接口
 *
 * 设计思路：
 *   - 每个应用实现 xh_app_entry_t 接口（create/destroy/key/tick）
 *   - app_registry 维护静态注册表，编译期决定哪些应用可用
 *   - app_hub 管理状态机：HIDDEN → LIST → RUNNING → LIST → HIDDEN
 *   - app_installer 预留未来动态安装接口（当前为 stub）
 *
 * 按键映射（在应用大厅/游戏中）：
 *   VolUp 短按 → 上/左
 *   VolDw 短按 → 下/右
 *   Wakeup 短按 → 确认/启动
 *   Wakeup 长按 → 返回/退出
 *
 * 进入应用大厅：
 *   VolDw 长按 → 从 AI 模式进入应用大厅
 */

/* 应用按键事件（由 app_hub 转发给各应用） */
enum {
    XH_APP_KEY_UP = 0,       /* VolUp 短按 */
    XH_APP_KEY_DOWN,         /* VolDw 短按 */
    XH_APP_KEY_LEFT,         /* VolUp 短按（游戏左移，与 UP 复用，由应用自行解释） */
    XH_APP_KEY_RIGHT,        /* VolDw 短按（游戏右移，与 DOWN 复用，由应用自行解释） */
    XH_APP_KEY_CONFIRM,      /* Wakeup 短按 */
    XH_APP_KEY_BACK,         /* Wakeup 长按 */
    XH_APP_KEY_MAX,
};

/* 应用大厅状态 */
enum {
    XH_HUB_STATE_HIDDEN = 0,   /* 应用大厅不可见（正常 AI 模式） */
    XH_HUB_STATE_LIST,         /* 显示应用列表 */
    XH_HUB_STATE_RUNNING,      /* 某个应用正在运行 */
};

/* 应用入口结构体 — 每个应用实现此接口 */
typedef struct {
    uint8_t  id;               /* 唯一应用 ID */
    const char *name;          /* 显示名称（UTF-8） */
    const char *icon;          /* 图标符号（LV_SYMBOL 或 emoji） */
    bool available;            /* 是否已实现（false = 占位/敬请期待） */

    /*
     * on_create: 创建应用 UI，传入父容器（app_hub 提供的 lv_obj_t）
     *   返回 true 表示创建成功，false 表示失败
     * on_destroy: 销毁应用 UI，释放资源
     * on_key: 接收按键事件（XH_APP_KEY_*）
     * on_tick: 周期性更新（由 LVGL timer 驱动，约 100ms 一次）
     *   NULL 表示不需要周期更新
     */
    bool  (*on_create)(lv_obj_t *parent);
    void  (*on_destroy)(void);
    void  (*on_key)(uint8_t key_event);
    void  (*on_tick)(void);
} xh_app_entry_t;

/* ===== App Hub 公共接口 ===== */

/* 初始化应用大厅（在 LvglTask 中调用一次） */
void xh_app_hub_init(void);

/* 打开应用大厅（显示应用列表） */
void xh_app_hub_open(void);

/* 关闭应用大厅（返回 AI 模式） */
void xh_app_hub_close(void);

/* 应用大厅是否处于活动状态（非 HIDDEN） */
bool xh_app_hub_is_active(void);

/*
 * 处理物理按键事件（由 MainTask 调用）
 * 参数 key_event: key_config.h 中的 eKey_* 值
 * 返回 true 表示应用大厅消费了此事件，调用方不应再处理
 */
bool xh_app_hub_handle_key(uint8_t key_event);

/* 周期性 tick（由 LvglTask 的 timer 回调驱动） */
void xh_app_hub_tick(void);

#endif /* __APP_HUB_H__ */
