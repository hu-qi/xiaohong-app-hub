/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#ifndef __LIQUID_DUCK_H__
#define __LIQUID_DUCK_H__

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

/* LiquidDuck — 预留接口，尚未实现 */

bool  liquid_duck_create(lv_obj_t *parent);
void  liquid_duck_destroy(void);
void  liquid_duck_on_key(uint8_t key_event);
void  liquid_duck_on_tick(void);

#endif /* __LIQUID_DUCK_H__ */
