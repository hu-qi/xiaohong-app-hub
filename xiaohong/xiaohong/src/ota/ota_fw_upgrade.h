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

#ifndef __OTA_FW_UPGRADE_H__
#define __OTA_FW_UPGRADE_H__

#include <stdbool.h>
#include <stdint.h>
#include "errcode.h"

#define FOTA_VERSION_MAX     16
#define FOTA_URL_MAX        256
#define FOTA_SHA256_MAX     64

#define FOTA_STATE_MAP(OP) \
    OP(FOTA_STATE_IDLE, "IDLE") \
    OP(FOTA_STATE_CHECKING, "CHECKING") \
    OP(FOTA_STATE_DOWNLOADING, "DOWNLOADING") \
    OP(FOTA_STATE_DOWNLOADED, "DOWNLOADED") \
    OP(FOTA_STATE_VERIFYING, "VERIFYING") \
    OP(FOTA_STATE_UPGRADING, "UPGRADING") \
    OP(FOTA_STATE_REBOOTING, "REBOOTING") \
    OP(FOTA_STATE_SUCCESS, "SUCCESS") \
    OP(FOTA_STATE_FAILED, "FAILED")

#define FOTA_STATE_ENUM(s, str) s,
#define FOTA_STATE_STR(s, str) str,

typedef enum {
    FOTA_STATE_MAP(FOTA_STATE_ENUM)
    FOTA_STATE_MAX
} fota_state_t;

extern const char * const g_fota_state_str[];
#define FOTA_STATE_TO_STR(state) (((uint32_t)(state) < FOTA_STATE_MAX) ? g_fota_state_str[(uint32_t)(state)] : "UNKNOWN")

extern volatile uint8_t g_ota_fw_update_progress;

typedef struct {
    char version[FOTA_VERSION_MAX];
    char url[FOTA_URL_MAX];
    char sha256[FOTA_SHA256_MAX + 1];
    uint32_t size;
} fota_firmware_info_t;

typedef struct {
    fota_state_t state;
    uint32_t download_progress;
    uint32_t total_size;
    uint32_t downloaded_size;
    int32_t last_error;
} fota_status_t;

typedef void (*fota_progress_cb)(fota_state_t state, uint32_t progress, void *user_data);

errcode_t fota_init(void);
//errcode_t fota_check_and_upgrade(const char *fota_info_url, fota_progress_cb cb, void *user_data);
//errcode_t fota_get_version(char *version, uint32_t len);
//errcode_t fota_set_version(const char *version);
//errcode_t fota_get_status(fota_status_t *status);
void ota_fw_update_start(void);

#endif //__OTA_FW_UPGRADE_H__