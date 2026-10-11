/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#ifndef __TETRIS_GAME_H__
#define __TETRIS_GAME_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* 俄罗斯方块 — 预留接口，尚未实现 */

bool  tetris_game_create(lv_obj_t *parent);
void  tetris_game_destroy(void);
void  tetris_game_on_key(uint8_t key_event);
void  tetris_game_on_tick(void);

#endif /* __TETRIS_GAME_H__ */
