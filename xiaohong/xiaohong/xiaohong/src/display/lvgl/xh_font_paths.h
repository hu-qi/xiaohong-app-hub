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
 * @file xh_font_paths.h
 * @brief LVGL 二进制字库在 lv_fs 中的路径（与 LittleFS /system/font.bin 对应，盘符 L）
 */
#ifndef XH_FONT_PATHS_H
#define XH_FONT_PATHS_H

#ifndef XIAOHONG_LVGL_FONT_BIN_LV_PATH
#define XIAOHONG_LVGL_FONT_BIN_LV_PATH "L:/system/font.bin"
#endif

/** 与 fs_adapt_stat / font_download 写入路径一致（相对 LFS 根、无盘符）；下载为 /system/<basename> → system/font.bin */
#ifndef XIAOHONG_FONT_LFS_REL_PATH_DEFAULT
#define XIAOHONG_FONT_LFS_REL_PATH_DEFAULT "system/font.bin"
#endif

#endif /* XH_FONT_PATHS_H */
