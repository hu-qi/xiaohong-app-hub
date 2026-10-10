/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#ifndef __BREAKOUT_GAME_H__
#define __BREAKOUT_GAME_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* 打砖块 — 预留接口，尚未实现 */

bool  breakout_game_create(lv_obj_t *parent);
void  breakout_game_destroy(void);
void  breakout_game_on_key(uint8_t key_event);
void  breakout_game_on_tick(void);

#endif /* __BREAKOUT_GAME_H__ */
