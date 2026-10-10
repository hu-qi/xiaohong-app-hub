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

#include "kb_keymap.h"
#include "nv.h"
#include "log.h"

// 内存中的键位映射，上电时从 NV 加载
static keymap_config_t g_keymap = {0};

errcode_t keymap_read_from_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;
    uint16_t nvlen = 0;

    memset_s(&g_keymap, sizeof(g_keymap), 0, sizeof(g_keymap));

    ret = uapi_nv_read(KB_KEYMAP_NV_ID, sizeof(g_keymap), &nvlen, (uint8_t *)&g_keymap);
    if (ret != ERRCODE_SUCC) {
        log_warn("[KeyMap] %s: uapi_nv_read NG, use defaults\r\n", __func__);
        return ret;
    }
    if ((g_keymap.magic != KB_KEYMAP_MAGIC) || (nvlen != sizeof(g_keymap))) {
        log_warn("[KeyMap] %s: magic/len invalid, use defaults\r\n", __func__);
        memset_s(&g_keymap, sizeof(g_keymap), 0, sizeof(g_keymap));
        return ERRCODE_FAIL;
    }
    log_info("[KeyMap] %s: OK, nvlen[%u]\r\n", __func__, nvlen);
    return ERRCODE_SUCC;
}

errcode_t keymap_write_to_nv(void)
{
    errcode_t ret = ERRCODE_FAIL;

    g_keymap.magic = KB_KEYMAP_MAGIC;
    ret = uapi_nv_write(KB_KEYMAP_NV_ID, (uint8_t *)&g_keymap, sizeof(g_keymap));
    if (ret != ERRCODE_SUCC) {
        log_error("[KeyMap] %s: uapi_nv_write NG\r\n", __func__);
    }
    return ret;
}

const keymap_action_t *keymap_get_action(uint8_t ekey)
{
    if (ekey >= eKey_Max) {
        return NULL;
    }
    if (g_keymap.keys[ekey].step_count == 0) {
        return NULL;    // 未自定义
    }
    return &g_keymap.keys[ekey];
}

errcode_t keymap_set_action(uint8_t ekey, const keymap_action_t *act)
{
    if (ekey >= eKey_Max) {
        log_error("[KeyMap] %s: ekey[%u] invalid\r\n", __func__, ekey);
        return ERRCODE_INVALID_PARAM;
    }
    if (act == NULL) {
        memset_s(&g_keymap.keys[ekey], sizeof(g_keymap.keys[ekey]), 0, sizeof(g_keymap.keys[ekey]));
    } else {
        if (act->step_count > KEYMAP_MACRO_MAX_STEPS) {
            log_error("[KeyMap] %s: step_count[%u] too large\r\n", __func__, act->step_count);
            return ERRCODE_INVALID_PARAM;
        }
        memcpy_s(&g_keymap.keys[ekey], sizeof(g_keymap.keys[ekey]), act, sizeof(*act));
    }
    return keymap_write_to_nv();
}

errcode_t keymap_reset_all(void)
{
    memset_s(&g_keymap, sizeof(g_keymap), 0, sizeof(g_keymap));
    return keymap_write_to_nv();
}
