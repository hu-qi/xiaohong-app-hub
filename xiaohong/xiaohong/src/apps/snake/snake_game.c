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

/* ===== LVGL 对象 ===== */
/*
 * 用 12×12=144 个 lv_obj_t 小方块代替 Canvas。
 * 每个 lv_obj_t ~100 bytes，总计 ~14.4KB，远小于 Canvas 的 112KB。
 * 游戏逻辑只改 bg_color，不创建/销毁对象，性能好。
 */
static lv_obj_t *s_cells[SNAKE_GRID_W][SNAKE_GRID_H];  /* 网格对象 */
static lv_obj_t *s_score_label  = NULL;
static lv_obj_t *s_status_label = NULL;

/* 颜色定义 */
static const lv_color_t COLOR_BG    = {0};  /* 黑色背景 — 运行时用 lv_color_make */
static lv_color_t color_empty;
static lv_color_t color_snake_head;
static lv_color_t color_snake_body;
static lv_color_t color_food;
static lv_color_t color_grid;

/* 外部字体 */
LV_FONT_DECLARE(lv_font_SourceHanSerifCN_SemiBold_22_Default);

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

static void snake_render(void)
{
    /* 先全部清空为 empty 颜色 */
    for (int8_t y = 0; y < SNAKE_GRID_H; y++) {
        for (int8_t x = 0; x < SNAKE_GRID_W; x++) {
            lv_obj_set_style_bg_color(s_cells[x][y], color_empty, 0);
        }
    }

    /* 画食物 */
    lv_obj_set_style_bg_color(s_cells[s_food.x][s_food.y], color_food, 0);

    /* 画蛇身 */
    for (uint8_t i = 0; i < s_snake_len; i++) {
        if (i == 0) {
            lv_obj_set_style_bg_color(s_cells[s_snake[i].x][s_snake[i].y], color_snake_head, 0);
        } else {
            lv_obj_set_style_bg_color(s_cells[s_snake[i].x][s_snake[i].y], color_snake_body, 0);
        }
    }
}

static void snake_update_status(void)
{
    if (s_status_label == NULL) {
        return;
    }
    switch (s_state) {
        case SNAKE_STATE_READY:
            lv_label_set_text(s_status_label, "按确认键开始");
            break;
        case SNAKE_STATE_RUNNING:
            lv_label_set_text(s_status_label, "");
            break;
        case SNAKE_STATE_PAUSED:
            lv_label_set_text(s_status_label, "已暂停");
            break;
        case SNAKE_STATE_GAME_OVER:
            lv_label_set_text(s_status_label, "游戏结束 按确认重开");
            break;
        default:
            break;
    }
}

static void snake_update_score(void)
{
    if (s_score_label != NULL) {
        lv_label_set_text_fmt(s_score_label, "分数: %d", s_score);
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
        snake_place_food();
        snake_update_score();
        log_info("[Snake] ate food, score=%d, len=%d\r\n", s_score, s_snake_len);

        if (s_snake_len >= SNAKE_MAX_LEN) {
            s_state = SNAKE_STATE_GAME_OVER;
            s_snake[0] = new_head;
            snake_render();
            snake_update_status();
            log_info("[Snake] game over: max length, score=%d\r\n", s_score);
            return;
        }
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
    color_grid       = lv_color_make(0x20, 0x20, 0x20);  /* 网格线 */

    /* 创建 12×12 网格对象 */
    for (int8_t y = 0; y < SNAKE_GRID_H; y++) {
        for (int8_t x = 0; x < SNAKE_GRID_W; x++) {
            s_cells[x][y] = lv_obj_create(parent);
            lv_obj_set_pos(s_cells[x][y],
                           SNAKE_OFFSET_X + x * SNAKE_CELL,
                           SNAKE_OFFSET_Y + y * SNAKE_CELL);
            lv_obj_set_size(s_cells[x][y], SNAKE_CELL - 1, SNAKE_CELL - 1);
            lv_obj_set_style_bg_opa(s_cells[x][y], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(s_cells[x][y], color_empty, 0);
            lv_obj_set_style_border_width(s_cells[x][y], 0, 0);
            lv_obj_set_style_radius(s_cells[x][y], 1, 0);
            lv_obj_clear_flag(s_cells[x][y], LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    /* 分数标签（左上角） */
    s_score_label = lv_label_create(parent);
    lv_obj_set_pos(s_score_label, 2, 2);
    lv_obj_set_style_text_color(s_score_label, lv_color_make(0xFF, 0xFF, 0xFF), 0);
    lv_obj_set_style_text_font(s_score_label, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);

    /* 状态标签（底部居中） */
    s_status_label = lv_label_create(parent);
    lv_obj_set_pos(s_status_label, 0, DISP_HEIGHT - 22);
    lv_obj_set_width(s_status_label, DISP_WIDTH);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, lv_color_make(0xFF, 0x82, 0x14), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_SourceHanSerifCN_SemiBold_22_Default, LV_PART_MAIN);

    snake_reset();
    snake_render();
    snake_update_score();
    snake_update_status();

    log_info("[Snake] created: grid %dx%d, cell %dpx, %d objects (~%d bytes)\r\n",
             SNAKE_GRID_W, SNAKE_GRID_H, SNAKE_CELL,
             SNAKE_GRID_W * SNAKE_GRID_H, SNAKE_GRID_W * SNAKE_GRID_H * 100);
    return true;
}

void snake_game_destroy(void)
{
    /* 网格对象会被父容器销毁时自动清理，只需清空指针 */
    for (int8_t y = 0; y < SNAKE_GRID_H; y++) {
        for (int8_t x = 0; x < SNAKE_GRID_W; x++) {
            s_cells[x][y] = NULL;
        }
    }
    s_score_label  = NULL;
    s_status_label = NULL;

    log_info("[Snake] destroyed\r\n");
}

void snake_game_on_key(uint8_t key_event)
{
    switch (key_event) {
        case XH_APP_KEY_UP:
            if (s_direction != SNAKE_DIR_DOWN) {
                s_next_dir = SNAKE_DIR_UP;
            }
            break;
        case XH_APP_KEY_DOWN:
            if (s_direction != SNAKE_DIR_UP) {
                s_next_dir = SNAKE_DIR_DOWN;
            }
            break;
        case XH_APP_KEY_LEFT:
            if (s_direction != SNAKE_DIR_RIGHT) {
                s_next_dir = SNAKE_DIR_LEFT;
            }
            break;
        case XH_APP_KEY_RIGHT:
            if (s_direction != SNAKE_DIR_LEFT) {
                s_next_dir = SNAKE_DIR_RIGHT;
            }
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
