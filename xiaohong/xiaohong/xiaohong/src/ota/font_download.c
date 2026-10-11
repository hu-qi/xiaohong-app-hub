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
 * @file font_download.c
 * @brief 字库：OTA 取 URL、LittleFS 路径清理、标记文件与 UI 通知；HTTP 流式落盘委托 http_lfs_download.c。
 */

#include "font_download.h"

#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmsis_os2.h"
#if defined(SUPPORT_LVGL)
#include "disp_driver.h"
#endif
#include "http_lfs_download.h"
#include "littlefs_adapt.h"
#include "ota.h"
#include "soc_osal.h"
#include "task_entry.h"
#include "log.h"

extern osMessageQueueId_t g_disp_event_qid;
extern osMessageQueueId_t g_main_event_qid;

volatile uint8_t g_font_dl_ui_percent = 0;

#define FD_SYSTEM_DIR "/system"
#define FD_PATH_MAX 96
#define FD_URL_TASK_MAX 256
#define FD_NAME_TASK_MAX 64

#define FONT_DL_MAX_ROUNDS 8U
#define FONT_DL_RETRY_MS_BASE 600U
#define FONT_DL_PROGRESS_STRIDE (32U * 1024U)

typedef struct {
    char url[FD_URL_TASK_MAX];
    char name[FD_NAME_TASK_MAX];
} fd_task_param_t;

static int fd_write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = (const unsigned char *)buf;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > (size_t)UINT_MAX) {
            chunk = (size_t)UINT_MAX;
        }
        int w = fs_adapt_write(fd, (const char *)(p + off), (unsigned int)chunk);
        if (w < 0) {
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

/**
 * 下载前删除已有目标文件以便覆盖。
 * 若目标为 system/font.bin，顺带删除根目录 font.bin，避免两套路径并存。
 */
static void fd_remove_existing_target(const char *path)
{
    unsigned int sz = 0U;

    if (fs_adapt_stat(path, &sz) == 0) {
        if (fs_adapt_delete(path) != 0) {
            log_error("[FontDwlo] remove %s failed (e.g. file open)\r\n", path);
        }
    }
    {
        const char *slash = strrchr(path, '/');
        const char *base = (slash != NULL) ? (slash + 1) : path;
        if (strcmp(base, "font.bin") == 0) {
            sz = 0U;
            if (fs_adapt_stat("font.bin", &sz) == 0) {
                if (fs_adapt_delete("font.bin") != 0) {
                    log_error("[FontDwlo] remove font.bin (root) failed\r\n");
                }
            }
        }
    }
}

static int fd_font_path_tmp(char *out, size_t out_sz, const char *path_final)
{
    if (snprintf(out, out_sz, "%s.tmp", path_final) >= (int)out_sz) {
        return -1;
    }
    return 0;
}

static uint8_t s_font_dl_last_queued_pct = FONT_DL_UI_PCT_UNKNOWN;

static void font_dl_lfs_progress(void *arg, size_t written, size_t total)
{
    (void)arg;
    uint8_t pct;

    if (total > 0U) {
        unsigned long long p = (unsigned long long)written * 100ULL / (unsigned long long)total;
        if (p > 99ULL) {
            p = 99ULL;
        }
        pct = (uint8_t)p;
    } else {
        pct = FONT_DL_UI_PCT_UNKNOWN;
    }

    g_font_dl_ui_percent = pct;
    if (total > 0U) {
        log_info("[FontDwlo] downloaded(%2u%%): %u : %u bytes\r\n", pct, total, written);
    } else {
        log_info("[FontDwlo] downloaded %u bytes (total unknown)\r\n", (unsigned int)written);
    }

    if (g_disp_event_qid == NULL) {
        return;
    }
    /* 显示队列仅 8 槽：百分比未变则不重复投递；无 Content-Length 时只投递一次“未知”刷新 */
    if (total > 0U) {
        if (pct == s_font_dl_last_queued_pct) {
            return;
        }
    } else if (s_font_dl_last_queued_pct == FONT_DL_UI_PCT_UNKNOWN) {
        return;
    }
    s_font_dl_last_queued_pct = pct;
    #if defined(SUPPORT_LVGL)
    uint8_t ev = (uint8_t)eDisp_Font_Dl_Progress;
    (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, 50U);
    #endif
}

/**
 * 首轮清空正式文件与 .tmp，避免沿用过期断点；后续轮次由 http_lfs 对 .tmp 做 Range 续传。
 */
static int font_download_run(const char *url, const char *local_basename)
{
    char path_final[FD_PATH_MAX];
    char path_tmp[FD_PATH_MAX];
    http_lfs_download_param_t param = {0};

    if (url == NULL || local_basename == NULL || url[0] == '\0' || local_basename[0] == '\0') {
        return -1;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        return -1;
    }
    if (strchr(local_basename, '/') != NULL || strstr(local_basename, "..") != NULL) {
        return -1;
    }
    if (strlen(local_basename) > 48U) {
        return -1;
    }
    if (snprintf(path_final, sizeof(path_final), "%s/%s", FD_SYSTEM_DIR, local_basename) >= (int)sizeof(path_final)) {
        return -1;
    }
    if (fd_font_path_tmp(path_tmp, sizeof(path_tmp), path_final) != 0) {
        return -1;
    }

    fd_remove_existing_target(path_final);
    fd_remove_existing_target(path_tmp);

    g_font_dl_ui_percent = 0;
    s_font_dl_last_queued_pct = FONT_DL_UI_PCT_UNKNOWN;

    param.url = url;
    param.base_dir = FD_SYSTEM_DIR;
    param.basename = local_basename;
    param.part_suffix = ".tmp";
    param.store_mode = HTTP_LFS_STORE_ATOMIC;
    param.progress_fn = font_dl_lfs_progress;
    param.progress_arg = NULL;
    param.progress_every_bytes = FONT_DL_PROGRESS_STRIDE;

    for (unsigned round = 1U; round <= FONT_DL_MAX_ROUNDS; round++) {
        int r = http_lfs_download_blocking(&param);
        if (r == 0) {
            return 0;
        }
        log_error("[FontDwlo] http_lfs round %u/%u failed, ret=%d\r\n", round, FONT_DL_MAX_ROUNDS, r);
        if (round < FONT_DL_MAX_ROUNDS) {
            osal_msleep(FONT_DL_RETRY_MS_BASE * round);
        }
    }

    (void)fs_adapt_delete(path_tmp);
    return -1;
}

int font_download_url_to_system(const char *url, const char *local_basename)
{
    return font_download_run(url, local_basename);
}

static void font_download_task_main(void *arg)
{
    fd_task_param_t *p = (fd_task_param_t *)arg;
    if (p != NULL) {
        (void)font_download_run(p->url, p->name);
        free(p);
    }
}

void font_download_start_task(const char *url, const char *local_basename)
{
    if (url == NULL || local_basename == NULL) {
        return;
    }
    fd_task_param_t *p = (fd_task_param_t *)malloc(sizeof(*p));
    if (p == NULL) {
        return;
    }
    memset(p, 0, sizeof(*p));
    if (snprintf(p->url, sizeof(p->url), "%s", url) >= (int)sizeof(p->url)) {
        free(p);
        return;
    }
    if (snprintf(p->name, sizeof(p->name), "%s", local_basename) >= (int)sizeof(p->name)) {
        free(p);
        return;
    }

    task_config_t task_conf = {
        "FontDlTask",
        1024 * 8,
        24,
        (void *)font_download_task_main
    };
    if (create_task(&task_conf, (void *)p) == 0) {
        free(p);
    }
}

uint32_t font_download_task_entry(void)
{
    return 0;
}

/* ─── 联网后一次性字库拉取（标记文件 + 独立任务） ─── */

#if defined(SUPPORT_LFS)

#include "net_wifi_config.h"
#include "ota_http.h"
#include "settings.h"

/** LittleFS 根目录标记文件（路径经适配层规范化，无前导 / 亦可） */
#define FONT_DL_MARKER_PATH "/font_dl_ok"
#define FONT_DL_MARKER_MAGIC "XH_FONT_OK_1\n"

static volatile uint8_t s_font_wifi_worker_spawned;
static osSemaphoreId_t s_font_fs_released_sem;

static void font_dl_sem_ensure(void)
{
    if (s_font_fs_released_sem == NULL) {
        s_font_fs_released_sem = osSemaphoreNew(1U, 0U, NULL);
    }
}

void font_download_notify_lvgl_released_bin(void)
{
    if (s_font_fs_released_sem != NULL) {
        (void)osSemaphoreRelease(s_font_fs_released_sem);
    }
}

/** 通知 LvglTask 释放流式字库文件句柄，并等待其完成（避免 lfs 上文件仍被 lv_fs 打开） */
static void font_dl_wait_lvgl_release_bin(void)
{
    font_dl_sem_ensure();
    if (g_disp_event_qid == NULL) {
        return;
    }
    #if defined(SUPPORT_LVGL)
    uint8_t ev = (uint8_t)eDisp_Font_Dl_ReleaseBin;
    (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, osWaitForever);
    #endif
    if (osSemaphoreAcquire(s_font_fs_released_sem, 10000U) != osOK) {
        log_info("[FontDwlo] timeout waiting for LVGL to release bin font\r\n");
    }
    osal_msleep(100U);
}

/* 不依赖 SUPPORT_LVGL：CMake 等链路可能未下发该宏，会导致提示永远不显示 */
static void font_dl_ui_put(uint8_t ev)
{
    if (g_disp_event_qid == NULL) {
        return;
    }
    #if defined(SUPPORT_LVGL)
    /* 显示队列仅 8 槽，非阻塞 Put 易丢；字库提示需尽量送达 */
    (void)osMessageQueuePut(g_disp_event_qid, &ev, 0, 50U);
    #endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static void font_dl_ui_pause_then_restore(void)
{
    #if defined(SUPPORT_LVGL)
    osal_msleep(2000U);
    font_dl_ui_put((uint8_t)eDisp_Font_Dl_Restore);
    #endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((unused))
#endif
static int font_download_marker_is_set(void)
{
    unsigned int sz = 0U;

    if (fs_adapt_stat(FONT_DL_MARKER_PATH, &sz) != 0) {
        return 0;
    }
    if (sz < 8U) {
        return 0;
    }

    int fd = fs_adapt_open(FONT_DL_MARKER_PATH, O_RDONLY);
    if (fd < 0) {
        return 0;
    }

    char buf[40];
    int n = fs_adapt_read(fd, buf, (unsigned int)(sizeof(buf) - 1U));
    (void)fs_adapt_close(fd);
    if (n < 0) {
        return 0;
    }
    buf[(size_t)n] = '\0';

    return (strncmp(buf, FONT_DL_MARKER_MAGIC, strlen(FONT_DL_MARKER_MAGIC)) == 0) ? 1 : 0;
}

static int font_download_marker_write_ok(void)
{
    int fd = fs_adapt_open(FONT_DL_MARKER_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        return -1;
    }

    const char *m = FONT_DL_MARKER_MAGIC;
    size_t ml = strlen(m);
    int ok = (fd_write_all(fd, m, ml) == 0) ? 0 : -1;
    (void)fs_adapt_sync(fd);
    (void)fs_adapt_close(fd);
    if (ok != 0) {
        return -1;
    }
    return 0;
}

/** 与 xh_lv_font 探测一致：system/font.bin 或根目录 font.bin，且不小于最小合法体积 */
#define FONT_DL_MIN_BIN_BYTES 64U

static int font_download_bin_file_present(void)
{
    unsigned int sz = 0U;
    int ret = 0;

    if (fs_adapt_stat("system/font.bin", &sz) == 0 && sz >= FONT_DL_MIN_BIN_BYTES) {
        ret = 1;
    }
    if (fs_adapt_stat("font.bin", &sz) == 0 && sz >= FONT_DL_MIN_BIN_BYTES) {
        ret = 1;
    }

    log_debug("[FontDwlo] font_download_bin_file_present: ret[%d]\r\n", ret);
    return ret;
}

int xh_font_bin_installed(void)
{
    return font_download_bin_file_present();
}

/** 与 `ota_check_task` 相同 POST body，用于从 OTA 检查 JSON 中取 `firmware.url` */
static int font_dl_ota_post_json_append(char *out, size_t cap, size_t *off, const char *s)
{
    if (s == NULL) {
        return 0;
    }
    size_t l = strlen(s);
    if (*off + l >= cap) {
        return -1;
    }
    memcpy(out + *off, s, l);
    *off += l;
    return 0;
}

static void font_download_wifi_worker(void *arg)
{
    (void)arg;

    const char *name = XIAOHONG_FONT_LOCAL_BASENAME;
    const char *url = NULL;
    ota_result_t ota_out;

    memset(&ota_out, 0, sizeof(ota_out));

    if (get_ota_url() != NULL) {
        char post_json[768];
        size_t pj = 0U;
        const char *dev_id_str = get_device_id_str();
        if (dev_id_str == NULL) {
            log_error("[FontDwlo] get_device_id_str NG\r\n");
            s_font_wifi_worker_spawned = 0;
            osThreadExit();
            return;
        }

        if (font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj,
                                          "{\"version\":2,\"language\":\"zh-CN\",\"mac_address\":\"") != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj, "\",\"uuid\":\"") != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj, dev_id_str) != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj,
                                         "\",\"application\":{\"version\":\"") != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj, get_font_ver_str()) != 0 ||
            font_dl_ota_post_json_append(post_json, sizeof(post_json), &pj,
                                         "\"},\"board\":{\"type\":\"xiaohong_font\"}}") != 0) {
            log_error("[FontDwlo] FontDown: post JSON build failed\r\n");
        } else {
            post_json[pj] = '\0';
            log_debug("[FontDwlo] FontDown: post JSON: len[%d]{{\r\n%s\r\n}}\r\n", (int)pj, post_json);

            ota_http_request_t ota_req = {0};
            ota_req.activation_version = "1";
            ota_req.device_id = dev_id_str;
            ota_req.client_id = dev_id_str;
            ota_req.post_body = post_json;
            ota_req.debug_print_io = 0U;

            errcode_t err = ota_check_version_http(get_ota_url(), get_fw_ver_str(),
                                                   &ota_req, NULL, &ota_out);
            if (err == ERRCODE_SUCC && ota_out.firmware_url[0] != '\0') {
                url = ota_out.firmware_url;
                log_info("[FontDwlo] FontDown: version[%s] url[%s]\r\n", ota_out.firmware_version, url);
            } else {
                log_info("[FontDwlo] FontDown: version[%s] url(empty), err=%d\r\n", ota_out.firmware_version, (int)err);
            }
        }
    }

    if (url == NULL || url[0] == '\0') {
        log_info("[FontDwlo] no download URL (OTA + fallback empty)\r\n");
        s_font_wifi_worker_spawned = 0;
        osThreadExit();
        return;
    }
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        log_info("[FontDwlo] unsupported URL (need http:// or https://)\r\n");
        s_font_wifi_worker_spawned = 0;
        osThreadExit();
        return;
    }

    log_info("[FontDwlo] Font download: -> /system/%s\r\n", name);
    font_dl_wait_lvgl_release_bin();

    g_font_dl_ui_percent = 0;
    #if defined(SUPPORT_LVGL)
    font_dl_ui_put((uint8_t)eDisp_Font_Dl_Progress);
    #endif
    int ret = font_download_url_to_system(url, name);
    if (ret == 0) {
        log_info("[FontDwlo] Font download: -> /system/%s [%s]\r\n", name, ota_out.firmware_version);
        set_font_version(ota_out.firmware_version);
        if (font_download_marker_write_ok() != 0) {
            log_error("[FontDwlo] Font download completed, marker write NG\r\n");
        } else {
            log_info("[FontDwlo] Font download completed, marker write OK\r\n");
        }
        #if defined(SUPPORT_LVGL)
        font_dl_ui_put((uint8_t)eDisp_Font_Dl_Complete);
        #endif

        uint8_t send_msg = eSettings_Font_Downloaded;
        osMessageQueuePut(g_main_event_qid, &send_msg, 0, 0);
    } else {
        log_error("[FontDwlo] Font download failed, ret=%d\r\n", ret);
        #if defined(SUPPORT_LVGL)
        font_dl_ui_put((uint8_t)eDisp_Font_Dl_Restore);
        #endif
    }

    s_font_wifi_worker_spawned = 0;
    osThreadExit();
}

uint32_t font_download_on_wifi_connected(void)
{
    if (s_font_wifi_worker_spawned != 0U) {
        return 0;
    }
    if (get_ota_url() == NULL) {
        log_info("[FontDwlo] no font download URL\r\n");
        return 0;
    }

    s_font_wifi_worker_spawned = 1U;

    task_config_t tc = {
        .name = "FontDlWiFi",
        .stack_size = 1024U * 24U,
        .priority = 23U,
        .func = (void *)font_download_wifi_worker,
    };
    uint32_t task_id = create_task(&tc, NULL);
    if (task_id == 0U) {
        log_error("[FontDwlo] create_task FontDlWiFi failed\r\n");
        s_font_wifi_worker_spawned = 0;
    } else {
        log_info("[FontDwlo] create_task FontDlWiFi OK\r\n");
    }
    return task_id;
}

#else /* !SUPPORT_LFS */

void font_download_notify_lvgl_released_bin(void)
{
}

uint32_t font_download_on_wifi_connected(void)
{
    return 0;
}

int xh_font_bin_installed(void)
{
    return 1;
}
#endif
