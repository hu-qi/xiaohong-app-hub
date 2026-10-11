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
 * @file lv_fs_lfs_adapt.c
 * @brief 将 fs_adapt_get_lfs() 绑定到 LVGL LittleFS 驱动（SDK: lv_fs_littlefs.c）
 *
 * SDK 中 lv_littlefs_set_handler() 常见实现未检查 lv_fs_get_drv 返回值；
 * 若盘符尚未注册即 drv==NULL，写入 user_data 会立即崩溃（a0=0、mtval=0 等）。
 * 此处仅在 drv 非空时设置 user_data；必要时调用 lv_fs_littlefs_init() 一次。
 */

#include "lv_fs_lfs_adapt.h"

#include <stdio.h>

#include "lfs.h"
#include "littlefs_adapt.h"
#include "lvgl.h"

#if LV_USE_FS_LITTLEFS

void lv_fs_littlefs_init(void);

#ifndef LV_FS_LITTLEFS_LETTER
#define LV_FS_LITTLEFS_LETTER XH_LV_FS_LFS_LETTER
#endif

static int xh_fs_bind_lfs_to_letter(lfs_t *lfs)
{
    lv_fs_drv_t *drv;

    drv = lv_fs_get_drv((char)LV_FS_LITTLEFS_LETTER);
    if (drv == NULL) {
        lv_fs_littlefs_init();
        drv = lv_fs_get_drv((char)LV_FS_LITTLEFS_LETTER);
    }
    if (drv == NULL) {
        printf("[xh_lv_fs] lv_fs_get_drv('%c') NULL after init (check LV_USE_FS / lv_fs_littlefs in build)\r\n",
            (char)LV_FS_LITTLEFS_LETTER);
        return -1;
    }
    /* lvgl_task 与 xh_lv_font_bin_init_demo 都会调用 register：重复写 user_data 无益，仅首次打印 */
    if (drv->user_data == (void *)lfs) {
        return 0;
    }
    drv->user_data = lfs;
    printf("[xh_lv_fs] bound lfs_t* to LVGL letter '%c'\r\n", (char)LV_FS_LITTLEFS_LETTER);
    return 0;
}

#endif /* LV_USE_FS_LITTLEFS */

int xh_lv_fs_littlefs_register_drv(void)
{
#if !LV_USE_FS_LITTLEFS
    printf("[xh_lv_fs] LV_USE_FS_LITTLEFS is 0, skip (check lv_conf)\r\n");
    return -1;
#else
    lfs_t *lfs;

    lfs = fs_adapt_get_lfs();
    if (lfs == NULL) {
        printf("[xh_lv_fs] fs_adapt_get_lfs() NULL, mount LittleFS before LVGL uses L:\r\n");
        return -1;
    }

    return xh_fs_bind_lfs_to_letter(lfs);
#endif
}
