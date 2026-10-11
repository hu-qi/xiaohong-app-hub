/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#include "app_registry.h"
#include "snake/snake_game.h"
#include "tetris/tetris_game.h"
#include "breakout/breakout_game.h"
#include "liquid_duck/liquid_duck.h"

/*
 * 静态应用注册表
 *
 * available=false 的应用会显示在列表中但标记为"敬请期待"，
 * 启动时弹出提示而非真正运行。
 */
static const xh_app_entry_t s_app_registry[] = {
    {
        .id         = XH_APP_ID_SNAKE,
        .name       = "Snake",
        .icon       = "S",       /* 暂用内置符号，后续可换 emoji */
        .available  = true,
        .on_create  = snake_game_create,
        .on_destroy = snake_game_destroy,
        .on_key     = snake_game_on_key,
        .on_tick    = snake_game_on_tick,
    },
    {
        .id         = XH_APP_ID_TETRIS,
        .name       = "Tetris",
        .icon       = "T",
        .available  = false,              /* 预留接口，尚未实现 */
        .on_create  = tetris_game_create,
        .on_destroy = tetris_game_destroy,
        .on_key     = tetris_game_on_key,
        .on_tick    = tetris_game_on_tick,
    },
    {
        .id         = XH_APP_ID_BREAKOUT,
        .name       = "Breakout",
        .icon       = "B",
        .available  = false,
        .on_create  = breakout_game_create,
        .on_destroy = breakout_game_destroy,
        .on_key     = breakout_game_on_key,
        .on_tick    = breakout_game_on_tick,
    },
    {
        .id         = XH_APP_ID_LIQUID_DUCK,
        .name       = "LiquidDuck",
        .icon       = "D",
        .available  = false,
        .on_create  = liquid_duck_create,
        .on_destroy = liquid_duck_destroy,
        .on_key     = liquid_duck_on_key,
        .on_tick    = liquid_duck_on_tick,
    },
};

const xh_app_entry_t *xh_app_registry_get_all(uint8_t *count)
{
    if (count != NULL) {
        *count = sizeof(s_app_registry) / sizeof(s_app_registry[0]);
    }
    return s_app_registry;
}

const xh_app_entry_t *xh_app_registry_find_by_id(uint8_t id)
{
    uint8_t n = sizeof(s_app_registry) / sizeof(s_app_registry[0]);
    for (uint8_t i = 0; i < n; i++) {
        if (s_app_registry[i].id == id) {
            return &s_app_registry[i];
        }
    }
    return NULL;
}
