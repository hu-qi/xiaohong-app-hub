/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#ifndef __SNAKE_GAME_H__
#define __SNAKE_GAME_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/*
 * 贪吃蛇游戏 — XiaoHong WS63 240×240 LCD
 *
 * 按键：
 *   XH_APP_KEY_UP    → 上
 *   XH_APP_KEY_DOWN  → 下
 *   XH_APP_KEY_LEFT  → 左
 *   XH_APP_KEY_RIGHT → 右
 *   XH_APP_KEY_CONFIRM → 暂停/继续
 *   XH_APP_KEY_BACK  → 退出（由 app_hub 处理）
 *
 * 由 app_hub 调用，不需要自行创建屏幕。
 */

bool  snake_game_create(lv_obj_t *parent);
void  snake_game_destroy(void);
void  snake_game_on_key(uint8_t key_event);
void  snake_game_on_tick(void);

#endif /* __SNAKE_GAME_H__ */
