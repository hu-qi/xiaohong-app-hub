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
 * @file font_download.h
 * @brief 字库下载业务：URL 解析、/system 落盘路径、OTA 任务与标记文件；HTTP 传输见 http_lfs_download。
 */

#ifndef FONT_DOWNLOAD_H
#define FONT_DOWNLOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XIAOHONG_FONT_LOCAL_BASENAME
#define XIAOHONG_FONT_LOCAL_BASENAME  "font.bin"
#endif

/**
 * LvglTask 显示用：0–99 为真实下载百分比（完成前最高 99，完成见 eDisp_Font_Dl_Complete）；
 * FONT_DL_UI_PCT_UNKNOWN 表示无 Content-Length，界面勿显示假百分比。
 */
#define FONT_DL_UI_PCT_UNKNOWN ((uint8_t)255)
extern volatile uint8_t g_font_dl_ui_percent;

/**
 * 同步下载：阻塞调用方直到完成、失败或全局超时。
 * @param url            仅支持 http://（与工程 MG_TLS=0 一致）
 * @param local_basename 保存为 /system/<local_basename>，不得包含 '/'
 * @return 0 成功，-1 失败
 */
int font_download_url_to_system(const char *url, const char *local_basename);

/**
 * 创建一次性任务执行下载，任务结束后线程退出，不常驻占用栈。
 * url / local_basename 会在任务内拷贝，调用方可立即释放传入指针。
 */
void font_download_start_task(const char *url, const char *local_basename);

/**
 * WiFi 已连接且业务判定可上网时调用（例如在 STA+DHCP 就绪后）。
 * 内部启独立任务：等待 TCP 就绪 → 若根目录标记文件不存在则下载字库 → 成功后写标记。
 * 先向 `XIAOHONG_OTA_CHECK_URL` POST（与 OTA 检查相同 body），从响应 JSON `firmware.url` 取下载地址。
 * 本地文件名由 `XIAOHONG_FONT_LOCAL_BASENAME` 指定，保存于 `/system`。
 */
uint32_t font_download_on_wifi_connected(void);

/**
 * 合法字库是否已安装（与联网字库任务判定一致：system/font.bin 或 font.bin，且不小于最小体积）。
 * 无 SUPPORT_LFS 时恒为 1（不拦截后续流程）。
 */
int xh_font_bin_installed(void);

/**
 * LvglTask 在关闭流式 bin 字库文件句柄后调用，唤醒等待中的字库下载任务。
 * 必须在 lv_timer_handler 线程内调用（与 xh_lv_font_bin_release_for_replace 配对）。
 */
void font_download_notify_lvgl_released_bin(void);

#ifdef __cplusplus
}
#endif

#endif /* FONT_DOWNLOAD_H */
