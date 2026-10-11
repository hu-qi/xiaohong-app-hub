/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#include "snake_game.h"
#include "app_hub.h"
#include "lvgl_ui_layout.h"   /* DISP_WIDTH, DISP_HEIGHT */
#include "log.h"

#include <string.h>
#include <stdlib.h>

/* ===== 游戏参数 ===== */
#define SNAKE_GRID_W    12       /* 网格列数 */
#define SNAKE_GRID_H    12       /* 网格行数 */
#define SNAKE_CELL      16       /* 每格像素大小 (12*16=192, 居中在 240 屏) */
#define SNAKE_OFFSET_X  ((DISP_WIDTH  - SNAKE_GRID_W * SNAKE_CELL) / 2)  /* 24 */
#define SNAKE_OFFSET_Y  ((DISP_HEIGHT - SNAKE_GRID_H * SNAKE_CELL) / 2)  /* 24 */

#define SNAKE_MAX_LEN   (SNAKE_GRID_W * SNAKE_GRID_H)  /* 最大长度 = 144 */
#define SNAKE_INIT_LEN  3                               /* 初始长度 */
#define SNAKE_TICK_DIV  2                               /* on_tick 每100ms, 每2次移动一步 = 200ms */

/* 方向 */
enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT,
};

/* 游戏状态 */
enum {
    SNAKE_STATE_READY = 0,
    SNAKE_STATE_RUNNING,
    SNAKE_STATE_PAUSED,
    SNAKE_STATE_GAME_OVER,
};

/* 蛇身节点 */
typedef struct {
    int8_t x;
    int8_t y;
} snake_point_t;

/* ===== 内部状态 ===== */
static snake_point_t s_snake[SNAKE_MAX_LEN];
static uint8_t  s_snake_len   = 0;
static uint8_t  s_direction   = SNAKE_DIR_RIGHT;
static uint8_t  s_next_dir    = SNAKE_DIR_RIGHT;
static snake_point_t s_food   = {0};
static uint8_t  s_state       = SNAKE_STATE_READY;
static uint16_t s_score       = 0;
static uint8_t  s_tick_counter = 0;

/* LVGL: one custom-drawn 12x12 board replaces 144 heap objects.
 * Game model stays in s_snake[144] (288 bytes); no full-screen buffer. */
static lv_obj_t *s_board_obj=NULL;
static lv_obj_t *s_score_label=NULL;
static lv_obj_t *s_status_label=NULL;
static lv_color_t color_empty,color_snake_head,color_snake_body,color_food;

/* ===== 内部函数 ===== */

static void snake_place_food(void)
{
    bool valid = false;
    while (!valid) {
        s_food.x = rand() % SNAKE_GRID_W;
        s_food.y = rand() % SNAKE_GRID_H;
        valid = true;
        for (uint8_t i = 0; i < s_snake_len; i++) {
            if (s_snake[i].x == s_food.x && s_snake[i].y == s_food.y) {
                valid = false;
                break;
            }
        }
    }
}

static void snake_reset(void)
{
    s_snake_len = SNAKE_INIT_LEN;
    s_direction = SNAKE_DIR_RIGHT;
    s_next_dir  = SNAKE_DIR_RIGHT;
    s_score     = 0;
    s_state     = SNAKE_STATE_READY;
    s_tick_counter = 0;

    /* 初始蛇身：水平排列在中间偏左 */
    int8_t mid_y = SNAKE_GRID_H / 2;
    for (uint8_t i = 0; i < s_snake_len; i++) {
        s_snake[i].x = SNAKE_INIT_LEN - 1 - i;  /* 头在右，尾在左 */
        s_snake[i].y = mid_y;
    }

    snake_place_food();
}

static void snake_draw(lv_event_t *event)
{
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_area_t origin;
    lv_obj_get_coords((lv_obj_t *)lv_event_get_target(event),&origin);
    for(int y=0;y<SNAKE_GRID_H;y++)for(int x=0;x<SNAKE_GRID_W;x++) {
        lv_color_t color=color_empty;
        if(s_food.x==x&&s_food.y==y)color=color_food;
        for(uint8_t k=0;k<s_snake_len;k++) {
            if(s_snake[k].x==x&&s_snake[k].y==y) {
                color=k==0?color_snake_head:color_snake_body;
                break;
            }
        }
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color=color;d.bg_opa=LV_OPA_COVER;d.radius=1;
        lv_area_t cell={
            .x1=(int16_t)(origin.x1+x*SNAKE_CELL),
            .y1=(int16_t)(origin.y1+y*SNAKE_CELL),
            .x2=(int16_t)(origin.x1+x*SNAKE_CELL+SNAKE_CELL-2),
            .y2=(int16_t)(origin.y1+y*SNAKE_CELL+SNAKE_CELL-2)
        };
        lv_draw_rect(layer,&d,&cell);
    }
}
static void snake_render(void) {
    if(s_board_obj)lv_obj_invalidate(s_board_obj);
}

static void snake_update_status(void)
{
    if (s_status_label == NULL) {
        return;
    }
    switch (s_state) {
        case SNAKE_STATE_READY:
            lv_label_set_text(s_status_label, "M:START  +/- TURN");
            break;
        case SNAKE_STATE_RUNNING:
            lv_label_set_text(s_status_label, "");
            break;
        case SNAKE_STATE_PAUSED:
            lv_label_set_text(s_status_label, "PAUSED  M:RESUME");
            break;
        case SNAKE_STATE_GAME_OVER:
            lv_label_set_text(s_status_label, "GAME OVER  M:RESTART");
            break;
        default:
            break;
    }
}

static void snake_update_score(void)
{
    if (s_score_label != NULL) {
        lv_label_set_text_fmt(s_score_label, "SNAKE: %d", s_score);
    }
}

static void snake_step(void)
{
    /* 更新方向（防止 180° 反转） */
    s_direction = s_next_dir;

    /* 计算新头部位置 */
    snake_point_t new_head = s_snake[0];
    switch (s_direction) {
        case SNAKE_DIR_UP:    new_head.y--; break;
        case SNAKE_DIR_DOWN:  new_head.y++; break;
        case SNAKE_DIR_LEFT:  new_head.x--; break;
        case SNAKE_DIR_RIGHT: new_head.x++; break;
        default: break;
    }

    /* 碰墙检测 */
    if (new_head.x < 0 || new_head.x >= SNAKE_GRID_W ||
        new_head.y < 0 || new_head.y >= SNAKE_GRID_H) {
        s_state = SNAKE_STATE_GAME_OVER;
        snake_update_status();
        log_info("[Snake] game over: hit wall, score=%d\r\n", s_score);
        return;
    }

    /* 碰自身检测（不检查尾部——尾部会移走，除非吃食物） */
    uint8_t check_len = s_snake_len;
    if (new_head.x == s_food.x && new_head.y == s_food.y) {
        check_len = s_snake_len;  /* 吃食物时检查全身（尾部不会移走） */
    } else {
        check_len = s_snake_len - 1;  /* 不吃食物时尾部会移走，不检查最后一节 */
    }
    for (uint8_t i = 0; i < check_len; i++) {
        if (s_snake[i].x == new_head.x && s_snake[i].y == new_head.y) {
            s_state = SNAKE_STATE_GAME_OVER;
            snake_update_status();
            log_info("[Snake] game over: hit self, score=%d\r\n", s_score);
            return;
        }
    }

    /* 判断是否吃到食物 */
    bool ate_food = (new_head.x == s_food.x && new_head.y == s_food.y);

    /* 移动蛇身：从尾到头依次前移 */
    if (!ate_food) {
        /* 不吃食物：整体前移，长度不变 */
        for (int16_t i = s_snake_len - 1; i > 0; i--) {
            s_snake[i] = s_snake[i - 1];
        }
    } else {
        /* 吃食物：长度+1，所有节点后移一位 */
        for (int16_t i = s_snake_len; i > 0; i--) {
            s_snake[i] = s_snake[i - 1];
        }
        s_snake_len++;
        s_score += 10;
        snake_update_score();
        log_info("[Snake] ate food, score=%d, len=%d\r\n", s_score, s_snake_len);

        /* 棋盘已满时必须停止，不能调用无限寻找空格的放食物循环。 */
        if (s_snake_len >= SNAKE_MAX_LEN) {
            s_state = SNAKE_STATE_GAME_OVER;
            s_snake[0] = new_head;
            snake_render();
            snake_update_status();
            log_info("[Snake] game over: max length, score=%d\r\n", s_score);
            return;
        }
        snake_place_food();
    }

    /* 更新头部 */
    s_snake[0] = new_head;
}

/* ===== 公共接口 ===== */

bool snake_game_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return false;
    }

    /* 初始化颜色 */
    color_empty      = lv_color_make(0x10, 0x10, 0x10);  /* 深灰背景 */
    color_snake_head = lv_color_make(0x00, 0xFF, 0x00);  /* 亮绿头部 */
    color_snake_body = lv_color_make(0x00, 0xC0, 0x40);  /* 暗绿身体 */
    color_food       = lv_color_make(0xFF, 0x00, 0x00);  /* 红色食物 */

    /* A single LVGL board renderer stays under 1KB of game model memory. */
    s_board_obj=lv_obj_create(parent);
    lv_obj_remove_style_all(s_board_obj);
    lv_obj_set_pos(s_board_obj,SNAKE_OFFSET_X,SNAKE_OFFSET_Y);
    lv_obj_set_size(s_board_obj,SNAKE_GRID_W*SNAKE_CELL,
                    SNAKE_GRID_H*SNAKE_CELL);
    lv_obj_clear_flag(s_board_obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_board_obj,snake_draw,LV_EVENT_DRAW_MAIN,NULL);

    /* 分数标签（左上角） */
    s_score_label = lv_label_create(parent);
    lv_obj_set_pos(s_score_label, 2, 2);
    lv_obj_set_style_text_color(s_score_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);

    /* 状态标签（底部居中） */
    s_status_label = lv_label_create(parent);
    lv_obj_set_pos(s_status_label, 0, DISP_HEIGHT - 22);
    lv_obj_set_width(s_status_label, DISP_WIDTH);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, lv_color_make(0xFF, 0x82, 0x14), 0);

    snake_reset();
    snake_render();
    snake_update_score();
    snake_update_status();

    log_info("[Snake] custom board %dx%d, cell %dpx\r\n",
             SNAKE_GRID_W,SNAKE_GRID_H,SNAKE_CELL);
    return true;
}

void snake_game_destroy(void)
{
    s_board_obj=NULL;
    s_score_label  = NULL;
    s_status_label = NULL;

    log_info("[Snake] destroyed\r\n");
}

void snake_game_on_key(uint8_t key_event)
{
    switch (key_event) {
        case XH_APP_KEY_UP: /* + : relative left */
        case XH_APP_KEY_DOWN: { /* - : relative right */
            uint8_t next = s_direction;
            if (key_event == XH_APP_KEY_UP) {
                if (next == SNAKE_DIR_RIGHT) next = SNAKE_DIR_UP;
                else if (next == SNAKE_DIR_UP) next = SNAKE_DIR_LEFT;
                else if (next == SNAKE_DIR_LEFT) next = SNAKE_DIR_DOWN;
                else next = SNAKE_DIR_RIGHT;
            } else {
                if (next == SNAKE_DIR_RIGHT) next = SNAKE_DIR_DOWN;
                else if (next == SNAKE_DIR_DOWN) next = SNAKE_DIR_LEFT;
                else if (next == SNAKE_DIR_LEFT) next = SNAKE_DIR_UP;
                else next = SNAKE_DIR_RIGHT;
            }
            s_next_dir = next;
            break;
        }
        case XH_APP_KEY_LEFT:
        case XH_APP_KEY_RIGHT:
            break;
        case XH_APP_KEY_CONFIRM:
            if (s_state == SNAKE_STATE_READY) {
                s_state = SNAKE_STATE_RUNNING;
                snake_update_status();
            } else if (s_state == SNAKE_STATE_RUNNING) {
                s_state = SNAKE_STATE_PAUSED;
                snake_update_status();
            } else if (s_state == SNAKE_STATE_PAUSED) {
                s_state = SNAKE_STATE_RUNNING;
                snake_update_status();
            } else if (s_state == SNAKE_STATE_GAME_OVER) {
                snake_reset();
                snake_render();
                snake_update_score();
                snake_update_status();
            }
            break;
        case XH_APP_KEY_BACK:
            /* BACK 由 app_hub 处理退出，此处不需要 */
            break;
        default:
            break;
    }

    /* READY 状态下按方向键也启动游戏 */
    if (s_state == SNAKE_STATE_READY &&
        (key_event == XH_APP_KEY_UP || key_event == XH_APP_KEY_DOWN ||
         key_event == XH_APP_KEY_LEFT || key_event == XH_APP_KEY_RIGHT)) {
        s_state = SNAKE_STATE_RUNNING;
        snake_update_status();
    }
}

void snake_game_on_tick(void)
{
    if (s_state != SNAKE_STATE_RUNNING) {
        return;
    }

    /* on_tick 约每 100ms 调一次，每 SNAKE_TICK_DIV 次（200ms）移动一步 */
    s_tick_counter++;
    if (s_tick_counter < SNAKE_TICK_DIV) {
        return;
    }
    s_tick_counter = 0;

    snake_step();
    snake_render();
}
