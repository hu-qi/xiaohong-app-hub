/*
MIT License
Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/
#include "liquid_duck.h"
#include "app_hub.h"
#include "log.h"
#include <string.h>

/* LiquidDuck: catch clean rain drops, dodge red polluted drops.
 * Fully two-key playable, no touch screen/IMU, no fluid simulation claim. */
#define DROP_COUNT 6
enum { D_READY, D_PLAY, D_PAUSED, D_OVER };
typedef struct { int16_t x,y;uint8_t type,active; } drop_t;
static drop_t drops[DROP_COUNT];
static lv_obj_t *drop_objs[DROP_COUNT],*duck_body,*duck_head,*duck_beak,
                *duck_eye,*pond,*hud,*footer;
static int16_t duck_x;
static uint16_t score;
static uint8_t lives,state,ticks,spawn_ticks;
static uint32_t prng;

static uint32_t next_random(void) {
    prng ^= prng << 13;prng ^= prng >> 17;prng ^= prng << 5;
    return prng;
}
static void update_text(void) {
    if(!hud||!footer)return;
    lv_label_set_text_fmt(hud,"LIQUID DUCK %u  HP:%u",(unsigned)score,(unsigned)lives);
    lv_label_set_text(footer,
        state==D_READY?"M START   +/- SWIM":
        state==D_PAUSED?"PAUSED   M RESUME":
        state==D_OVER?"GAME OVER  M RESTART":
        "+ LEFT   - RIGHT   M PAUSE");
}
static void paint(void) {
    lv_obj_set_pos(duck_body,duck_x,190);
    lv_obj_set_pos(duck_head,duck_x+27,179);
    lv_obj_set_pos(duck_beak,duck_x+45,191);
    lv_obj_set_pos(duck_eye,duck_x+40,184);
    for(unsigned i=0;i<DROP_COUNT;i++) {
        if(drops[i].active) {
            lv_obj_set_pos(drop_objs[i],drops[i].x,drops[i].y);
            lv_obj_set_style_bg_color(drop_objs[i],
                drops[i].type?lv_color_make(245,104,105):
                              lv_color_make(88,199,251),0);
            lv_obj_remove_flag(drop_objs[i],LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(drop_objs[i],LV_OBJ_FLAG_HIDDEN);
    }
    update_text();
}
static void reset(void) {
    memset(drops,0,sizeof(drops));
    lives=3;score=0;state=D_READY;duck_x=93;
    ticks=0;spawn_ticks=0;prng=0x4528a29bU;
    paint();
}
static void spawn_drop(void) {
    for(unsigned i=0;i<DROP_COUNT;i++) {
        if(drops[i].active)continue;
        drops[i].x=(int16_t)(12+(next_random()%210U));
        drops[i].y=29;
        drops[i].type=(uint8_t)((next_random()%5U)==0U);
        drops[i].active=1;
        return;
    }
}
static lv_obj_t *solid(lv_obj_t *parent,int w,int h,int radius,
                        lv_color_t color) {
    lv_obj_t *obj=lv_obj_create(parent);
    lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,color,0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(obj,0,0);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_radius(obj,radius,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
bool liquid_duck_create(lv_obj_t *parent) {
    if(!parent)return false;
    lv_obj_set_style_bg_color(parent,lv_color_make(10,25,43),0);
    pond=solid(parent,222,25,7,lv_color_make(25,75,109));
    lv_obj_set_pos(pond,9,204);
    for(unsigned i=0;i<DROP_COUNT;i++) {
        drop_objs[i]=solid(parent,9,13,5,lv_color_make(84,197,255));
    }
    duck_body=solid(parent,48,25,12,lv_color_make(251,213,82));
    duck_head=solid(parent,23,24,10,lv_color_make(255,224,109));
    duck_beak=solid(parent,17,8,3,lv_color_make(250,154,74));
    duck_eye=solid(parent,4,4,2,lv_color_make(24,44,58));
    hud=lv_label_create(parent);
    lv_obj_set_pos(hud,30,4);lv_obj_set_width(hud,200);
    lv_obj_set_style_text_color(hud,lv_color_make(228,241,252),0);
    footer=lv_label_create(parent);
    lv_obj_set_pos(footer,0,232);lv_obj_set_width(footer,240);
    lv_obj_set_style_text_align(footer,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(footer,lv_color_make(168,212,246),0);
    reset();return true;
}
void liquid_duck_destroy(void) {
    memset(drop_objs,0,sizeof(drop_objs));
    duck_body=NULL;duck_head=NULL;duck_beak=NULL;duck_eye=NULL;
    pond=NULL;hud=NULL;footer=NULL;state=D_READY;
}
void liquid_duck_on_key(uint8_t key) {
    if(key==XH_APP_KEY_CONFIRM) {
        if(state==D_OVER)reset();
        else if(state==D_READY||state==D_PAUSED)state=D_PLAY;
        else if(state==D_PLAY)state=D_PAUSED;
        update_text();return;
    }
    if(key==XH_APP_KEY_UP) {
        duck_x=(int16_t)(duck_x-14);if(duck_x<9)duck_x=9;
    } else if(key==XH_APP_KEY_DOWN) {
        duck_x=(int16_t)(duck_x+14);if(duck_x>166)duck_x=166;
    }
    paint();
}
void liquid_duck_on_tick(void) {
    if(state!=D_PLAY)return;
    ticks++;
    /* Water drops update each 100ms tick, increasing speed with score. */
    if(++spawn_ticks >= (score>=15?4U:7U)) {
        spawn_ticks=0;spawn_drop();
    }
    for(unsigned i=0;i<DROP_COUNT;i++) {
        if(!drops[i].active)continue;
        drops[i].y=(int16_t)(drops[i].y+4+(score>=15?2:0));
        if(drops[i].y >= 180 && drops[i].y <= 215 &&
           drops[i].x+8>=duck_x && drops[i].x<=duck_x+56) {
            if(drops[i].type) {
                if(lives>0)lives--;
                if(!lives)state=D_OVER;
            } else {
                score=(uint16_t)(score+1);
            }
            drops[i].active=0;
        } else if(drops[i].y>225) {
            drops[i].active=0;
        }
    }
    paint();
}
