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
 * @file lv_font_lfs_binfont.h
 * @brief 从 LittleFS 流式加载 LVGL bin 字库（font.bin）并可选应用到主界面标签。
 */
#ifndef LV_FONT_LFS_BINFONT_H
#define LV_FONT_LFS_BINFONT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 当前已加载的 bin 字库；未加载时为 NULL */
lv_font_t *xh_lv_font_bin_get(void);

/**
 * 关闭流式字库占用的 lv_fs 文件句柄（须从 LvglTask / lv_timer_handler 线程调用）。
 * 覆盖 /system/font.bin 前调用，否则 LittleFS 上 remove/rename 易因文件仍打开而失败。
 */
void xh_lv_font_bin_release_for_replace(void);

/**
 * 注册 L: 盘、加载 XIAOHONG_LVGL_FONT_BIN_LV_PATH，并在成功时将主文本区字体切换为流式 bin 字库，
 * // 同时写入中文/数字/标点测试文案（源文件为 UTF-8）。
 */
//void xh_lv_font_bin_init_demo(lv_obj_t *label_text_half, lv_obj_t *label_text_full);
void xh_lv_font_bin_init(void);
/**
 * 延迟若干毫秒后再执行 xh_lv_font_bin_init_demo（在 lv_timer_handler 上下文中调用，避免与首帧布局/LVGL 初始化叠在一起）。
 */
//void xh_lv_font_bin_schedule_demo(lv_obj_t *label_text_half, lv_obj_t *label_text_full);

/**
 * 横向滚动字幕 demo：内部会创建纯色「视口」并把 label 挂到其下，减轻 partial 刷新下的拖影。
 * 须在 xh_lv_font_bin_init_demo 成功之后调用；font 未加载时直接返回。
 * param marquee_label 已 create 的 lv_label（初始父对象一般为当前 screen）。
 */
//void xh_lv_font_bin_scroll_demo_setup(lv_obj_t *marquee_label, uint8_t full_screen);

/** 显示/隐藏滚动字幕（会同时处理 setup 创建的视口父对象，勿只对 label 设 HIDDEN）。 */
//void xh_lv_font_bin_scroll_demo_set_visible(lv_obj_t *marquee_label, bool show);

#ifdef __cplusplus
}
#endif

#endif /* LV_FONT_LFS_BINFONT_H */
