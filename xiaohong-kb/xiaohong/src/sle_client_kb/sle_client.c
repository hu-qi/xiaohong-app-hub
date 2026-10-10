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

#include <stdbool.h>
#include "string.h"
#include "common_def.h"
#include "ohos_init.h"
#include "cmsis_os2.h"

#include "osal_debug.h"
#include "osal_task.h"
#include "securec.h"
#include "efuse.h"
#include "pinctrl.h"
#include "errcode.h"

#include "log.h"
#include "settings.h"
#include "disp_driver.h"

#include "sle_client.h"
#define LOG_TAG             ""  // "[sle_client] "

#define SLE_MTU_SIZE_DEFAULT         512
#define SLE_SEEK_INTERVAL_DEFAULT    100
#define SLE_SEEK_WINDOW_DEFAULT      100
#define UUID_16BIT_LEN               2
#define UUID_128BIT_LEN              16

static uint8_t g_sle_client_ccc_val[2] = {0x03, 0x00};
static char g_sle_uuid_app_uuid[] = {
    0x39, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#if USE_OHOS_API
static SleConnectionCallbacks        g_sle_connect_cbk = {0};
#else
static sle_connection_callbacks_t    g_sle_connect_cbk = {0};
#endif
static sle_announce_seek_callbacks_t g_sle_seek_cbk    = {0};
static ssapc_callbacks_t             g_sle_ssapc_cbk   = {0};

ssapc_write_param_t g_sle_send_param = {0};
static char g_sle_server_name[128]   = "";
static sle_addr_t g_sle_server_addr  = {0};


#define SLE_SCAN_RESULT_MAX_COUNT      10
static sle_addr_t g_sle_scan_result[SLE_SCAN_RESULT_MAX_COUNT] = {0};
static uint8_t g_sle_scan_result_count = 0;


static uint8_t g_client_id           = 0;

static volatile uint8_t g_sle_scan_active = 0;
static volatile uint8_t g_sle_seek_active = 0;
/*
  每个 Client 最多同时连接 SLE_1_CLIENT_M_SERVER 个 Server,
  每个 Server 对应一个 sle_client_conn_slot_t 结构体，即一个连接槽
*/
static sle_client_conn_slot_t g_client_conn_slots[SLE_1_CLIENT_M_SERVER] = {0};

void sle_set_server_name(char *name)
{
    log_info("%s[sle_set_server_name] name[%s]\r\n", LOG_TAG, name);
    memcpy_s(g_sle_server_name, strlen(name), name, strlen(name));
}

void sle_clear_server_addr(void)
{
    memset(&g_sle_server_addr, 0, sizeof(sle_addr_t));
}
void sle_set_server_addr(const sle_addr_t *server_addr)
{
    if (server_addr == NULL) {
        log_error("%s[sle_set_server_addr] server_addr is NULL\r\n", LOG_TAG);
        return;
    }

    log_info("%s[sle_set_server_addr] addr[%02X:%02X:%02X:%02X:%02X:%02X]\r\n", LOG_TAG,
           server_addr->addr[0], server_addr->addr[1], server_addr->addr[2],
           server_addr->addr[3], server_addr->addr[4], server_addr->addr[5]);
    memcpy_s(&g_sle_server_addr, sizeof(sle_addr_t), server_addr, sizeof(sle_addr_t));
}

void sle_get_server_addr(sle_addr_t *server_addr)
{
    if (server_addr == NULL) {
        log_error("%s[sle_get_server_addr] server_addr is NULL\r\n", LOG_TAG);
        return;
    }
    memcpy_s(server_addr, sizeof(sle_addr_t), &g_sle_server_addr, sizeof(sle_addr_t));

#if 0
    log_debug("%s[sle_get_server_addr] addr[%02X:%02X:%02X:%02X:%02X:%02X]\r\n", LOG_TAG,
           server_addr->addr[0], server_addr->addr[1], server_addr->addr[2],
           server_addr->addr[3], server_addr->addr[4], server_addr->addr[5]);
#endif
}

static bool sle_client_addr_equal(const sle_addr_t *a, const sle_addr_t *b)
{
    if (a == NULL || b == NULL) {
        return false;
    }
    return (memcmp(a->addr, b->addr, SLE_ADDR_LEN) == 0);
}

uint8_t save_scan_result(sle_addr_t *server_addr)
{
    uint8_t i;

    if (server_addr == NULL) {
        log_error("%s[%s] server_addr is NULL\r\n", LOG_TAG, __func__);
        return g_sle_scan_result_count;
    }

    /* 不重复：已保存过的地址直接忽略 */
    for (i = 0; i < g_sle_scan_result_count; i++) {
        if (sle_client_addr_equal(&g_sle_scan_result[i], server_addr)) {
        #if 0
            log_debug("%s[%s] dup addr[%02X:%02X:%02X:%02X:%02X:%02X]\r\n",
                   LOG_TAG, __func__,
                   server_addr->addr[0], server_addr->addr[1], server_addr->addr[2],
                   server_addr->addr[3], server_addr->addr[4], server_addr->addr[5]);
        #endif
            return g_sle_scan_result_count;
        }
    }

    /* 无遗漏：缓冲区已满时，淘汰最旧的一条，再保存新结果 */
    if (g_sle_scan_result_count >= SLE_SCAN_RESULT_MAX_COUNT) {
        for (i = 1; i < SLE_SCAN_RESULT_MAX_COUNT; i++) {
            (void)memcpy_s(&g_sle_scan_result[i - 1], sizeof(sle_addr_t),
                           &g_sle_scan_result[i], sizeof(sle_addr_t));
        }
        g_sle_scan_result_count = (uint8_t)(SLE_SCAN_RESULT_MAX_COUNT - 1);
    }

    (void)memcpy_s(&g_sle_scan_result[g_sle_scan_result_count], sizeof(sle_addr_t),
                   server_addr, sizeof(sle_addr_t));
    g_sle_scan_result_count++;

    log_debug("%s[%s] saved addr[%02X:%02X:%02X:%02X:%02X:%02X] count[%u]\r\n",
           LOG_TAG, __func__,
           server_addr->addr[0], server_addr->addr[1], server_addr->addr[2],
           server_addr->addr[3], server_addr->addr[4], server_addr->addr[5],
           (unsigned)g_sle_scan_result_count);
    return g_sle_scan_result_count;
}
void clear_scan_result(void)
{
    g_sle_scan_result_count = 0;
    memset(&g_sle_scan_result, 0, sizeof(g_sle_server_addr)*SLE_SCAN_RESULT_MAX_COUNT);
}

sle_addr_t* get_scan_result(uint8_t* count)
{
    if (count == NULL) {
        return NULL;
    }
    *count = g_sle_scan_result_count;
    return g_sle_scan_result;
}

void accept_scan_result(uint8_t idx)
{
    if (g_sle_scan_result_count > 0) {
        if (idx >= g_sle_scan_result_count) {
            idx = g_sle_scan_result_count - 1;
        }
        sle_set_server_addr(&g_sle_scan_result[idx]);
    }
}

uint32_t sle_client_get_max_slots(void)
{
    return SLE_1_CLIENT_M_SERVER;
}

sle_client_conn_slot_t *sle_get_client_slots(void)
{
    return g_client_conn_slots;
}

static uint32_t sle_client_slot_count_by_state(sle_client_slot_state_t state)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state == state) {
            n++;
        }
    }
    return n;
}

static uint32_t sle_client_slot_busy_count(void)
{
    return sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) +
           sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED);
}

uint32_t sle_client_get_conn_count(void)
{
    return sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED);
}

uint32_t sle_client_get_ready_count(void)
{
    uint32_t ready = 0;
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state != SLE_CLIENT_SLOT_CONNECTED) {
            continue;
        }
        if (g_client_conn_slots[i].service.property_ready != 0 &&
            g_client_conn_slots[i].service.property_handle != 0) {
            ready++;
        }
    }
    return ready;
}

uint32_t sle_client_get_ntf_subscribed_count(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state != SLE_CLIENT_SLOT_CONNECTED) {
            continue;
        }
        if (g_client_conn_slots[i].service.property_ready != 0 &&
            g_client_conn_slots[i].service.property_handle != 0 &&
            g_client_conn_slots[i].service.ntf_subscribed != 0) {
            n++;
        }
    }
    return n;
}



void sle_client_slot_reset(uint32_t slot)
{
    if (slot >= sle_client_get_max_slots()) {
        return;
    }
    memset_s(&g_client_conn_slots[slot], sizeof(g_client_conn_slots[slot]),
             0, sizeof(g_client_conn_slots[slot]));
}

static int32_t sle_client_slot_find_by_addr(const sle_addr_t *addr,
                                            sle_client_slot_state_t state)
{
    if (addr == NULL) {
        return -1;
    }
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state != state) {
            continue;
        }
        if (sle_client_addr_equal(&g_client_conn_slots[i].server_addr, addr)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sle_client_slot_find_by_addr_busy(const sle_addr_t *addr)
{
    if (addr == NULL) {
        return -1;
    }
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state == SLE_CLIENT_SLOT_IDLE) {
            continue;
        }
        if (sle_client_addr_equal(&g_client_conn_slots[i].server_addr, addr)) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sle_client_slot_find_idle(void)
{
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state == SLE_CLIENT_SLOT_IDLE) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sle_client_slot_find_by_conn_id(uint16_t conn_id)
{
    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state == SLE_CLIENT_SLOT_CONNECTED &&
            g_client_conn_slots[i].service.conn_id == conn_id) {
            return (int32_t)i;
        }
    }
    return -1;
}

static int32_t sle_client_conn_index_by_id(uint16_t conn_id)
{
    return sle_client_slot_find_by_conn_id(conn_id);
}

static bool sle_client_addr_in_use(const sle_addr_t *addr)
{
    return sle_client_slot_find_by_addr_busy(addr) >= 0;
}

static void sle_client_mark_connecting(const sle_addr_t *addr)
{
    int32_t slot;

    if (addr == NULL) {
        return;
    }
    if (sle_client_addr_in_use(addr)) {
        return;
    }
    slot = sle_client_slot_find_idle();
    if (slot < 0) {
        return;
    }
    g_client_conn_slots[slot].state = SLE_CLIENT_SLOT_CONNECTING;
    (void)memcpy_s(&g_client_conn_slots[slot].server_addr, sizeof(sle_addr_t),
                   addr, sizeof(sle_addr_t));
}

static void sle_client_clear_connecting(const sle_addr_t *addr)
{
    int32_t slot = sle_client_slot_find_by_addr(addr, SLE_CLIENT_SLOT_CONNECTING);
    if (slot >= 0) {
        sle_client_slot_reset((uint8_t)slot);
        sle_clear_server_addr();
    }
}

static void sle_client_slot_on_connected(uint32_t slot, uint16_t conn_id, const sle_addr_t *addr)
{
    sle_conn_and_service_t *svc;

    if (slot >= sle_client_get_max_slots() || addr == NULL) {
        return;
    }
    svc = &g_client_conn_slots[slot].service;
    memset_s(svc, sizeof(*svc), 0, sizeof(*svc));
    svc->conn_id = conn_id;
    (void)memcpy_s(&g_client_conn_slots[slot].server_addr, sizeof(sle_addr_t),
                   addr, sizeof(sle_addr_t));
    g_client_conn_slots[slot].state = SLE_CLIENT_SLOT_CONNECTED;

    sle_set_server_addr(addr);
}

void sle_client_try_stop_scan(void)
{
    if (g_sle_seek_active == 0) {
        return;
    }
    g_sle_scan_active = 1;
#if USE_OHOS_API
    (void)SleStopSeek();
#else
    (void)sle_stop_seek();
#endif
    g_sle_seek_active = 0;
    g_sle_scan_active = 0;
    log_debug("%s[%s] stop_seek busy[%u] conn[%u] connecting[%u]\r\n",
           LOG_TAG, __func__,
           (unsigned)sle_client_slot_busy_count(),
           (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED),
           (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING));
    log_debug("%s =========== [sle_client_try_stop_scan] ===========\r\n", LOG_TAG);
}

static void sle_client_try_resume_scan(void)
{
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots()) {
        sle_client_try_stop_scan();
        return;
    }
    /* WS63 协议栈同时只处理一路 outbound connect，须等当前 CONNECTING 结束再扫下一个 */
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) > 0) {
        return;
    }
    osal_msleep(200);
    sle_start_scan();
}

static void sle_client_ccc_v106_combo_write(uint8_t client_id, uint16_t conn_id, int32_t idx)
{
    if (idx < 0 || g_client_conn_slots[idx].service.ccc_value_fallback_tried != 0) {
        return;
    }
    uint16_t val_hdl = g_client_conn_slots[idx].service.property_handle;
    uint16_t ccc_hdl = g_client_conn_slots[idx].service.ccc_handle;
    if (val_hdl == 0U) {
        return;
    }
    if (ccc_hdl == 0U) {
        ccc_hdl = (uint16_t)(val_hdl + 1U);
        g_client_conn_slots[idx].service.ccc_handle = ccc_hdl;
    }

    g_client_conn_slots[idx].service.ccc_value_fallback_tried = 1;
    g_client_conn_slots[idx].service.ccc_desc_pending = 0;
    g_client_conn_slots[idx].service.ccc_verify_pending = 0;
    g_client_conn_slots[idx].service.ccc_prime_pending = 0;

    ssapc_write_param_t c_w_param = {0};
    c_w_param.handle   = ccc_hdl;
    c_w_param.type     = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    c_w_param.data_len = sizeof(g_sle_client_ccc_val);
    c_w_param.data     = g_sle_client_ccc_val;

    errcode_t wc_ret = ssapc_write_cmd(client_id, conn_id, &c_w_param);
    log_info("%s[%s] write_cmd DESC h[0x%04X] ret[0x%02X]\r\n",
           LOG_TAG, __func__, ccc_hdl, (unsigned)wc_ret);

    osal_mdelay(120);

    g_client_conn_slots[idx].service.ccc_desc_pending = 1;
    errcode_t wr_ret = ssapc_write_req(client_id, conn_id, &c_w_param);
    log_info("%s[%s] write_req DESC h[0x%04X] ret[0x%02X]\r\n",
           LOG_TAG, __func__, ccc_hdl, (unsigned)wr_ret);

    if (wr_ret == ERRCODE_SUCC || wr_ret == (errcode_t)ERRCODE_SLE_SUCCESS) {
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
        log_info("%s[%s] write_req DESC sent -> subscribed\r\n", LOG_TAG, __func__);
    } else if (wc_ret == ERRCODE_SUCC || wc_ret == (errcode_t)ERRCODE_SLE_SUCCESS) {
        g_client_conn_slots[idx].service.ccc_desc_pending = 0;
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
        log_info("%s[%s] trust write_cmd DESC -> subscribed\r\n", LOG_TAG, __func__);
    } else {
        g_client_conn_slots[idx].service.ccc_desc_pending = 0;
    }
}

static void sle_client_ccc_value_fallback_write(uint8_t client_id, uint16_t conn_id, int32_t idx)
{
    sle_client_ccc_v106_combo_write(client_id, conn_id, idx);
}

static bool sle_client_read_looks_like_cccd(const ssapc_handle_value_t *rd, uint16_t expect_hdl)
{
    if (rd == NULL || rd->data == NULL || rd->data_len < 2) {
        return false;
    }
    if (rd->handle != 0 && rd->handle != expect_hdl) {
        return false;
    }
    if (rd->data[1] != 0) {
        return false;
    }
    if ((rd->data[0] & (uint8_t)~3u) != 0) {
        return false;
    }
    return true;
}

static void sle_client_ccc_start_verify_read(uint8_t client_id, uint16_t conn_id, int32_t idx, uint16_t ccc_hdl)
{
    g_client_conn_slots[idx].service.ccc_verify_pending = 1;
    g_client_conn_slots[idx].service.ccc_verify_retries = 0;
    osal_mdelay(100);

    errcode_t rr = ssapc_read_req(client_id, conn_id, ccc_hdl, SSAP_DESCRIPTOR_CLIENT_CONFIGURATION);
    log_info("%s[%s] read CCCD h[0x%04X] ret[0x%02X]\r\n", LOG_TAG, __func__, ccc_hdl, (unsigned)rr);
    if (!(rr == ERRCODE_SUCC || rr == (errcode_t)ERRCODE_SLE_SUCCESS)) {
        g_client_conn_slots[idx].service.ccc_verify_pending = 0;
        sle_client_ccc_v106_combo_write(client_id, conn_id, idx);
    }
}

static errcode_t sle_client_ccc_issue_desc_write(uint8_t client_id, uint16_t conn_id, int32_t idx, uint16_t ccc_hdl)
{
    ssapc_write_param_t w = {0};

    w.handle   = ccc_hdl;
    w.type     = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    w.data_len = sizeof(g_sle_client_ccc_val);
    w.data     = g_sle_client_ccc_val;

    errcode_t rc = ssapc_write_cmd(client_id, conn_id, &w);
    log_info("%s[%s] write_cmd conn_id[0x%02X] h[0x%04X] ret[0x%02X]\r\n",
           LOG_TAG, __func__, conn_id, ccc_hdl, (unsigned)rc);
    if (rc == ERRCODE_SUCC || rc == (errcode_t)ERRCODE_SLE_SUCCESS) {
        sle_client_ccc_start_verify_read(client_id, conn_id, idx, ccc_hdl);
        return ERRCODE_SUCC;
    }

    g_client_conn_slots[idx].service.ccc_desc_pending = 1;
    errcode_t r1 = ssapc_write_req(client_id, conn_id, &w);
    log_info("%s[%s] write_req DESC conn_id[0x%02X] h[0x%04X] ret[0x%02X]\r\n",
           LOG_TAG, __func__, conn_id, ccc_hdl, (unsigned)r1);
    if (!(r1 == ERRCODE_SUCC || r1 == (errcode_t)ERRCODE_SLE_SUCCESS)) {
        g_client_conn_slots[idx].service.ccc_desc_pending = 0;
        sle_client_ccc_value_fallback_write(client_id, conn_id, idx);
    }
    return r1;
}

static void sle_client_ccc_begin_subscribe(uint8_t client_id, uint16_t conn_id, int32_t idx)
{
    uint16_t val_hdl = g_client_conn_slots[idx].service.property_handle;
    uint16_t ccc_hdl = g_client_conn_slots[idx].service.ccc_handle;
    if (ccc_hdl == 0U && val_hdl != 0U) {
        ccc_hdl = (uint16_t)(val_hdl + 1U);
        g_client_conn_slots[idx].service.ccc_handle = ccc_hdl;
    }

    log_info("%s[%s] conn_id[0x%02X] val[0x%04X] ccc[0x%04X]\r\n",
           LOG_TAG, __func__, conn_id, val_hdl, ccc_hdl);
    sle_client_ccc_v106_combo_write(client_id, conn_id, idx);
}

static errcode_t sle_client_try_subscribe_notify(uint8_t client_id, uint16_t conn_id)
{
    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return ERRCODE_FAIL;
    }
    if (g_client_conn_slots[idx].service.property_handle == 0U) {
        return ERRCODE_FAIL;
    }
    if (g_client_conn_slots[idx].service.ntf_subscribed != 0) {
        return ERRCODE_SUCC;
    }

    osal_mdelay(80);
    sle_client_ccc_begin_subscribe(client_id, conn_id, idx);
    return ERRCODE_SUCC;
}

ssapc_write_param_t *sle_get_send_param(void)
{
    return &g_sle_send_param;
}

#if USE_OHOS_API
static errcode_t sle_uuid_client_register(void)
{
    SleUuid app_uuid = {0};
    app_uuid.len = sizeof(g_sle_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, SLE_UUID_LEN, g_sle_uuid_app_uuid, sizeof(g_sle_uuid_app_uuid)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    log_info("%s[%s] g_client_id[0x%02X]\r\n", LOG_TAG, __func__, g_client_id);
    return SsapcRegisterClient(&app_uuid, &g_client_id);
}
#else
static errcode_t sle_uuid_client_register(void)
{
    sle_uuid_t app_uuid = {0};;
    app_uuid.len = sizeof(g_sle_uuid_app_uuid);
    if (memcpy_s(app_uuid.uuid, SLE_UUID_LEN, g_sle_uuid_app_uuid, sizeof(g_sle_uuid_app_uuid)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }

    log_info("%s[%s] g_client_id[0x%02X]\r\n", LOG_TAG, __func__, g_client_id);
    return ssapc_register_client(&app_uuid, &g_client_id);
}
#endif

#if USE_OHOS_API
void sle_start_scan(void)
{
    SleSeekParam param = {0};
    errcode_t ret;

    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots()) {
        sle_client_try_stop_scan();
        return;
    }
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) > 0) {
        return;
    }

    g_sle_scan_active = 1;
    (void)SleStopSeek();
    osal_msleep(50);

    param.ownaddrtype      = SLE_ADDRESS_TYPE_PUBLIC;
    param.filterduplicates = 0;
    param.seekfilterpolicy = SLE_SEEK_FILTER_ALLOW_ALL;
    param.seekphys         = SLE_SEEK_PHY_1M;
    param.seekType[0]      = SLE_SEEK_ACTIVE;
    param.seekInterval[0]  = SLE_SEEK_INTERVAL_DEFAULT;
    param.seekWindow[0]    = SLE_SEEK_WINDOW_DEFAULT;

    (void)SleSetSeekParam(&param);
    ret = (errcode_t)SleStartSeek();
    g_sle_scan_active = 0;
    if (ret == ERRCODE_SUCC || ret == (errcode_t)ERRCODE_SLE_SUCCESS) {
        g_sle_seek_active = 1;
    }
#if 0
    log_info("%s[%s] start_seek ret[0x%02X] busy[%u] conn[%u]\r\n",
           LOG_TAG, __func__, (unsigned)ret,
           (unsigned)sle_client_slot_busy_count(),
           (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED));
#endif
}
#else
void sle_start_scan(void)
{
    sle_seek_param_t param = {0};
    errcode_t ret;

    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots()) {
        sle_client_try_stop_scan();
        return;
    }
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) > 0) {
        return;
    }

    g_sle_scan_active = 1;
    (void)sle_stop_seek();
    osal_msleep(50);

    param.own_addr_type      = SLE_ADDRESS_TYPE_PUBLIC;    // 0
    param.filter_duplicates  = 0;
    param.seek_filter_policy = SLE_SEEK_FILTER_ALLOW_ALL;  // 0
    param.seek_phys          = SLE_SEEK_PHY_1M;            // 1
    param.seek_type[0]       = SLE_SEEK_ACTIVE;            // 1
    param.seek_interval[0]   = SLE_SEEK_INTERVAL_DEFAULT;  // 100
    param.seek_window[0]     = SLE_SEEK_WINDOW_DEFAULT;    // 100

    (void)sle_set_seek_param(&param);
    ret = sle_start_seek();
    g_sle_scan_active = 0;
    if (ret == ERRCODE_SUCC || ret == (errcode_t)ERRCODE_SLE_SUCCESS) {
        g_sle_seek_active = 1;
    }
#if 0
    log_info("%s[%s] start_seek ret[0x%02X] busy[%u] conn[%u]\r\n",
           LOG_TAG, __func__, (unsigned)ret,
           (unsigned)sle_client_slot_busy_count(),
           (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED));
#endif
}
#endif

static void sle_client_sle_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        log_error("%s[%s] status error\r\n", LOG_TAG, __func__);
    } else {
        osal_msleep(1000);
        sle_start_scan();
    }
}

static void sle_client_seek_enable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        log_error("%s[%s] status error\r\n", LOG_TAG, __func__);
    }
}

#if USE_OHOS_API
static void sle_client_seek_result_info_cbk(SleSeekResultInfo *seek_result_data)
#else
static void sle_client_seek_result_info_cbk(sle_seek_result_info_t *seek_result_data)
#endif
{
    static uint8_t scan_result_cnt = 0xFF;
    uint8_t count = 0;

    sle_addr_t target_addr = {0};
    errcode_t conn_ret;

    if (seek_result_data == NULL) {
        log_error("%s[%s] seek_result_data is NULL\r\n", LOG_TAG, __func__);
        return;
    }

    if (strstr((const char *)seek_result_data->data, g_sle_server_name) != NULL) {
        if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots()) {
            sle_client_try_stop_scan();
            return;
        }
        if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) > 0) {
            return;
        }
        if (sle_client_addr_in_use(&seek_result_data->addr)) {
            return;
        }
        if (sle_client_slot_find_idle() < 0) {
            return;
        }

        sle_get_server_addr(&target_addr);
        if (memcmp(&target_addr, &seek_result_data->addr, sizeof(sle_addr_t)) != 0) {
            count = save_scan_result(&seek_result_data->addr);
            if (scan_result_cnt != count) {
                scan_result_cnt = count;
                log_info("%s[%s] scan_result_cnt[%d]\r\n", LOG_TAG, __func__, scan_result_cnt);
                post_msg_event(g_disp_event_qid, eDisp_Show_Sle_Pairing_Page);
            } else {
                //post_msg_event(g_disp_event_qid, eDisp_All_Update);
            }
            return;
        }

        log_info("%s[%s] target server addr: [%02X:%02X:%02X:%02X:%02X:%02X] busy[%u] conn[%u] connecting[%u]\r\n",
                    LOG_TAG, __func__,
                    seek_result_data->addr.addr[0], seek_result_data->addr.addr[1],
                    seek_result_data->addr.addr[2], seek_result_data->addr.addr[3],
                    seek_result_data->addr.addr[4], seek_result_data->addr.addr[5],
                    (unsigned)sle_client_slot_busy_count(),
                    (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED),
                    (unsigned)sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING));

        sle_client_mark_connecting(&target_addr);

    #if USE_OHOS_API
        conn_ret = (errcode_t)SleConnectRemoteDevice(&target_addr);
    #else
        conn_ret = sle_connect_remote_device(&target_addr);
    #endif
    log_info("%s[%s] connect ret[0x%02X]\r\n", LOG_TAG, __func__, (unsigned)conn_ret);
        if (conn_ret != ERRCODE_SUCC && conn_ret != (errcode_t)ERRCODE_SLE_SUCCESS) {
            sle_client_clear_connecting(&target_addr);
            sle_client_try_resume_scan();
        } else {
            set_dongle_mac((uint8_t*)target_addr.addr, SLE_ADDR_LEN);  // 连接成功的目标dongle的mac落盘保存
            sle_client_try_stop_scan();
            clear_scan_result();
        }
    }
}

static void sle_client_seek_disable_cbk(errcode_t status)
{
    if (status != ERRCODE_SUCC) {
        log_error("%s[%s] status error = %x\r\n", LOG_TAG, __func__, status);
    }
    if (g_sle_scan_active != 0) {
        return;
    }
    g_sle_seek_active = 0;
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTING) > 0) {
        return;
    }
    if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots()) {
        return;
    }
    sle_client_try_resume_scan();
}

static void sle_client_seek_cbk_register(void)
{
    g_sle_seek_cbk.sle_enable_cb   = sle_client_sle_enable_cbk;
    g_sle_seek_cbk.seek_enable_cb  = sle_client_seek_enable_cbk;
    g_sle_seek_cbk.seek_result_cb  = sle_client_seek_result_info_cbk;
    g_sle_seek_cbk.seek_disable_cb = sle_client_seek_disable_cbk;
    sle_announce_seek_register_callbacks(&g_sle_seek_cbk);
}

static void sle_client_connect_state_changed_cbk(uint16_t conn_id,
                                                 const sle_addr_t *addr,
                                                 sle_acb_state_t conn_state,
                                                 sle_pair_state_t pair_state,
                                                 sle_disc_reason_t disc_reason)
{
    int32_t slot = -1;

    log_info("%s[%s] "
        "conn_id[0x%02X] conn_state[0x%02X] pair_state[0x%02X] disc_reason[0x%02X]\r\n",
        LOG_TAG, __func__, conn_id, conn_state, pair_state, disc_reason);
        log_info("%s[%s] "
        "server_addr: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
        LOG_TAG, __func__,
        addr->addr[0], addr->addr[1], addr->addr[2],
        addr->addr[3], addr->addr[4], addr->addr[5]);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        slot = sle_client_slot_find_by_addr(addr, SLE_CLIENT_SLOT_CONNECTING);

        if (slot < 0) {
            slot = sle_client_slot_find_idle();
        }
        if (slot < 0) {
            log_error("%s[%s] no free slot\r\n", LOG_TAG, __func__);
            return;
        }
        if (sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED) >= sle_client_get_max_slots() &&
            g_client_conn_slots[slot].state != SLE_CLIENT_SLOT_CONNECTING) {
            log_info("%s[%s] max server conn reached\r\n", LOG_TAG, __func__);
            sle_client_slot_reset((uint32_t)slot);
            return;
        }

        sle_client_slot_on_connected((uint32_t)slot, conn_id, addr);

        log_info("%s[%s] "
            "SLE_ACB_STATE_CONNECTED: slot[%d] conn[%u] conn_id[0x%02X]\r\n",
            LOG_TAG, __func__, slot, sle_client_slot_count_by_state(SLE_CLIENT_SLOT_CONNECTED), conn_id);

    #if USE_OHOS_API
        SsapcExchangeInfo info = {0};
        info.mtuSize = SLE_MTU_SIZE_DEFAULT;
        info.version = 1;
        SsapcExchangeInfoReq(g_client_id, conn_id, &info);
    #else
        ssap_exchange_info_t info = {0};
        info.mtu_size = SLE_MTU_SIZE_DEFAULT;
        info.version = 1;
        ssapc_exchange_info_req(g_client_id, conn_id, &info);
    #endif

        sle_client_try_resume_scan();
    } else if (conn_state == SLE_ACB_STATE_NONE) {
        log_info("%s[%s] SLE_ACB_STATE_NONE\r\n", LOG_TAG, __func__);
        sle_client_clear_connecting(addr);
        sle_client_try_resume_scan();
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {

        log_info("%s[%s] SLE_ACB_STATE_DISCONNECTED: conn_id[0x%02X]\r\n",
            LOG_TAG, __func__, conn_id);

        sle_client_clear_connecting(addr);

        slot = sle_client_slot_find_by_conn_id(conn_id);
        if (slot >= 0) {
            sle_client_slot_reset((uint32_t)slot);
        }

        sle_client_try_resume_scan();
    } else {
        log_error("%s[%s] status error\r\n", LOG_TAG, __func__);
    }
}

static void sle_client_connect_cbk_register(void)
{
#if USE_OHOS_API
    g_sle_connect_cbk.connectStateChangedCb = sle_client_connect_state_changed_cbk;
    SleConnectionRegisterCallbacks(&g_sle_connect_cbk);
#else
    g_sle_connect_cbk.connect_state_changed_cb = sle_client_connect_state_changed_cbk;
    sle_connection_register_callbacks(&g_sle_connect_cbk);
#endif
}

static void sle_client_exchange_info_cbk(uint8_t client_id, uint16_t conn_id,
                                         ssap_exchange_info_t *param,
                                         errcode_t status)
{
    log_info("%s[%s] "
        "client_id[0x%02X] conn_id[0x%02X] status[0x%02X] "
        "mtu_size[0x%02X] version[0x%02X]\r\n",
        LOG_TAG, __func__, client_id, conn_id, status,
        param->mtu_size, param->version);

    if (status == ERRCODE_SUCC || status == (errcode_t)ERRCODE_SLE_SUCCESS) {
        ssapc_find_structure_param_t find_param = {0};
        find_param.type      = SSAP_FIND_TYPE_PRIMARY_SERVICE;
        find_param.start_hdl = 1;
        find_param.end_hdl   = 0xFFFF;
        ssapc_find_structure(client_id, conn_id, &find_param);
    }
}

static void sle_client_disc_start_property_find(uint8_t client_id, uint16_t conn_id)
{
    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return;
    }
    if (g_client_conn_slots[idx].service.property_ready != 0) {
        return;
    }
    uint16_t s_hdl = g_client_conn_slots[idx].service.find_service_result.start_hdl;
    if (s_hdl == 0) {
        log_error("%s[%s] start_hdl=0, skip conn_id[0x%02X]\r\n", LOG_TAG, __func__, conn_id);
        return;
    }
    ssapc_find_structure_param_t fp = {0};
    fp.type = SSAP_FIND_TYPE_PROPERTY;
    fp.start_hdl = s_hdl;
    fp.end_hdl = 0xFFFF;
    errcode_t pret = ssapc_find_structure(client_id, conn_id, &fp);
    log_info("%s[%s] ssapc_find_structure(PROPERTY) ret=0x%x start_hdl=0x%x conn_id[0x%02X]\r\n",
           LOG_TAG, __func__, (unsigned)pret, (unsigned)s_hdl, conn_id);
}

static void sle_client_find_structure_cbk(uint8_t client_id, uint16_t conn_id,
                                          ssapc_find_service_result_t *service,
                                          errcode_t status)
{
    if (service == NULL) {
        return;
    }

    log_debug("%s[%s] "
        "client_id[0x%02X] conn_id[0x%02X] status[0x%02X] "
        "start_hdl[0x%02X] end_hdl[0x%02x] uuid.len[0x%02X]\r\n",
        LOG_TAG, __func__, client_id, conn_id, status,
        service->start_hdl, service->end_hdl, service->uuid.len);

    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return;
    }
    g_client_conn_slots[idx].service.find_service_result.start_hdl = service->start_hdl;
    g_client_conn_slots[idx].service.find_service_result.end_hdl   = service->end_hdl;
    memcpy_s(&g_client_conn_slots[idx].service.find_service_result.uuid, sizeof(sle_uuid_t),
             &service->uuid, sizeof(sle_uuid_t));
}

static void sle_client_find_property_cbk(uint8_t client_id, uint16_t conn_id,
                                         ssapc_find_property_result_t *property,
                                         errcode_t status)
{
    if (property == NULL) {
        return;
    }

    log_debug("%s[%s] "
        "client_id[0x%02X] conn_id[0x%02X] status[0x%02X] "
        "operate_indication[0x%02X] descriptors_count[0x%02X] handle[0x%02X]\r\n",
        LOG_TAG, __func__, client_id, conn_id, status,
        property->operate_indication, property->descriptors_count, property->handle);

    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return;
    }
    g_client_conn_slots[idx].service.property_handle = property->handle;
    g_client_conn_slots[idx].service.property_ready = 1;
    g_client_conn_slots[idx].service.ntf_subscribed = 0;
    g_client_conn_slots[idx].service.ccc_desc_pending = 0;
    g_client_conn_slots[idx].service.ccc_value_fallback_tried = 0;
    g_client_conn_slots[idx].service.ccc_prime_pending = 0;
    g_client_conn_slots[idx].service.ccc_value_pending = 0;
    g_client_conn_slots[idx].service.ccc_verify_pending = 0;
    g_client_conn_slots[idx].service.ccc_verify_retries = 0;
    g_sle_send_param.handle = property->handle;
    g_sle_send_param.type   = SSAP_PROPERTY_TYPE_VALUE;
    log_info("%s[%s] conn_id[0x%02X] property_handle[0x%04X] writeable\r\n",
           LOG_TAG, __func__, conn_id, property->handle);

    if (property->descriptors_count > 0 && property->handle < 0xFFFF) {
        uint16_t ext_end = (uint16_t)(property->handle + (uint16_t)property->descriptors_count);
        if (ext_end > g_client_conn_slots[idx].service.find_service_result.end_hdl) {
            log_info("%s[%s] extend end_hdl 0x%x -> 0x%x (val+desc_cnt)\r\n",
                   LOG_TAG, __func__, (unsigned)g_client_conn_slots[idx].service.find_service_result.end_hdl, (unsigned)ext_end);
            g_client_conn_slots[idx].service.find_service_result.end_hdl = ext_end;
        }
        g_client_conn_slots[idx].service.ccc_handle = (uint16_t)(property->handle + 1U);
    } else {
        g_client_conn_slots[idx].service.ccc_handle = (uint16_t)(property->handle + 1U);
    }
}

static void sle_client_find_structure_cmp_cbk(uint8_t client_id, uint16_t conn_id,
                                              ssapc_find_structure_result_t *structure_result,
                                              errcode_t status)
{
    log_debug("%s[%s] "
        "client_id[0x%02X] conn_id[0x%02X] status[0x%02X] "
        "type[0x%02X] uuid.len[0x%02X]\r\n",
        LOG_TAG, __func__, client_id, conn_id, status,
        structure_result != NULL ? structure_result->type : 0,
        structure_result != NULL ? structure_result->uuid.len : 0);

    const bool st_ok = (status == ERRCODE_SUCC || status == (errcode_t)ERRCODE_SLE_SUCCESS);
    const bool is_primary_cmp =
        (structure_result != NULL && structure_result->type == SSAP_FIND_TYPE_PRIMARY_SERVICE);
    const bool is_property_cmp =
        (structure_result != NULL && structure_result->type == SSAP_FIND_TYPE_PROPERTY);

    if (!st_ok) {
        int32_t idx = sle_client_conn_index_by_id(conn_id);
        if (idx >= 0 && g_client_conn_slots[idx].service.find_service_result.start_hdl != 0 &&
            g_client_conn_slots[idx].service.property_ready == 0) {
            osal_mdelay(80);
            sle_client_disc_start_property_find(client_id, conn_id);
        }
        return;
    }

    if (is_primary_cmp) {
        osal_mdelay(80);
        sle_client_disc_start_property_find(client_id, conn_id);
        return;
    }

    if (is_property_cmp) {
        int32_t idx = sle_client_conn_index_by_id(conn_id);
        if (idx >= 0 && g_client_conn_slots[idx].service.property_handle != 0) {
            log_info("%s[%s] PROPERTY complete -> subscribe notify\r\n", LOG_TAG, __func__);
            osal_mdelay(80);
            (void)sle_client_try_subscribe_notify(client_id, conn_id);
        }
    }
}

static void sle_client_write_cfm_cbk(uint8_t client_id, uint16_t conn_id,
                                     ssapc_write_result_t *write_result, errcode_t status)
{
#if 0
    log_debug("%s[%s] "
        "client_id[0x%02X] conn_id[0x%02X] handle[0x%04X] type[0x%02X] status[0x%02X]\r\n",
        LOG_TAG, __func__, client_id, conn_id, write_result != NULL ? write_result->handle : 0,
        write_result != NULL ? write_result->type : 0, status);
#endif

    if (write_result == NULL) {
        return;
    }

    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return;
    }

    const bool st_ok = (status == ERRCODE_SUCC || status == (errcode_t)ERRCODE_SLE_SUCCESS);
    uint16_t val_hdl = g_client_conn_slots[idx].service.property_handle;
    uint16_t ccc_hdl = g_client_conn_slots[idx].service.ccc_handle;

    if (g_client_conn_slots[idx].service.ccc_value_pending != 0) {
        g_client_conn_slots[idx].service.ccc_value_pending = 0;
        if (st_ok &&
            write_result->type == SSAP_PROPERTY_TYPE_VALUE &&
            write_result->handle == val_hdl) {
            g_client_conn_slots[idx].service.ntf_subscribed = 1;
            log_info("%s[%s] Notify Subscribed (VALUE char+CCC): conn_id[0x%02X] h[0x%04X]\r\n",
                   LOG_TAG, __func__, conn_id, write_result->handle);
        }
        return;
    }

    if (!st_ok) {
        if (g_client_conn_slots[idx].service.ccc_desc_pending != 0) {
            g_client_conn_slots[idx].service.ccc_desc_pending = 0;
            if (g_client_conn_slots[idx].service.ntf_subscribed == 0) {
                g_client_conn_slots[idx].service.ntf_subscribed = 1;
            }
            log_error("%s[%s] CCCD DESC cfm fail status[0x%02X] trust write_cmd\r\n",
                   LOG_TAG, __func__, (unsigned)status);
        }
        return;
    }

    if (ccc_hdl == 0U) {
        return;
    }

    if (write_result->type == SSAP_DESCRIPTOR_CLIENT_CONFIGURATION &&
        write_result->handle == ccc_hdl) {
        g_client_conn_slots[idx].service.ccc_desc_pending = 0;
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
        log_info("%s[%s] Notify Subscribed (DESC): conn_id[0x%02X] handle[0x%04X]\r\n",
               LOG_TAG, __func__, conn_id, write_result->handle);
        return;
    }

    if (write_result->type == SSAP_PROPERTY_TYPE_VALUE && write_result->handle == ccc_hdl) {
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
        log_info("%s[%s] Notify Subscribed (VALUE on CCCD h): conn_id[0x%02X] handle[0x%04X]\r\n",
               LOG_TAG, __func__, conn_id, write_result->handle);
        return;
    }

    if (st_ok && write_result->type == SSAP_PROPERTY_TYPE_VALUE &&
        write_result->handle == val_hdl &&
        g_client_conn_slots[idx].service.ntf_subscribed != 0) {
        osal_mdelay(200);
        (void)ssapc_read_req(client_id, conn_id, val_hdl, SSAP_PROPERTY_TYPE_VALUE);
    }
}

static void sle_client_read_cfm_cbk(uint8_t client_id, uint16_t conn_id,
                                    ssapc_handle_value_t *read_data, errcode_t status)
{
#if 0
    log_debug("%s[%s ] ---------------------------------------------------------\r\n", LOG_TAG, __func__);
    log_debug("%s[%s ] "
        "client_id[0x%02X] conn_id[0x%02X] status[0x%02X] handle[0x%04X] type[0x%02X] len[%u]\r\n",
        LOG_TAG, __func__, client_id, conn_id, status,
        read_data != NULL ? read_data->handle : 0,
        read_data != NULL ? read_data->type : 0,
        read_data != NULL ? read_data->data_len : 0);
#endif

    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx < 0) {
        return;
    }

    const bool st_ok = (status == ERRCODE_SUCC || status == (errcode_t)ERRCODE_SLE_SUCCESS);
    uint16_t ccc_hdl = g_client_conn_slots[idx].service.ccc_handle;
    uint16_t val_hdl = g_client_conn_slots[idx].service.property_handle;

    if (g_client_conn_slots[idx].service.ccc_verify_pending != 0) {
        if (st_ok && read_data != NULL &&
            sle_client_read_looks_like_cccd(read_data, ccc_hdl)) {
            log_info("%s[%s ] CCCD verify bytes: %02x %02x\r\n",
                   LOG_TAG, __func__, read_data->data[0], read_data->data[1]);
            g_client_conn_slots[idx].service.ccc_verify_pending = 0;
            g_client_conn_slots[idx].service.ccc_verify_retries = 0;
            if ((read_data->data[0] & 0x01u) != 0) {
                g_client_conn_slots[idx].service.ntf_subscribed = 1;
                log_info("%s[%s ] Notify Subscribed (read verify)\r\n", LOG_TAG, __func__);
            } else {
                log_info("%s[%s ] CCCD notify bit off -> VALUE char fallback\r\n", LOG_TAG, __func__);
                sle_client_ccc_value_fallback_write(client_id, conn_id, idx);
            }
            return;
        }

        if (read_data != NULL && read_data->data_len >= 2) {
            log_info("%s[%s ] CCCD verify suspicious h[0x%04X] %02x %02x\r\n",
                   LOG_TAG, __func__, read_data->handle, read_data->data[0], read_data->data[1]);
        }

        if (read_data != NULL && read_data->handle == 0 && read_data->data_len >= 2 &&
            read_data->data[0] == 0x0c && read_data->data[1] == 0x00 &&
            g_client_conn_slots[idx].service.ccc_verify_retries >= 1u) {
            g_client_conn_slots[idx].service.ccc_verify_pending = 0;
            g_client_conn_slots[idx].service.ccc_verify_retries = 0;
            log_info("%s[%s ] bogus CCCD read on v106 -> combo fallback\r\n", LOG_TAG, __func__);
            sle_client_ccc_value_fallback_write(client_id, conn_id, idx);
            return;
        }

        if (ccc_hdl != 0 && g_client_conn_slots[idx].service.ccc_verify_retries < 2u) {
            g_client_conn_slots[idx].service.ccc_verify_retries++;
            uint8_t rd_type = (g_client_conn_slots[idx].service.ccc_verify_retries == 1u) ?
                              (uint8_t)SSAP_PROPERTY_TYPE_VALUE :
                              (uint8_t)SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
            log_info("%s[%s ] CCCD verify retry read h[0x%04X] type[0x%02X]\r\n",
                   LOG_TAG, __func__, ccc_hdl, (unsigned)rd_type);
            (void)ssapc_read_req(client_id, conn_id, ccc_hdl, rd_type);
            return;
        }

        g_client_conn_slots[idx].service.ccc_verify_pending = 0;
        g_client_conn_slots[idx].service.ccc_verify_retries = 0;
        log_info("%s[%s ] CCCD verify inconclusive -> VALUE char fallback\r\n", LOG_TAG, __func__);
        sle_client_ccc_value_fallback_write(client_id, conn_id, idx);
        return;
    }

    if (g_client_conn_slots[idx].service.ccc_prime_pending != 0) {
        g_client_conn_slots[idx].service.ccc_prime_pending = 0;
        if (read_data != NULL && read_data->handle != 0U) {
            ccc_hdl = read_data->handle;
            g_client_conn_slots[idx].service.ccc_handle = ccc_hdl;
        } else if (val_hdl != 0U && val_hdl < 0xFFFF) {
            ccc_hdl = (uint16_t)(val_hdl + 1U);
            g_client_conn_slots[idx].service.ccc_handle = ccc_hdl;
        }
        log_info("%s[%s ] prime done -> write CCCD h[0x%04X]\r\n", LOG_TAG, __func__, ccc_hdl);

        osal_mdelay(40);
        (void)sle_client_ccc_issue_desc_write(client_id, conn_id, idx, ccc_hdl);
        return;
    }

    if ((read_data != NULL) && st_ok && (read_data->data != NULL) &&
        (read_data->data_len > 2U) && (val_hdl != 0U) &&
        (read_data->handle == val_hdl || read_data->handle == 0U)) {
        char tmp[128] = {0};
        uint16_t copy = read_data->data_len;
        if (copy >= sizeof(tmp)) {
            copy = (uint16_t)(sizeof(tmp) - 1U);
        }
        if (memcpy_s(tmp, sizeof(tmp), read_data->data, copy) == EOK) {
//          log_debug("%s[%s ] -> sle_client_recv_data::\r\n", LOG_TAG, __func__);
            sle_client_recv_data(conn_id, (uint8_t *)tmp, copy);
            return;
        }
    }
}

errcode_t sle_client_broadcast_msg(const uint8_t *data, uint16_t len)
{
    errcode_t ret = ERRCODE_FAIL;
    ssapc_write_param_t param = {0};

    if ((data == NULL) || (len == 0)) {
        log_error("%s[%s] invalid args\r\n", LOG_TAG, __func__);
        return 0;
    }

    for (uint32_t i = 0; i < sle_client_get_max_slots(); i++) {
        if (g_client_conn_slots[i].state != SLE_CLIENT_SLOT_CONNECTED) {
            continue;
        }
        if (g_client_conn_slots[i].service.property_ready == 0 ||
            g_client_conn_slots[i].service.property_handle == 0) {
            log_info("%s[%s] conn_id[0x%02X] Not ready, skip\r\n", LOG_TAG, __func__,
                   g_client_conn_slots[i].service.conn_id);
            continue;
        }

        param.handle   = g_client_conn_slots[i].service.property_handle;
        param.type     = SSAP_PROPERTY_TYPE_VALUE;
        param.data_len = len + 1;

        param.data = osal_vmalloc(param.data_len);
        if (param.data == NULL) {
            log_error("%s[%s] osal_vmalloc fail\r\n", LOG_TAG, __func__);
            continue;
        }
        if (memcpy_s(param.data, param.data_len, data, len) != EOK) {
            osal_vfree(param.data);
            continue;
        }
        param.data[len] = '\0';

        ret = ssapc_write_req(g_client_id, g_client_conn_slots[i].service.conn_id, &param);

    #if 0
        log_info("%s[%s] ssapc_write_req conn_id[0x%02X] handle[0x%04X]  ret[0x%02X]:%s\r\n",
            LOG_TAG, __func__, g_client_conn_slots[i].service.conn_id, param.handle, ret,
            (ret == ERRCODE_SUCC || ret == (errcode_t)ERRCODE_SLE_SUCCESS)?"OK":"NG");
    #endif
        osal_vfree(param.data);
    }

    return ret;
}

__attribute__((weak)) int sle_client_send_data(uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0)) {
        log_error("%s[%s][weak] invalid args\r\n", LOG_TAG, __func__);
        return 0;
    }

    log_info("%s[%s][weak] [%d]:%s\r\n", LOG_TAG, __func__, len, (char*)data);
    return sle_client_broadcast_msg(data, len);
}

__attribute__((weak)) int sle_client_recv_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0)) {
        log_error("%s[%s][weak] invalid args\r\n", LOG_TAG, __func__);
        return 0;
    }

    log_info("%s[%s][weak] conn_id[0x%02X] [%d]:%s\r\n", LOG_TAG, __func__, conn_id, len, (char*)data);
    return len;
}

void ssapc_notification_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx >= 0) {
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
    }
    if (data == NULL || data->data == NULL || data->data_len == 0) {
        return;
    }
    data->data[data->data_len - 1] = '\0';

#if 0
    log_info("%s[%s] RecvFromServer: conn_id[0x%02X] status[0x%02X] len[%u]{{",
        LOG_TAG, __func__, conn_id, (unsigned)status, (unsigned)data->data_len);
    for (uint16_t i=0; i<data->data_len; i++) {
        printf("0x%02X ", data->data[i]);
    }
    printf("}}\r\n");
#endif
    sle_client_recv_data(conn_id, data->data, data->data_len);
}

void ssapc_indication_cbk(uint8_t client_id, uint16_t conn_id, ssapc_handle_value_t *data, errcode_t status)
{
    (void)client_id;
    int32_t idx = sle_client_conn_index_by_id(conn_id);
    if (idx >= 0) {
        g_client_conn_slots[idx].service.ntf_subscribed = 1;
    }

    if (data == NULL || data->data == NULL || data->data_len == 0) {
        return;
    }
    data->data[data->data_len - 1] = '\0';

    log_info("%s[%s] conn_id[0x%02X] status[0x%02X]\r\n",
        LOG_TAG, __func__, conn_id, (unsigned)status);
    log_info("%s[%s] RecvFromServer: [%u]:%s\r\n",
            LOG_TAG, __func__, (unsigned)data->data_len, (char *)data->data);
    sle_client_recv_data(conn_id, data->data, data->data_len);
}

static void sle_client_ssapc_cbk_register(void)
{
    g_sle_ssapc_cbk.exchange_info_cb        = sle_client_exchange_info_cbk;
    g_sle_ssapc_cbk.find_structure_cb       = sle_client_find_structure_cbk;
    g_sle_ssapc_cbk.ssapc_find_property_cbk = sle_client_find_property_cbk;
    g_sle_ssapc_cbk.find_structure_cmp_cb   = sle_client_find_structure_cmp_cbk;
    g_sle_ssapc_cbk.write_cfm_cb            = sle_client_write_cfm_cbk;
    g_sle_ssapc_cbk.read_cfm_cb             = sle_client_read_cfm_cbk;
    g_sle_ssapc_cbk.notification_cb         = ssapc_notification_cbk;
    g_sle_ssapc_cbk.indication_cb           = ssapc_indication_cbk;

    ssapc_register_callbacks(&g_sle_ssapc_cbk);
}

void sle_client_get_server_address(uint16_t conn_id, uint8_t* addr, uint8_t len)
{
    if ((addr == NULL)||(len == 0)) {
        log_error("%s[%s] invalid args\r\n", LOG_TAG, __func__);
        return;
    }
    int32_t slot = sle_client_slot_find_by_conn_id(conn_id);
    if (slot < 0) {
        log_error("%s[%s] invalid conn_id\r\n", LOG_TAG, __func__);
        return;
    }

    (void)memcpy_s(addr, len, &g_client_conn_slots[slot].server_addr.addr, SLE_ADDR_LEN);
}

void sle_client_set_local_address(uint8_t* addr, uint8_t len)
{
#if USE_OHOS_API
    SleAddr local_address;
#else
    sle_addr_t local_address;
#endif

    local_address.type = SLE_ADDRESS_TYPE_PUBLIC;

    if ((addr == NULL) || (len < SLE_ADDR_LEN)) {
//      random_mac_addr(local_address.addr);
        /* 使用芯片唯一的DieID[6...B]作为Mac[0...5]地址 */
        uint8_t die_id[16] = {0};
        if (uapi_efuse_get_die_id(die_id, sizeof(die_id)) == ERRCODE_SUCC) {
            for (uint8_t i = 0; i < SLE_ADDR_LEN; i++) {
                local_address.addr[i] = die_id[i+6];
            }
        }
    } else {
        (void)memcpy_s(local_address.addr, SLE_ADDR_LEN, addr, len);
    }

    log_info("%s[%s] %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           LOG_TAG, __func__,
           local_address.addr[0], local_address.addr[1], local_address.addr[2],
           local_address.addr[3], local_address.addr[4], local_address.addr[5]);

#if USE_OHOS_API
    SleSetLocalAddr(&local_address);
#else
    sle_set_local_addr(&local_address);
#endif
}

void sle_client_init(void)
{
    sle_uuid_client_register();
    sle_client_seek_cbk_register();
    sle_client_connect_cbk_register();
    sle_client_ssapc_cbk_register();

#if USE_OHOS_API
    EnableSle();
#else
    enable_sle();
#endif
}

void sle_client_deinit(void)
{
    ssapc_unregister_client(g_client_id);

#if USE_OHOS_API
    DisableSle();
#else
    disable_sle();
#endif
    log_info("%s[%s] Exit\r\n", LOG_TAG, __func__);
}
