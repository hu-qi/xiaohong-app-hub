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
 * @file lv_fs_lfs_adapt.h
 * @brief 将本工程已挂载的 LittleFS（littlefs_adapt / lfs_t）挂到 LVGL 内置 fsdrv：lv_fs_littlefs.c
 *
 * SDK lv_conf 须同时满足（否则无法编译或无法打开字库）：
 * - LV_USE_FS = 1
 * - LV_USE_FS_LITTLEFS = 1
 * - LV_FS_LITTLEFS_LETTER = 'L'   （须为 A～Z，与下方 XH_LV_FS_LFS_LETTER 一致，否则 lv_fs_littlefs.c 会 #error）
 * - LV_FS_LITTLEFS_PATH 一般保持默认 ""；若打开文件失败再按 LVGL 文档调整路径前缀
 */
#ifndef LV_FS_LFS_ADAPT_H
#define LV_FS_LFS_ADAPT_H

#ifdef __cplusplus
extern "C" {
#endif

/** 与 xh_font_paths.h 中 "L:/system/font.bin" 一致，且须与 lv_conf 中 LV_FS_LITTLEFS_LETTER 相同 */
#define XH_LV_FS_LFS_LETTER 'L'

/**
 * 绑定已挂载的 lfs_t*：lv_littlefs_set_handler(fs_adapt_get_lfs())；若盘符未注册则先 lv_fs_littlefs_init()（WS63 SDK）。
 * 须在 fs_adapt_mount() 成功之后、lv_init() 之后、lv_binfont_create() 之前调用；可重复调用。
 * @return 0 成功绑定；非 0 失败（勿再调用 lv_binfont_create 使用 L: 路径）。
 */
int xh_lv_fs_littlefs_register_drv(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_FS_LFS_ADAPT_H */
