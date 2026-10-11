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
 * @file http_lfs_download.h
 * @brief Mongoose HTTP 客户端：流式下载到 LittleFS 指定目录（可断点续传、校验、临时文件原子替换）。
 *
 * 与字库/固件等业务无关；上层传入目录与文件名即可复用。
 */

#ifndef HTTP_LFS_DOWNLOAD_H
#define HTTP_LFS_DOWNLOAD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 下载进度回调。
 * @param bytes_written_total 已累计写入的字节（含 Range 续传起点）
 * @param total_bytes         预期总字节；HTTP 无 Content-Length 等无法获知时为 0
 */
typedef void (*http_lfs_progress_fn)(void *arg, size_t bytes_written_total, size_t total_bytes);

/** 服务端拒绝 Range（416）时内部用于触发全量重试 */
#define HTTP_LFS_RES_FALLBACK_FULL (-2)

/** 落盘策略：未初始化结构体时字段为 0，等价于 ATOMIC */
typedef enum {
    HTTP_LFS_STORE_ATOMIC = 0, /**< 先写 base_dir/basename+part_suffix，成功后 rename 覆盖 basename（中断时旧文件仍可用） */
    HTTP_LFS_STORE_DIRECT = 1, /**< 直接写 base_dir/basename，无临时文件、无 rename（中断可能留下不完整文件） */
} http_lfs_store_mode_t;

typedef struct {
    const char *url;          /**< 支持 http:// 与 https://（https 由 mongoose 内建 TLS 承载） */
    const char *base_dir;     /**< 例如 "/system"，LittleFS 路径 */
    const char *basename;     /**< 本地文件名，不得含 '/' 或 ".." */
    const char *part_suffix;  /**< 仅 ATOMIC：临时文件后缀；NULL 或 "" 则使用 ".part"；DIRECT 时忽略 */
    http_lfs_store_mode_t store_mode; /**< 默认 ATOMIC（0） */
    http_lfs_progress_fn progress_fn;
    void *progress_arg;
    size_t progress_every_bytes; /**< 0：不回调 */
} http_lfs_download_param_t;

/**
 * 阻塞下载：ATOMIC 时正式文件为 base_dir/basename，临时为 basename+part_suffix；DIRECT 时只写 basename。
 * @return 0 成功，-1 失败，HTTP_LFS_RES_FALLBACK_FULL 仅内部使用
 */
int http_lfs_download_blocking(const http_lfs_download_param_t *param);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_LFS_DOWNLOAD_H */
