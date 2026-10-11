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

#ifndef LITTLSFS_CONFIG_H
#define LITTLSFS_CONFIG_H

#include "w25q128.h"

#if defined(CONFIG_LFS_ADAPT_DEBUG) || defined(CONFIG_LFS_ADAPT_WARN) || defined(CONFIG_LFS_ADAPT_ERROR)
#include "debug_print.h"
#endif

#ifndef CONFIG_LFS_EXT_FLASH_START_ADDR
#define CONFIG_LFS_EXT_FLASH_START_ADDR     W25X_P0_ADDR
#endif

#ifndef CONFIG_LFS_EXT_FLASH_SIZE
#define CONFIG_LFS_EXT_FLASH_SIZE           W25X_P0_SIZE
#endif

#ifndef CONFIG_LFS_EXT_FLASH_READ_SIZE
#define CONFIG_LFS_EXT_FLASH_READ_SIZE      16U
#endif

#ifndef CONFIG_LFS_EXT_FLASH_PROG_SIZE
#define CONFIG_LFS_EXT_FLASH_PROG_SIZE      16U
#endif

#ifndef CONFIG_LFS_EXT_FLASH_BLOCK_SIZE
#define CONFIG_LFS_EXT_FLASH_BLOCK_SIZE     W25X_SECTOR_SIZE
#endif

#ifndef CONFIG_LFS_EXT_FLASH_CACHE_SIZE
#define CONFIG_LFS_EXT_FLASH_CACHE_SIZE     W25X_PAGE_SIZE
#endif

#ifndef CONFIG_LFS_EXT_FLASH_LOOKAHEAD_SIZE
#define CONFIG_LFS_EXT_FLASH_LOOKAHEAD_SIZE 32U
#endif

#ifndef CONFIG_LFS_EXT_FLASH_BLOCK_CYCLES
#define CONFIG_LFS_EXT_FLASH_BLOCK_CYCLES   500
#endif

#ifdef CONFIG_LFS_ADAPT_DEBUG
#define lfs_debug_print_info(fmt, arg...) print_str("LFS [I]:" fmt, ##arg)
#else
#define lfs_debug_print_info(fmt, arg...)
#endif

#ifdef CONFIG_LFS_ADAPT_WARN
#define lfs_debug_print_warning(fmt, arg...) print_str("LFS [W]:" fmt, ##arg)
#else
#define lfs_debug_print_warning(fmt, arg...)
#endif

#ifdef CONFIG_LFS_ADAPT_ERROR
#define lfs_debug_print_error(fmt, arg...) print_str("LFS [E]:" fmt, ##arg)
#else
#define lfs_debug_print_error(fmt, arg...)
#endif

#endif
