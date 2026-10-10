/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#include "liquid_duck.h"
#include "app_hub.h"
#include "log.h"

/* LiquidDuck stub — 占位实现 */

static lv_obj_t *s_label = NULL;

bool liquid_duck_create(lv_obj_t *parent)
{
    s_label = lv_label_create(parent);
    lv_label_set_text(s_label, "LiquidDuck\n\n敬请期待");
    lv_obj_center(s_label);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    log_info("[LiquidDuck] create (stub)\r\n");
    return true;
}

void liquid_duck_destroy(void)
{
    s_label = NULL;
    log_info("[LiquidDuck] destroy (stub)\r\n");
}

void liquid_duck_on_key(uint8_t key_event)
{
    (void)key_event;
}

void liquid_duck_on_tick(void)
{
}
