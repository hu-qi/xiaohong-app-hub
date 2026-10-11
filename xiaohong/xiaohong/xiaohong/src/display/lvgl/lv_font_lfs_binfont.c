/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/**
 * @file lv_font_lfs_binfont.c
 * @brief LVGL 9.x：经 LittleFS 流式加载 bin 字库（路径见 xh_font_paths.h），避免整库位图进堆。
 */

#include "lv_font_lfs_binfont.h"

#include <stdio.h>

#include "disp_driver.h"
#include "lvgl_ui_layout.h"
#include "littlefs_adapt.h"
#include "lv_font_stream_bin.h"
#include "lv_fs_lfs_adapt.h"
#include "xh_font_paths.h"

/*
 * 流式 font.bin 须由 lv_font_conv 生成 **--format bin**（与 lv_font_stream_bin / 官方 lv_binfont 布局一致），
 * 不可把 --format lvgl 的 .c 当 bin 用。须包含实际界面会用到的汉字 Unicode（例如
 * --range 0x4E00-0x9FFF 或按需 --symbols），否则 cmap 无对应码点会显示方框（□）。
 * 加载后为流式字体设置 fallback，缺字时可退回芯片内嵌的小子集字体（字形可能不一致）。
 */
#ifndef XH_FONT_MARQUEE_H
#define XH_FONT_MARQUEE_H 28
#endif

/* 仍重影可试 LV_LABEL_LONG_SCROLL（非无缝循环，但 invalidation 更简单） */
#ifndef XH_FONT_MARQUEE_LONG_MODE
#define XH_FONT_MARQUEE_LONG_MODE LV_LABEL_LONG_SCROLL_CIRCULAR
#endif

static lv_font_t *s_bin_font;
/** 与 lv_font_stream_bin_create 一致，由 xh_probe_font_bin 在首次加载时解析 */
static const char *s_stream_bin_lv_path = XIAOHONG_LVGL_FONT_BIN_LV_PATH;

typedef struct {
    lv_obj_t *half;
    lv_obj_t *full;
} xh_binfont_ui_t;

static void xh_binfont_deferred_cb(lv_timer_t *timer)
{
    xh_binfont_ui_t *p = (xh_binfont_ui_t *)lv_timer_get_user_data(timer);

    if (p != NULL) {
        lv_free(p);
    }
}

lv_font_t *xh_lv_font_bin_get(void)
{
    return s_bin_font;
}

void xh_lv_font_bin_release_for_replace(void)
{
    if (s_bin_font != NULL) {
        lv_font_stream_bin_destroy(s_bin_font);
        s_bin_font = NULL;
    }
    s_stream_bin_lv_path = XIAOHONG_LVGL_FONT_BIN_LV_PATH;
}

/** 在 LittleFS 上探测 font.bin：优先与下载路径一致 system/font.bin，其次根目录 font.bin */
static const char *xh_probe_font_bin(unsigned int *fsz)
{
    static const struct {
        const char *rel;
        const char *lv;
    } k[] = {
        { XIAOHONG_FONT_LFS_REL_PATH_DEFAULT, XIAOHONG_LVGL_FONT_BIN_LV_PATH },
        { "font.bin", "L:/font.bin" },
    };

    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (fs_adapt_stat(k[i].rel, fsz) != 0) {
            continue;
        }
        if (*fsz < 64U) {
            printf("[xh_lv_font] skip: %s too small (%u bytes)\r\n", k[i].rel, *fsz);
            continue;
        }
        s_stream_bin_lv_path = k[i].lv;
        printf("[xh_lv_font] %s size=%u, stream load (cmap/loca in RAM, glyf on-demand)...\r\n", k[i].rel, *fsz);
        return k[i].rel;
    }
    return NULL;
}

void xh_lv_font_bin_init(void)
{
    lv_font_t *f;

    if (xh_lv_font_bin_get() != NULL) {
        return;
    }

    if (xh_lv_fs_littlefs_register_drv() != 0) {
        printf("[xh_lv_font] skip binfont: L: drive not bound\r\n");
        return;
    }

    if (fs_adapt_get_lfs() == NULL) {
        printf("[xh_lv_font] skip: LittleFS not mounted (fs_adapt_get_lfs NULL); check fs_adapt_mount in MainTask\r\n");
        return;
    }

    {
        unsigned int fsz = 0U;

        s_stream_bin_lv_path = XIAOHONG_LVGL_FONT_BIN_LV_PATH;
        if (xh_probe_font_bin(&fsz) == NULL) {
            printf("[xh_lv_font] skip: no valid font.bin (tried system/font.bin, font.bin).\r\n");
            return;
        }
    }

    f = lv_font_stream_bin_create(s_stream_bin_lv_path);
    printf("[xh_lv_font] lv_font_stream_bin_create returned %p\r\n", (void *)f);

    if (f == NULL) {
        printf("[xh_lv_font] stream bin font failed: %s\r\n",
            s_stream_bin_lv_path);
        return;
    }

    f->fallback = &lv_font_SourceHanSerifCN_SemiBold_22_Default;

    s_bin_font = f;
    printf("[xh_lv_font] stream bin font OK: %s\r\n", s_stream_bin_lv_path);
}
