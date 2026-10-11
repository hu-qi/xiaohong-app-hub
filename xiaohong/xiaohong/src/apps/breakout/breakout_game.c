/*
MIT License
Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/
#include "breakout_game.h"
#include "app_hub.h"
#include "log.h"
#include <string.h>

#define BW 7
#define BH 4
#define BRICK_W 28
#define BRICK_H 10
#define BRICK_GAP 3
#define BRICK_X 13
#define BRICK_Y 37
#define PADDLE_W 40
#define PADDLE_Y 215
enum { B_READY, B_PLAY, B_PAUSED, B_OVER, B_WIN };
static lv_obj_t *bricks[BH][BW], *ball, *paddle, *hud, *footer;
static uint8_t grid[BH][BW],state,lives;
static uint16_t score;
static int16_t bx,by,pad_x;
static int8_t dx,dy;
static uint8_t tick_count;

static void reset_ball(void) {
    bx=120;by=189;dx=3;dy=-4;pad_x=100;tick_count=0;
}
static void update_text(void) {
    if(!hud||!footer)return;
    lv_label_set_text_fmt(hud,"BREAKOUT  %u   HP:%u",(unsigned)score,(unsigned)lives);
    lv_label_set_text(footer,
        state==B_READY?"M START    +/- MOVE":
        state==B_PAUSED?"PAUSED   M RESUME":
        state==B_OVER?"GAME OVER   M RESTART":
        state==B_WIN?"CLEARED!   M RESTART":
        "+ LEFT   - RIGHT   M PAUSE");
}
static void paint(void) {
    for(int y=0;y<BH;y++)for(int x=0;x<BW;x++) {
        if(grid[y][x]) lv_obj_remove_flag(bricks[y][x],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(bricks[y][x],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(ball,bx-4,by-4);
    lv_obj_set_pos(paddle,pad_x,PADDLE_Y);
    update_text();
}
static void restart(void) {
    memset(grid,1,sizeof(grid));
    lives=3;score=0;state=B_READY;reset_ball();paint();
}
bool breakout_game_create(lv_obj_t *parent) {
    if(!parent)return false;
    lv_obj_set_style_bg_color(parent,lv_color_make(10,17,32),0);
    for(int y=0;y<BH;y++)for(int x=0;x<BW;x++) {
        lv_obj_t *obj=lv_obj_create(parent);
        bricks[y][x]=obj;
        lv_obj_set_pos(obj,BRICK_X+x*(BRICK_W+BRICK_GAP),
                       BRICK_Y+y*(BRICK_H+BRICK_GAP));
        lv_obj_set_size(obj,BRICK_W,BRICK_H);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_set_style_border_width(obj,0,0);
        lv_obj_set_style_radius(obj,2,0);
        lv_obj_set_style_bg_color(obj,
           y%2?lv_color_make(93,208,206):lv_color_make(247,171,109),0);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }
    ball=lv_obj_create(parent);
    lv_obj_set_size(ball,8,8);
    lv_obj_set_style_radius(ball,4,0);
    lv_obj_set_style_bg_color(ball,lv_color_make(254,241,161),0);
    lv_obj_set_style_border_width(ball,0,0);
    lv_obj_set_style_pad_all(ball,0,0);
    lv_obj_clear_flag(ball,LV_OBJ_FLAG_SCROLLABLE);
    paddle=lv_obj_create(parent);
    lv_obj_set_size(paddle,PADDLE_W,8);
    lv_obj_set_style_radius(paddle,4,0);
    lv_obj_set_style_bg_color(paddle,lv_color_make(85,145,246),0);
    lv_obj_set_style_border_width(paddle,0,0);
    lv_obj_set_style_pad_all(paddle,0,0);
    lv_obj_clear_flag(paddle,LV_OBJ_FLAG_SCROLLABLE);
    hud=lv_label_create(parent);
    lv_obj_set_pos(hud,45,4);lv_obj_set_width(hud,165);
    lv_obj_set_style_text_color(hud,lv_color_make(235,243,255),0);
    footer=lv_label_create(parent);
    lv_obj_set_pos(footer,0,230);lv_obj_set_width(footer,240);
    lv_obj_set_style_text_align(footer,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(footer,lv_color_make(167,201,232),0);
    restart();return true;
}
void breakout_game_destroy(void) {
    memset(bricks,0,sizeof(bricks));ball=NULL;paddle=NULL;hud=NULL;footer=NULL;
    state=B_READY;
}
void breakout_game_on_key(uint8_t key) {
    if(key==XH_APP_KEY_CONFIRM) {
        if(state==B_OVER||state==B_WIN)restart();
        else if(state==B_READY||state==B_PAUSED)state=B_PLAY;
        else if(state==B_PLAY)state=B_PAUSED;
        update_text();return;
    }
    if(key==XH_APP_KEY_UP) {
        pad_x-=17; if(pad_x<9)pad_x=9;
    } else if(key==XH_APP_KEY_DOWN) {
        pad_x+=17;if(pad_x>231-PADDLE_W)pad_x=231-PADDLE_W;
    }
    if(state==B_READY)bx=pad_x+PADDLE_W/2;
    paint();
}
void breakout_game_on_tick(void) {
    if(state!=B_PLAY)return;
    /* A 100ms LVGL tick => a 200ms physics frame, affordable on WS63. */
    if(++tick_count<1U)return;
    tick_count=0;
    int16_t nx=(int16_t)(bx+dx),ny=(int16_t)(by+dy);
    if(nx<=8||nx>=231) {dx=(int8_t)-dx;nx=(int16_t)(bx+dx);}
    if(ny<=24) {dy=(int8_t)-dy;ny=(int16_t)(by+dy);}
    bool collision=false;
    for(int y=0;y<BH&&!collision;y++)for(int x=0;x<BW;x++) {
        if(!grid[y][x])continue;
        int rx=BRICK_X+x*(BRICK_W+BRICK_GAP),ry=BRICK_Y+y*(BRICK_H+BRICK_GAP);
        if(nx+4>=rx&&nx-4<=rx+BRICK_W&&ny+4>=ry&&ny-4<=ry+BRICK_H) {
            grid[y][x]=0;score=(uint16_t)(score+10);
            dy=(int8_t)-dy;ny=(int16_t)(by+dy);
            collision=true;break;
        }
    }
    if(dy>0&&ny+4>=PADDLE_Y&&by<PADDLE_Y &&
       nx>=pad_x-3&&nx<=pad_x+PADDLE_W+3) {
        dy=(int8_t)-dy;
        dx=(int8_t)((nx<pad_x+PADDLE_W/3)?-4:
                    (nx>pad_x+(PADDLE_W*2)/3)?4:(dx<0?-2:2));
        ny=(int16_t)(by+dy);
    }
    if(ny>237) {
        if(lives>0)lives--;
        if(lives==0)state=B_OVER;
        else {state=B_READY;reset_ball();}
    } else {bx=nx;by=ny;}
    bool remaining=false;
    for(int y=0;y<BH;y++)for(int x=0;x<BW;x++)if(grid[y][x])remaining=true;
    if(!remaining)state=B_WIN;
    paint();
}
