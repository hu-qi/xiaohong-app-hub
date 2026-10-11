/*
MIT License
Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/
#include "tetris_game.h"
#include "app_hub.h"
#include "log.h"
#include <string.h>

/* 10x16 board: 160 reusable LVGL squares, no 115KB full-screen framebuffer. */
#define TW 10
#define TH 16
#define TC 12
#define TX 60
#define TY 17
enum { T_READY, T_PLAYING, T_OVER };
static uint8_t board[TH][TW];
static lv_obj_t *cells[TH][TW];
static lv_obj_t *hud, *prompt;
static int8_t px,py;
static uint8_t kind,rotation,status,gravity,sequence;
static uint16_t score,lines;
/* Relative 4x4 coordinates are rotated around the 4x4 grid center.
 * 0 = I, 1 = O, 2 = T, 3 = L, 4 = J, 5 = S, 6 = Z. */
static const uint8_t shapes[7][4][2] = {
    {{0,1},{1,1},{2,1},{3,1}},
    {{1,1},{2,1},{1,2},{2,2}},
    {{1,1},{0,2},{1,2},{2,2}},
    {{2,1},{0,2},{1,2},{2,2}},
    {{0,1},{0,2},{1,2},{2,2}},
    {{1,1},{2,1},{0,2},{1,2}},
    {{0,1},{1,1},{1,2},{2,2}}
};
static uint8_t cell_x(uint8_t n,uint8_t r) {
    int8_t x=(int8_t)shapes[kind][n][0], y=(int8_t)shapes[kind][n][1];
    if(kind==1) return (uint8_t)x;
    while(r--) {int8_t t=x;x=(int8_t)(3-y);y=t;}
    return (uint8_t)x;
}
static uint8_t cell_y(uint8_t n,uint8_t r) {
    int8_t x=(int8_t)shapes[kind][n][0], y=(int8_t)shapes[kind][n][1];
    if(kind==1) return (uint8_t)y;
    while(r--) {int8_t t=x;x=(int8_t)(3-y);y=t;}
    return (uint8_t)y;
}
static bool fits(int8_t nx,int8_t ny,uint8_t r) {
    for(unsigned i=0;i<4;i++) {
        int x=nx+cell_x((uint8_t)i,r), y=ny+cell_y((uint8_t)i,r);
        if(x<0||x>=TW||y>=TH) return false;
        if(y>=0&&board[y][x]) return false;
    }
    return true;
}
static void spawn(void) {
    kind=(uint8_t)(sequence++%7);
    rotation=0; px=3;py=-1;
    if(!fits(px,py,rotation)) status=T_OVER;
}
static void title(void) {
    if(hud) lv_label_set_text_fmt(hud,"TETRIS %u  L%u",(unsigned)score,(unsigned)lines);
    if(prompt) lv_label_set_text(prompt,status==T_READY?"M:START   +/- MOVE":
                              status==T_OVER?"GAME OVER  M:RETRY":
                              "+ LEFT   - RIGHT   M TURN");
}
static void render(void) {
    for(int y=0;y<TH;y++)for(int x=0;x<TW;x++) {
        lv_color_t c=board[y][x]?lv_color_make(97,190,239):lv_color_make(24,36,54);
        if(status!=T_OVER)for(uint8_t i=0;i<4;i++) {
            if(px+cell_x(i,rotation)==x&&py+cell_y(i,rotation)==y) {
                c=lv_color_make(246,202,95);break;
            }
        }
        lv_obj_set_style_bg_color(cells[y][x],c,0);
    }
    title();
}
static void reset(void) {
    memset(board,0,sizeof(board));score=0;lines=0;sequence=0;gravity=0;
    status=T_READY;spawn();render();
}
static void lock_piece(void) {
    for(uint8_t i=0;i<4;i++) {
        int x=px+cell_x(i,rotation),y=py+cell_y(i,rotation);
        if(y<0) {status=T_OVER;return;}
        if(y<TH&&x>=0&&x<TW)board[y][x]=(uint8_t)(kind+1);
    }
    uint8_t cleared=0;
    for(int y=TH-1;y>=0;y--) {
        bool full=true;
        for(int x=0;x<TW;x++)if(!board[y][x]) {full=false;break;}
        if(full) {
            for(int row=y;row>0;row--)memcpy(board[row],board[row-1],TW);
            memset(board[0],0,TW);cleared++;y++;
        }
    }
    lines=(uint16_t)(lines+cleared);
    score=(uint16_t)(score+10+cleared*cleared*100);
    spawn();
}
bool tetris_game_create(lv_obj_t *parent) {
    if(!parent)return false;
    lv_obj_set_style_bg_color(parent,lv_color_make(9,17,31),0);
    for(int y=0;y<TH;y++)for(int x=0;x<TW;x++) {
        lv_obj_t *obj=lv_obj_create(parent);
        cells[y][x]=obj;
        lv_obj_set_pos(obj,TX+x*TC,TY+y*TC);
        lv_obj_set_size(obj,TC-1,TC-1);
        lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
        lv_obj_set_style_pad_all(obj,0,0);
        lv_obj_set_style_radius(obj,1,0);
        lv_obj_set_style_border_width(obj,0,0);
        lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    }
    hud=lv_label_create(parent);lv_obj_set_pos(hud,49,1);lv_obj_set_width(hud,152);
    lv_obj_set_style_text_color(hud,lv_color_make(232,241,251),0);
    prompt=lv_label_create(parent);lv_obj_set_pos(prompt,0,217);
    lv_obj_set_width(prompt,240);
    lv_obj_set_style_text_align(prompt,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(prompt,lv_color_make(198,218,245),0);
    reset();return true;
}
void tetris_game_destroy(void) {
    memset(cells,0,sizeof(cells));hud=NULL;prompt=NULL;
    status=T_READY;
}
void tetris_game_on_key(uint8_t key) {
    if(status==T_OVER) {
        if(key==XH_APP_KEY_CONFIRM)reset();
        return;
    }
    if(status==T_READY) {
        if(key==XH_APP_KEY_CONFIRM) {status=T_PLAYING;title();}
        return;
    }
    if(key==XH_APP_KEY_UP) {
        if(fits((int8_t)(px-1),py,rotation))px--;
    } else if(key==XH_APP_KEY_DOWN) {
        if(fits((int8_t)(px+1),py,rotation))px++;
    } else if(key==XH_APP_KEY_CONFIRM) {
        uint8_t nr=(uint8_t)((rotation+1)&3U);
        if(fits(px,py,nr))rotation=nr;
        else if(fits((int8_t)(px-1),py,nr)) {px--;rotation=nr;}
        else if(fits((int8_t)(px+1),py,nr)) {px++;rotation=nr;}
    }
    render();
}
void tetris_game_on_tick(void) {
    if(status!=T_PLAYING)return;
    gravity++;
    uint8_t div=(uint8_t)((lines>10)?3:(lines>4)?5:7);
    if(gravity<div)return;
    gravity=0;
    if(fits(px,(int8_t)(py+1),rotation))py++;
    else lock_piece();
    render();
}
