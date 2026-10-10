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

/*
 * 键盘 BLE-HID 外设(Peripheral/Server)实现:
 *  - WS63 作为 BLE 外设持续广播(设备名 xiaohong-kb-ble, Appearance=键盘, 携带 HID 服务 UUID)
 *  - 注册标准 HID Service(0x1812): HID Information / Report Map / Report(输入+输出) / Control Point
 *  - PC(蓝牙Central)扫描到后连接并配对, 订阅输入 Report 的通知, 即可直接作为键盘使用
 *  - 按键上报: 应用层调用 ble_hid_send_input_report() 发送 8 字节 Boot Keyboard 报告,
 *    内部按经典"无Report ID"布局直接经 CCCD 通知发给主机(载荷=8字节, 无前缀)
 *  - 反向数据: 主机写 HID Output Report(键盘LED)与 HID Control Point(Suspend)时,
 *    通过弱回调 ble_hid_on_host_leds()/ble_hid_on_host_suspend() 交给应用层
 *
 * 说明: 该实现参考 SDK sample application/samples/bt/ble/ble_gatt_server 的
 *       GAP/GATT Server 注册流程与 hilink_ble_adapter 的读写应答方式。
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "errcode.h"
#include "common_def.h"
#include "securec.h"
#include "osal_addr.h"
#include "efuse.h"

#include "bts_def.h"
#include "bts_le_gap.h"
#include "bts_gatt_stru.h"
#include "bts_gatt_server.h"

#include "log.h"
#include "settings.h"
#include "ble_client.h"

#define LOG_TAG "[BleHidServer]"

/* 广播数据分段总长度上限(legacy adv 31 字节) */
#define BLE_ADV_DATA_MAX_LEN     (31)
/* 默认广播句柄(与SDK sample ble_gatt_server 的定义一致) */
#ifndef BTH_GAP_BLE_ADV_HANDLE_DEFAULT
#define BTH_GAP_BLE_ADV_HANDLE_DEFAULT  0x01
#endif

/* 键盘按键输入报告: 8字节(与SLE/USB一致), 通知载荷不带Report ID前缀 */
#define BLE_HID_NOTIFY_BUF_LEN   (BLE_HID_INPUT_REPORT_LEN)

/* ------------------------------------------------------------------*/
/* HID Report Map(USB HID Descriptor): 经典"无Report ID"键盘布局(与    */
/* USB Boot键盘一致, 是BLE键盘在Windows上兼容性最好的形态):              */
/*   输入报告: 8字节 = modifier + 保留 + 6键码(与SLE/USB通道完全相同)    */
/*   输出报告: 1字节 = NumLock/CapsLock/ScrollLock/Compose/Kana LED    */
/* 注意: LED输出段必须放在6键码数组之后(HOGP按Report协议解析, 无Report    */
/* ID时输入/输出方向切换会把输入报告切成多段, 导致通知长度与映射不符而被    */
/* 主机丢弃; 此前的"modifier+保留 → LED → 键码"次序正因此让键码永远到不了) */
/* 通知/读写载荷均不带Report ID前缀, Report Reference 里的 ID 填 0x00    */
/* -------------------------------------------------------------------*/
static const uint8_t g_hid_report_map[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop)            */
    0x09, 0x06,       /* Usage (Keyboard)                        */
    0xA1, 0x01,       /* Collection (Application)                */
    0x05, 0x07,       /*   Usage Page (Keyboard/Keypad)          */
    0x19, 0xE0,       /*   Usage Minimum (224)                   */
    0x29, 0xE7,       /*   Usage Maximum (231)                   */
    0x15, 0x00,       /*   Logical Minimum (0)                   */
    0x25, 0x01,       /*   Logical Maximum (1)                   */
    0x75, 0x01,       /*   Report Size (1)                       */
    0x95, 0x08,       /*   Report Count (8)                      */
    0x81, 0x02,       /*   Input (Data,Var,Abs): Modifier 字节   */
    0x95, 0x01,       /*   Report Count (1)                      */
    0x75, 0x08,       /*   Report Size (8)                       */
    0x81, 0x01,       /*   Input (Const): 保留字节                */
    0x95, 0x06,       /*   Report Count (6)                      */
    0x75, 0x08,       /*   Report Size (8)                       */
    0x15, 0x00,       /*   Logical Minimum (0)                   */
    0x25, 0xFF,       /*   Logical Maximum (255)                 */
    0x19, 0x00,       /*   Usage Minimum (0)                     */
    0x29, 0xFF,       /*   Usage Maximum (255)                   */
    0x81, 0x00,       /*   Input (Data,Array): 6 个键码字节       */
    0x15, 0x00,       /*   Logical Minimum (0)                   */
    0x25, 0x01,       /*   Logical Maximum (1)                   */
    0x05, 0x08,       /*   Usage Page (LEDs)                     */
    0x19, 0x01,       /*   Usage Minimum (1): NumLock            */
    0x29, 0x05,       /*   Usage Maximum (5): Kana               */
    0x75, 0x01,       /*   Report Size (1)                       */
    0x95, 0x05,       /*   Report Count (5)                      */
    0x91, 0x02,       /*   Output (Data,Var,Abs): LED 位         */
    0x95, 0x01,       /*   Report Count (1)                      */
    0x75, 0x03,       /*   Report Size (3)                       */
    0x91, 0x01,       /*   Output (Const): 补齐到 1 字节          */
    0xC0              /* End Collection                          */
};

/* HID Information 特征值: bcdHID=1.11(小端) + CountryCode=0 + Flags=0x02(NormallyConnectable) */
static const uint8_t g_hid_info_value[] = {
    0x0B, 0x01,       /* bcdHID 1.11 */
    0x00,             /* bCountryCode */
    BLE_HID_INFO_FLAG_NORM_CONN,
};

/* Report Reference 描述符值: [ReportID(无ID布局=0x00), ReportType] */
static const uint8_t g_report_ref_input[]  = { 0x00, BLE_HID_REPORT_TYPE_INPUT };
static const uint8_t g_report_ref_output[] = { 0x00, BLE_HID_REPORT_TYPE_OUTPUT };

/* 各特征/描述符的运行时属性句柄(在 add 回调里按添加顺序回填) */
typedef enum {
    HD_HID_INFO = 0,    /* HID Information     特征值   */
    HD_REPORT_MAP,      /* Report Map          特征值   */
    HD_INPUT_REPORT,    /* Report(输入)        特征值   */
    HD_INPUT_CCC,       /* Report(输入) CCCD   描述符   */
    HD_INPUT_REF,       /* Report(输入) Ref    描述符   */
    HD_OUTPUT_REPORT,   /* Report(输出)        特征值   */
    HD_OUTPUT_REF,      /* Report(输出) Ref    描述符   */
    HD_CTRL_POINT,      /* HID Control Point   特征值   */
    HD_MAX,
} hid_attr_t;

/* HID 特征值当前内容: 输入报告/输出报告随收发实时更新, 供主机直接读(均无Report ID) */
static uint8_t g_input_report[BLE_HID_INPUT_REPORT_LEN] = { 0 };       /* 8B, 全0 */
static uint8_t g_output_report[BLE_HID_OUTPUT_REPORT_LEN] = { 0x00 };  /* 1B, LED */
static uint8_t g_ctrl_point_value = 0x00;

static uint16_t g_attr_handle[HD_MAX] = { 0 };
static uint16_t g_service_handle = 0;   /* 当前HID服务句柄(删除/重建服务时使用) */
static bool     g_rebuild_pending = false; /* 重建回调链进行中(stop→delete→add) */

/* BLE/GAP 运行状态 */
static uint8_t  g_server_id  = 0xFF;         /* GATT server id */
static bool     g_ble_enabled = false;
static uint16_t g_conn_id     = 0xFFFF;      /* 当前主机连接 */
static bd_addr_t g_host_addr;                /* 当前主机地址 */
static bool     g_host_connected = false;
/* 输入Report的CCCD注册初值为0x0001(通知使能, 对齐SDK sample):
 * 主机读CCCD看到已使能时不会再写订阅, 故镜像默认置true;
 * 仅当主机显式写0x0000时由写回调关闭 */
static bool     g_input_ntf_on   = true;
static bool     g_ccc_written    = false; /* 本次连接是否已收到主机CCCD订阅写(栈登记依据) */
static uint8_t  g_host_leds      = 0x00;
static uint8_t  g_host_suspend   = 0xFF;

/* 诊断: notify 丢弃告警(仅在状态翻转时各打一次, 避免刷屏) */
static bool g_log_drop_unconn = false;   // 已打过"主机未连接"告警
static bool g_log_drop_unsub  = false;   // 已打过"主机未订阅CCCD"告警
static bool g_log_first_send  = false;   // 已打印过"首次notify成功(含句柄)"的诊断

/* ---------------- 弱回调默认实现(可由应用层覆盖) ---------------- */
__attribute__((weak)) void ble_hid_on_host_leds(uint8_t leds)
{
    (void)leds;
}
__attribute__((weak)) void ble_hid_on_host_suspend(uint8_t suspend)
{
    (void)suspend;
}

/* 16bit UUID -> bt_uuid_t(高位在前) */
static void ble_uuid16_to_bt_uuid(uint16_t uuid16, bt_uuid_t *out)
{
    out->uuid_len = 2;
    out->uuid[0] = (uint8_t)(uuid16 >> 8);
    out->uuid[1] = (uint8_t)(uuid16 & 0xFF);
}

/* ----------------------------- 广播 ----------------------------- */
static uint8_t ble_set_adv_data(void)
{
    uint8_t adv_data[BLE_ADV_DATA_MAX_LEN] = { 0 };
    uint8_t rsp_data[BLE_ADV_DATA_MAX_LEN] = { 0 };
    uint8_t idx = 0;
    const char *name = BLE_HID_DEV_NAME;
    size_t name_len = strlen(name);
    gap_ble_config_adv_data_t cfg_adv_data = { 0 };

    /* --- 广播包: FLAGS + Appearance(键盘) + 完整16bit服务UUID列表(0x1812 HID) --- */
    adv_data[idx++] = 0x02;                 /* length */
    adv_data[idx++] = 0x01;                 /* type: Flags */
    adv_data[idx++] = 0x06;                 /* LE General Discoverable | BR/EDR Not Supported */
    adv_data[idx++] = 0x03;                 /* length */
    adv_data[idx++] = 0x19;                 /* type: Appearance */
    adv_data[idx++] = (uint8_t)(BLE_ADV_APPEARANCE_KEYBOARD & 0xFF);
    adv_data[idx++] = (uint8_t)((BLE_ADV_APPEARANCE_KEYBOARD >> 8) & 0xFF);
    adv_data[idx++] = 0x03;                 /* length */
    adv_data[idx++] = 0x03;                 /* type: Complete List of 16-bit Service UUIDs */
    adv_data[idx++] = (uint8_t)(BLE_UUID_SERVICE_HID & 0xFF);
    adv_data[idx++] = (uint8_t)((BLE_UUID_SERVICE_HID >> 8) & 0xFF);

    /* --- 扫描应答: 设备名 --- */
    if (name_len > (BLE_ADV_DATA_MAX_LEN - 2)) {
        name_len = BLE_ADV_DATA_MAX_LEN - 2;   /* 名称超长截断, 避免越界 */
    }
    rsp_data[0] = (uint8_t)(name_len + 1);     /* length(不含自身) */
    rsp_data[1] = 0x09;                        /* type: Complete Local Name */
    if (memcpy_s(&rsp_data[2], sizeof(rsp_data) - 2, name, name_len) != EOK) {
        return ERRCODE_BT_MEMCPY_FAIL;
    }

    cfg_adv_data.adv_length = idx;
    cfg_adv_data.adv_data = adv_data;
    cfg_adv_data.scan_rsp_length = (uint16_t)(name_len + 2);
    cfg_adv_data.scan_rsp_data = rsp_data;

    return gap_ble_set_adv_data(BTH_GAP_BLE_ADV_HANDLE_DEFAULT, &cfg_adv_data);
}

static errcode_t ble_set_adv_param(void)
{
    gap_ble_adv_params_t adv_para = { 0 };

    adv_para.min_interval = 0x30;              /* 30ms */
    adv_para.max_interval = 0x60;              /* 60ms */
    adv_para.adv_type = GAP_BLE_ADV_CONN_SCAN_UNDIR;   /* 可连接可扫描非定向广播 */
    adv_para.adv_filter_policy = GAP_BLE_ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    adv_para.channel_map = 0x07;               /* 37/38/39 全部信道 */
    adv_para.duration = 0;                     /* 永久广播 */
    /* own_addr/peer_addr 置0: 使用协议栈默认(公共地址), 不指定对端 */

    return gap_ble_set_adv_param(BTH_GAP_BLE_ADV_HANDLE_DEFAULT, &adv_para);
}

errcode_t ble_hid_restart_adv(void)
{
    errcode_t ret = ERRCODE_BT_SUCCESS;

    if (!g_ble_enabled) {
        return ERRCODE_BT_NOT_READY;
    }
    (void)gap_ble_stop_adv(BTH_GAP_BLE_ADV_HANDLE_DEFAULT);
    ret = ble_set_adv_data();
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: set adv data fail[0x%x]\r\n", LOG_TAG, __func__, ret);
        return ret;
    }
    ret = ble_set_adv_param();
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: set adv param fail[0x%x]\r\n", LOG_TAG, __func__, ret);
        return ret;
    }
    ret = gap_ble_start_adv(BTH_GAP_BLE_ADV_HANDLE_DEFAULT);
    log_info("%s %s: start adv ret[0x%x]\r\n", LOG_TAG, __func__, ret);
    return ret;
}

/* ------------------- GAP 回调(ble/广播/连接状态) ------------------- */
/* 设置安全(配对)参数: 支持绑定, Just-Works(无输入输出), 便于 Windows/手机直接配对。
 * 参考 hilink 适配层的做法: 主机连接(即将发起SMP)前也调用一次, 确保协议栈按需应答配对 */
static void ble_hid_set_sec_params(void)
{
    gap_ble_sec_params_t sec = { 0 };
    sec.bondable = 1;
    sec.io_capability = GAP_BLE_IO_CAPABILITY_NOINPUTNOOUTPUT;
    sec.sc_enable = 0;
    sec.sc_mode = GAP_BLE_GAP_SECURITY_MODE1_LEVEL2;
    (void)gap_ble_set_sec_param(&sec);
}

static void ble_hid_ble_enable_cbk(errcode_t status)
{
    log_info("%s %s: status[0x%x]\r\n", LOG_TAG, __func__, status);
    if (status != ERRCODE_BT_SUCCESS) {
        g_ble_enabled = false;
        return;
    }
    g_ble_enabled = true;

    ble_hid_set_sec_params();

    /* 设备名: Windows连接后会读GAP服务的Device Name特征并显示该名字;
       与广播名一致, 便于识别 */
    log_info("%s %s: set local name[%s]\r\n", LOG_TAG, __func__, BLE_HID_DEV_NAME);
    (void)gap_ble_set_local_name((const uint8_t *)BLE_HID_DEV_NAME,
                                 (uint8_t)strlen(BLE_HID_DEV_NAME));

    /* 地址: 优先使用"换地址兜底"持久化的覆盖地址(重启后沿用, 避免回到旧地址
       触发Windows"缓存不订阅"复发); 无覆盖时用芯片唯一DieID[6...B]作为Mac[0...5] */
    bd_addr_t local_addr = {0};
    uint8_t die_id[16] = {0};
    if (get_ble_addr_override(local_addr.addr, BD_ADDR_LEN) != 0) {
        log_info("%s %s: use BLE addr override\r\n", LOG_TAG, __func__);
    } else if (uapi_efuse_get_die_id(die_id, sizeof(die_id)) == ERRCODE_SUCC) {
        for (uint8_t i = 0; i < BD_ADDR_LEN; i++) {
            local_addr.addr[i] = die_id[i+6];
        }
    }
    (void)gap_ble_set_local_addr(&local_addr);

    if (gap_ble_get_local_addr(&local_addr) == ERRCODE_BT_SUCCESS) {
        log_info("%s %s: local addr[%02X:%02X:%02X:%02X:%02X:%02X]\r\n",
            LOG_TAG, __func__,
            local_addr.addr[0], local_addr.addr[1], local_addr.addr[2],
            local_addr.addr[3], local_addr.addr[4], local_addr.addr[5]);
    }

    ble_hid_restart_adv();
}

static void ble_hid_conn_state_cbk(uint16_t conn_id, bd_addr_t *addr,
                                    gap_ble_conn_state_t conn_state,
                                    gap_ble_pair_state_t pair_state,
                                    gap_ble_disc_reason_t disc_reason)
{
    if (addr == NULL) {
        return;
    }
    if (conn_state == GAP_BLE_STATE_CONNECTED) {
        g_conn_id = conn_id;
        g_host_connected = true;
        g_input_ntf_on = true;   /* 重连兜底: 键盘重启后CCCD初值回到0x0000, Windows自动
                                    重连时用缓存不再写订阅(写回调不触发), 应用层若拦截则
                                    按键全丢; 恢复"默认放行", 由栈按自身登记状态决定是否发出 */
        g_ccc_written = false;   /* 新连接: 等待主机写CCCD订阅(栈内部登记的依据) */
        g_log_drop_unconn = false;   // 新连接, 复位丢弃告警标志
        g_log_drop_unsub  = false;
        g_log_first_send  = false;

        (void)memcpy_s(&g_host_addr, sizeof(bd_addr_t), addr, sizeof(bd_addr_t));

        log_info("%s %s: CONNECTED conn[%d] addr[%02X:%02X:%02X:%02X:%02X:%02X] pair[%d]\r\n",
            LOG_TAG, __func__, conn_id,
            addr->addr[0], addr->addr[1], addr->addr[2],
            addr->addr[3], addr->addr[4], addr->addr[5], pair_state);
    } else {
        log_info("%s %s: DISCONNECTED conn[%d] reason[0x%x]\r\n", LOG_TAG, __func__, conn_id, disc_reason);
        if (g_host_connected && (g_conn_id == conn_id)) {
            g_host_connected = false;
            g_input_ntf_on = false;
            g_conn_id = 0xFFFF;
            memset_s(&g_host_addr, sizeof(g_host_addr), 0, sizeof(g_host_addr));
        }
        /* 断开后保持/恢复广播, 等待 PC 重新连接 */
        ble_hid_restart_adv();
    }
}

/* 诊断: 配对完成/加密完成回调(仅日志) —— 重连场景判断链路是否完成加密 */
static void ble_hid_pair_complete_cbk(uint16_t conn_id, const bd_addr_t *addr, errcode_t status)
{
    (void)conn_id;
    (void)addr;
    log_debug("%s %s: pair complete conn[%u] status[0x%x]\r\n", LOG_TAG, __func__, conn_id, status);
}

static void ble_hid_auth_complete_cbk(uint16_t conn_id, const bd_addr_t *addr, errcode_t status,
    const ble_auth_info_evt_t *evt)
{
    (void)conn_id;
    (void)addr;
    (void)evt;
    log_debug("%s %s: auth/encrypt done conn[%u] status[0x%x]\r\n", LOG_TAG, __func__, conn_id, status);
}

static void ble_hid_register_gap_callbacks(void)
{
    gap_ble_callbacks_t gap_cb = { 0 };

    gap_cb.ble_enable_cb = ble_hid_ble_enable_cbk;
    gap_cb.conn_state_change_cb = ble_hid_conn_state_cbk;
    gap_cb.pair_result_cb = ble_hid_pair_complete_cbk;       /* 诊断: 配对结果 */
    gap_cb.auth_complete_cb = ble_hid_auth_complete_cbk;     /* 诊断: 加密完成 */

    if (gap_ble_register_callbacks(&gap_cb) != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: register gap callbacks fail\r\n", LOG_TAG, __func__);
    }
}

/* --------------------- GATT 服务注册(回调驱动) --------------------- */
/* 输入 Report 的 CCCD(客户端特性配置)当前值: 初始 0x0000(规范值)。
 * 策略说明(基于实测结论): 该协议栈的通知使能由"客户端写CCCD"驱动登记,
 * 若初值设0x0001, 主机读到后不再写订阅, 栈内登记反而不会被触发(表现为
 * notify返回成功但空口无包)。故初值保持0x0000, 由Windows/Android在配对后
 * 写0x0001完成登记; 应用侧镜像 g_input_ntf_on 仍默认true做兜底。 */
static uint8_t g_attr_ccc_value[2] = { 0x00, 0x00 };

/* 建服务分步状态机: 每个特征/描述符添加回调返回后, 发起下一步,
 * 保证属性句柄按序回填, 且每个特征紧跟其专属描述符, 全部就绪后启动服务。
 * 步骤: 0 HIDInfo 1 ReportMap 2 输入Report 3 输入ReportRef 4 CCCD(放在特征末位!)
 *       5 输出Report 6 输出ReportRef 7 ControlPoint(=8步)
 * 注: CCCD 必须为该通知特征添加的"最后一个"描述符 —— 探针(仅CCCD, 工作正常)与
 *     HID(CCCD后还夹ReportRef, 通知被栈静默丢弃)的唯一结构差异即在此;
 *     主机按UUID(0x2902)匹配CCCD, 与描述符次序无关, 故可安全调序 */
#define BLE_HID_BUILD_TOTAL_STEPS  8
static uint8_t g_build_step = 0;
static uint8_t g_build_attr = HD_MAX;   /* 当前这一步对应的属性索引 */

static void ble_hid_build_next(uint8_t server_id, uint16_t srvc_handle)
{
    bt_uuid_t uuid_16 = { 0 };
    gatts_add_chara_info_t ch = { 0 };
    gatts_add_desc_info_t  desc = { 0 };

    switch (g_build_step) {
        case 0:   /* HID Information 特征: 只读 */
            ble_uuid16_to_bt_uuid(BLE_UUID_CHAR_HID_INFO, &uuid_16);
            ch.chara_uuid = uuid_16;
            ch.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
            ch.properties = GATT_CHARACTER_PROPERTY_BIT_READ;
            ch.value_len = sizeof(g_hid_info_value);
            ch.value = (uint8_t *)g_hid_info_value;
            g_build_attr = HD_HID_INFO;
            (void)gatts_add_characteristic(server_id, srvc_handle, &ch);
            break;
        case 1:   /* Report Map 特征: 只读(HID描述符) */
            ble_uuid16_to_bt_uuid(BLE_UUID_CHAR_HID_REPORT_MAP, &uuid_16);
            ch.chara_uuid = uuid_16;
            ch.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
            ch.properties = GATT_CHARACTER_PROPERTY_BIT_READ;
            ch.value_len = sizeof(g_hid_report_map);
            ch.value = (uint8_t *)g_hid_report_map;
            g_build_attr = HD_REPORT_MAP;
            (void)gatts_add_characteristic(server_id, srvc_handle, &ch);
            break;
        case 2:   /* 输入 Report 特征: 读 + 通知(按键上报通道) */
            ble_uuid16_to_bt_uuid(BLE_UUID_CHAR_HID_REPORT, &uuid_16);
            ch.chara_uuid = uuid_16;
            ch.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
            ch.properties = GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_NOTIFY;
            ch.value_len = sizeof(g_input_report);
            ch.value = g_input_report;
            g_build_attr = HD_INPUT_REPORT;
            (void)gatts_add_characteristic(server_id, srvc_handle, &ch);
            break;
        case 3:   /* 输入 Report 的 Report Reference(放CCCD之前) */
            ble_uuid16_to_bt_uuid(BLE_UUID_DESC_REPORT_REF, &uuid_16);
            desc.desc_uuid = uuid_16;
            desc.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
            desc.value_len = sizeof(g_report_ref_input);
            desc.value = (uint8_t *)g_report_ref_input;
            g_build_attr = HD_INPUT_REF;
            (void)gatts_add_descriptor(server_id, srvc_handle, &desc);
            break;
        case 4:   /* 输入 Report 的 CCCD —— 必须是该特征的末位描述符(通知使能登记依赖此点) */
            ble_uuid16_to_bt_uuid(BLE_UUID_DESC_CCCD, &uuid_16);
            desc.desc_uuid = uuid_16;
            desc.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
            desc.value_len = sizeof(g_attr_ccc_value);
            desc.value = g_attr_ccc_value;
            g_build_attr = HD_INPUT_CCC;
            (void)gatts_add_descriptor(server_id, srvc_handle, &desc);
            break;
        case 5:   /* 输出 Report 特征: 读 + 写(主机下发键盘LED) */
            ble_uuid16_to_bt_uuid(BLE_UUID_CHAR_HID_REPORT, &uuid_16);
            ch.chara_uuid = uuid_16;
            ch.permissions = GATT_ATTRIBUTE_PERMISSION_READ | GATT_ATTRIBUTE_PERMISSION_WRITE;
            ch.properties = GATT_CHARACTER_PROPERTY_BIT_READ | GATT_CHARACTER_PROPERTY_BIT_WRITE;
            ch.value_len = sizeof(g_output_report);
            ch.value = g_output_report;
            g_build_attr = HD_OUTPUT_REPORT;
            (void)gatts_add_characteristic(server_id, srvc_handle, &ch);
            break;
        case 6:   /* 输出 Report 的 Report Reference */
            ble_uuid16_to_bt_uuid(BLE_UUID_DESC_REPORT_REF, &uuid_16);
            desc.desc_uuid = uuid_16;
            desc.permissions = GATT_ATTRIBUTE_PERMISSION_READ;
            desc.value_len = sizeof(g_report_ref_output);
            desc.value = (uint8_t *)g_report_ref_output;
            g_build_attr = HD_OUTPUT_REF;
            (void)gatts_add_descriptor(server_id, srvc_handle, &desc);
            break;
        case 7:   /* HID Control Point 特征: 仅写(Suspend/Exit Suspend) */
            ble_uuid16_to_bt_uuid(BLE_UUID_CHAR_HID_CONTROL_POINT, &uuid_16);
            ch.chara_uuid = uuid_16;
            ch.permissions = GATT_ATTRIBUTE_PERMISSION_WRITE;
            ch.properties = GATT_CHARACTER_PROPERTY_BIT_WRITE;
            ch.value_len = sizeof(g_ctrl_point_value);
            ch.value = &g_ctrl_point_value;
            g_build_attr = HD_CTRL_POINT;
            (void)gatts_add_characteristic(server_id, srvc_handle, &ch);
            break;
        default:
            break;
    }
    if (g_build_step < BLE_HID_BUILD_TOTAL_STEPS) {
        g_build_step++;
    }
}

/* 服务是否已启动(只在最后一个特征落库后启动一次) */
static bool g_service_started = false;

/* 尝试启动HID服务: 仅当 Control Point(最后一个特征)的添加回调触发时才启动,
 * 避免"还有特征在添加队列中未落库"时就提前/重复启动服务(此前存在该竞态) */
static void ble_hid_try_start_service(uint8_t server_id, uint16_t service_handle)
{
    if (g_service_started || (g_build_attr != HD_CTRL_POINT)) {
        return;
    }
    g_service_started = true;
    log_info("%s %s: HID service build done, start service\r\n", LOG_TAG, __func__);
    (void)gatts_start_service(server_id, service_handle);
}

static void ble_hid_service_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t handle, errcode_t status)
{
    (void)uuid;
    log_info("%s %s: status[0x%x] handle[%d]\r\n", LOG_TAG, __func__, status, handle);
    if (status != ERRCODE_BT_SUCCESS) {
        return;
    }
    g_build_step = 0;
    g_build_attr = HD_MAX;
    g_service_started = false;
    g_service_handle = handle;   /* 记录服务句柄, 供删除/重建服务使用 */
    ble_hid_build_next(server_id, handle);   /* 步骤0: 添加 HID Information */
}

/* 特征添加回调: 回填句柄后发起下一步 */
static void ble_hid_chara_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t service_handle,
    gatts_add_character_result_t *result, errcode_t status)
{
    (void)uuid;
    if ((status != ERRCODE_BT_SUCCESS) || (result == NULL)) {
        log_error("%s %s: chara add fail status[0x%x]\r\n", LOG_TAG, __func__, status);
        return;
    }
    if (g_build_attr < HD_MAX) {
        g_attr_handle[g_build_attr] = result->value_handle;
        log_debug("%s %s: chara[%d] value_hdl[0x%x]\r\n", LOG_TAG, __func__, g_build_attr, result->value_handle);
    }
    /* 服务启动: 仅在最后一个特征(Control Point)的添加完成回调里执行一次。
       必须先于 build_next 判断 —— build_next 会把 g_build_attr 推进到下一步 */
    ble_hid_try_start_service(server_id, service_handle);
    ble_hid_build_next(server_id, service_handle);
}

/* 描述符添加回调: 回填句柄后发起下一步 */
static void ble_hid_desc_add_cbk(uint8_t server_id, bt_uuid_t *uuid, uint16_t service_handle,
    uint16_t handle, errcode_t status)
{
    (void)uuid;
    if (status != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: desc add fail status[0x%x]\r\n", LOG_TAG, __func__, status);
        return;
    }
    if (g_build_attr < HD_MAX) {
        g_attr_handle[g_build_attr] = handle;
        log_debug("%s %s: desc[%d] hdl[0x%x]\r\n", LOG_TAG, __func__, g_build_attr, handle);
    }
    /* 服务启动: 仅在最后一个特征(Control Point)的添加完成回调里执行一次。
       必须先于 build_next 判断 —— build_next 会把 g_build_attr 推进到下一步 */
    ble_hid_try_start_service(server_id, service_handle);
    ble_hid_build_next(server_id, service_handle);
}

/* 主机读请求: 仅记录日志。
 * 该协议栈对读请求由栈自动应答(按添加特征时登记的初值返回, 自动处理长读切片),
 * 应用层不应调用 gatts_send_response —— SDK sample(ble_gatt_server/ble_speed_server)
 * 的读写回调都只打日志, 由栈统一应答与记账 */
static void ble_hid_read_req_cbk(uint8_t server_id, uint16_t conn_id,
    gatts_req_read_cb_t *read_cb_para, errcode_t status)
{
    (void)server_id;
    (void)status;
    if (read_cb_para == NULL) {
        return;
    }
    /* 诊断: 记录主机发来的所有读请求(可确认Windows是否读完Report Map/各描述符) */
    log_debug("%s %s: READ hdl[0x%x] offset[%u] is_long[%d] need_rsp[%d]\r\n",
        LOG_TAG, __func__, read_cb_para->handle, read_cb_para->offset,
        read_cb_para->is_long, read_cb_para->need_rsp);
}

/* CCCD 写: 更新输入 Report 通知订阅状态 */
static void ble_hid_handle_ccc_write(const gatts_req_write_cb_t *req)
{
    uint16_t ccc_val = 0;

    if ((req->value == NULL) || (req->length < 1)) {
        return;
    }
    ccc_val = req->value[0];
    if (req->length > 1) {
        ccc_val |= (uint16_t)(req->value[1] << 8);
    }
    g_input_ntf_on = (ccc_val & 0x0001) ? true : false;
    g_ccc_written = true;   /* 已收到订阅写: 本次连接的栈通知登记完成 */
    log_debug("%s %s: ccc[0x%04X] ntf_on[%d]\r\n", LOG_TAG, __func__, ccc_val, g_input_ntf_on);
}

/* HID Output Report 写(主机下发键盘LED等) */
static void ble_hid_handle_output_write(const gatts_req_write_cb_t *req)
{
    uint8_t leds = 0;

    if ((req->value == NULL) || (req->length < 1)) {
        return;
    }
    if ((req->length >= 2) && (req->value[0] == 0x02)) {
        /* 兼容早期带Report ID(0x02)固件形态: [0x02][LED] */
        leds = req->value[1];
    } else {
        /* 无Report ID布局: 主机写1字节LED(取末尾字节, 容错) */
        leds = req->value[req->length - 1];
    }
    g_host_leds = leds;
    g_output_report[0] = leds;
    log_debug("%s %s: host leds[0x%02X]\r\n", LOG_TAG, __func__, leds);
    ble_hid_on_host_leds(leds);
}

/* HID Control Point 写: 0x00=Suspend 0x01=Exit Suspend */
static void ble_hid_handle_ctrl_pt_write(const gatts_req_write_cb_t *req)
{
    if ((req->value == NULL) || (req->length < 1)) {
        return;
    }
    g_host_suspend = req->value[0];
    g_ctrl_point_value = req->value[0];
    log_debug("%s %s: suspend[0x%02X]\r\n", LOG_TAG, __func__, g_host_suspend);
    ble_hid_on_host_suspend(g_host_suspend);
}

/* 主机写请求: 仅更新本地状态/镜像, 不应调用 gatts_send_response。
 * 该协议栈对写请求(含CCCD订阅写)由栈自动应答并更新内部状态 —— 应用层手动应答会
 * 干扰栈对CCCD订阅的内部登记(此前正是手动应答导致"notify返回成功但实际未发出"),
 * SDK sample(ble_speed_server等)的写回调同样只打日志 */
static void ble_hid_write_req_cbk(uint8_t server_id, uint16_t conn_id,
    gatts_req_write_cb_t *write_cb_para, errcode_t status)
{
    (void)server_id;
    (void)conn_id;
    (void)status;

    if (write_cb_para == NULL) {
        return;
    }
    /* 诊断: 记录主机发来的所有写请求(CCCD订阅/Output Report/Control Point都走这里) */
    log_debug("%s %s: WRITE hdl[0x%x] len[%u] need_rsp[%d] is_prep[%d]\r\n",
        LOG_TAG, __func__, write_cb_para->handle, write_cb_para->length,
        write_cb_para->need_rsp, write_cb_para->is_prep);

    if (write_cb_para->handle == g_attr_handle[HD_INPUT_CCC]) {
        ble_hid_handle_ccc_write(write_cb_para);
    } else if (write_cb_para->handle == g_attr_handle[HD_OUTPUT_REPORT]) {
        ble_hid_handle_output_write(write_cb_para);
    } else if (write_cb_para->handle == g_attr_handle[HD_CTRL_POINT]) {
        ble_hid_handle_ctrl_pt_write(write_cb_para);
    } else {
        /* 未知句柄: 通常是协议栈自动注册的 GATT/GAP 区属性(如 Service Changed 的 CCCD),
           主机(Windows)会写 0x0001 订阅服务变化指示; 由协议栈自动应答, 这里仅记录 */
        log_debug("%s %s: write to unknown hdl[0x%x] len[%u]: handled by stack\r\n",
            LOG_TAG, __func__, write_cb_para->handle, write_cb_para->length);
    }
}

/* 重建回调链(stop→delete→add 均为异步, 结果在各回调中返回):
 * 已启动的服务不能直接 delete(实测 gatts_delete_service 返回 0x80006007),
 * 必须先 gatts_stop_service, 待 stop 回调成功后再 delete, 再 re-add */
static errcode_t ble_hid_add_hid_service(void);   /* 定义于本文件下方 */

static void ble_hid_stop_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    if (!g_rebuild_pending) {
        return;
    }
    if (status != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: stop service fail[0x%x], abort rebuild\r\n", LOG_TAG, __func__, status);
        g_rebuild_pending = false;
        return;
    }
    log_debug("%s %s: stop done hdl[0x%x], delete service\r\n", LOG_TAG, __func__, handle);
    (void)gatts_delete_service(server_id, handle);
}

static void ble_hid_delete_service_cbk(uint8_t server_id, errcode_t status)
{
    if (!g_rebuild_pending) {
        return;
    }
    g_rebuild_pending = false;
    if (status != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: delete service fail[0x%x], abort rebuild\r\n", LOG_TAG, __func__, status);
        return;
    }
    log_debug("%s %s: delete done, re-add HID service\r\n", LOG_TAG, __func__);
    g_service_handle = 0;
    (void)ble_hid_add_hid_service();   /* add cb 会重置状态机并逐特征重建 */
}

static void ble_hid_register_gatts_callbacks(void)
{
    gatts_callbacks_t service_cb = { 0 };

    service_cb.add_service_cb = ble_hid_service_add_cbk;
    service_cb.add_characteristic_cb = ble_hid_chara_add_cbk;
    service_cb.add_descriptor_cb = ble_hid_desc_add_cbk;
    service_cb.read_request_cb = ble_hid_read_req_cbk;
    service_cb.write_request_cb = ble_hid_write_req_cbk;
    service_cb.stop_service_cb = ble_hid_stop_service_cbk;     /* 重建回调链 */
    service_cb.delete_service_cb = ble_hid_delete_service_cbk;  /* 重建回调链 */

    if (gatts_register_callbacks(&service_cb) != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: register gatts callbacks fail\r\n", LOG_TAG, __func__);
    }
}

/* 创建 HID 服务(主服务 0x1812) */
static errcode_t ble_hid_add_hid_service(void)
{
    bt_uuid_t service_uuid = { 0 };
    errcode_t ret;

    ble_uuid16_to_bt_uuid(BLE_UUID_SERVICE_HID, &service_uuid);
    ret = gatts_add_service(g_server_id, &service_uuid, true);
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: add HID service fail[0x%x]\r\n", LOG_TAG, __func__, ret);
    }
    return ret;
}

/* ------------------------- 对外接口 ------------------------- */
errcode_t ble_hid_server_init(void)
{
    bt_uuid_t app_uuid = { 0 };
    errcode_t ret;

    /* 1. 使能 BLE 协议栈(使能完成回调里再开广播) */
    ble_hid_register_gap_callbacks();
    ret = enable_ble();
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: enable_ble fail[0x%x]\r\n", LOG_TAG, __func__, ret);
        return ret;
    } else {
        log_info("%s %s: enable_ble OK\r\n", LOG_TAG, __func__);
    }

    /* 2. 注册 GATT Server */
    ble_hid_register_gatts_callbacks();
    app_uuid.uuid_len = 2;   /* 应用UUID, 仅作标识, 无实际含义 */
    app_uuid.uuid[0] = 0x00;
    app_uuid.uuid[1] = 0x00;
    ret = gatts_register_server(&app_uuid, &g_server_id);
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: register server fail[0x%x]\r\n", LOG_TAG, __func__, ret);
        return ret;
    }

    /* 3. 添加 HID 服务(特征在 add_service 回调里按序追加) */
    ret = ble_hid_add_hid_service();
    if (ret != ERRCODE_BT_SUCCESS) {
        return ret;
    }

    log_info("%s %s: ok, wait for host connect\r\n", LOG_TAG, __func__);
    return ERRCODE_BT_SUCCESS;
}

errcode_t ble_hid_server_deinit(void)
{
    gatts_unregister_server(g_server_id);
    disable_ble();
    log_info("%s %s: Exit\r\n", LOG_TAG, __func__);

    return ERRCODE_BT_SUCCESS;
}

errcode_t ble_hid_send_input_report(const uint8_t *report, uint16_t len)
{
    uint8_t frame[BLE_HID_INPUT_REPORT_LEN];
    errcode_t ret;

    if (!g_host_connected) {
        if (!g_log_drop_unconn) {
            g_log_drop_unconn = true;
            log_info("%s %s: DROP input report: host NOT connected\r\n", LOG_TAG, __func__);
        }
        return ERRCODE_BT_NOT_READY;   /* 主机未连接: 丢弃 */
    }
    if (!g_input_ntf_on) {
        if (!g_log_drop_unsub) {
            g_log_drop_unsub = true;
            log_info("%s %s: DROP input report: host NOT subscribed CCCD, notify disabled\r\n",
                    LOG_TAG, __func__);
        }
        return ERRCODE_BT_NOT_READY;   /* 主机未订阅通知: 丢弃 */
    }
    if ((report == NULL) || (len > BLE_HID_INPUT_REPORT_LEN)) {
        return ERRCODE_BT_PARAM_ERR;
    }

    /* 防御: 输入报告特征句柄未就绪(服务未建好/句柄丢失)时禁止发送,
       避免出现以 handle=0 空发 notify 导致主机永远收不到报告的假"成功" */
    if (g_attr_handle[HD_INPUT_REPORT] == 0) {
        log_error("%s %s: input report handle NOT ready(0), drop\r\n", LOG_TAG, __func__);
        return ERRCODE_BT_STATE_ERR;
    }

    /* 与探针同款: 局部帧缓冲, 载荷=8字节原始报告, 无Report ID */
    memset_s(frame, sizeof(frame), 0, sizeof(frame));
    if (len > 0) {
        (void)memcpy_s(frame, sizeof(frame), report, len);
    }

    gatts_ntf_ind_t ntf = { 0 };
    ntf.attr_handle = g_attr_handle[HD_INPUT_REPORT];
    ntf.value_len = sizeof(frame);
    ntf.value = frame;
    ret = gatts_notify_indicate(g_server_id, g_conn_id, &ntf);

    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: notify fail[0x%x]\r\n", LOG_TAG, __func__, ret);
    } else if (!g_log_first_send) {
        /* 诊断: 首次成功发送时打印实际使用的句柄与长度 —— 期望 hdl[0x14] len[8] */
        g_log_first_send = true;
        log_info("%s %s: notify OK: server[%u] conn[%u] hdl[0x%x] len[%u]\r\n",
            LOG_TAG, __func__, g_server_id, g_conn_id,
            g_attr_handle[HD_INPUT_REPORT], (uint16_t)sizeof(frame));
    }
    return ret;
}

errcode_t ble_hid_disconnect_host(void)
{
    if (!g_host_connected) {
        return ERRCODE_BT_NOT_READY;
    }
    log_info("%s %s: disconnect host\r\n", LOG_TAG, __func__);
    g_host_connected = false;
    return gap_ble_disconnect_remote_device(&g_host_addr);
}

/** 本次连接是否已收到主机CCCD订阅写(重连兜底判断: 主机是否重新订阅/栈是否已登记) */
bool ble_hid_ccc_written_since_connect(void)
{
    return g_ccc_written;
}

/** 主动发起与主机的配对/加密(重连链路未加密时, 促使Windows完成加密与订阅流程) */
errcode_t ble_hid_pair_with_host(void)
{
    errcode_t ret;

    if (!g_host_connected) {
        return ERRCODE_BT_NOT_READY;
    }
    ret = gap_ble_pair_remote_device(&g_host_addr);
    log_info("%s %s: pair_remote_device ret[0x%x]\r\n", LOG_TAG, __func__, ret);
    return ret;
}

/**
 * 删除并重建HID服务(重连兜底实验): Windows自动重连用缓存不重新发现/不重写CCCD,
 * 栈的通知登记只由"主机写CCCD"驱动 → 停止并删除服务后重建, 若栈在服务结构变化时向
 * 已订阅 Service Changed(0x2A05)的主机发指示, 或句柄变化使Windows缓存失效, 主机将
 * 重新发现并重写订阅。重建为异步回调链: stop → delete → add(走原有回调状态机)。
 */
errcode_t ble_hid_rebuild_service(void)
{
    if (!g_host_connected || (g_service_handle == 0) || g_rebuild_pending) {
        return ERRCODE_BT_NOT_READY;
    }
    log_info("%s %s: stop service hdl[0x%X] for rebuild\r\n", LOG_TAG, __func__, g_service_handle);
    g_rebuild_pending = true;
    /* 服务即将被删除重建: 新服务实例的CCCD通知登记从零开始(值回到0x0000, 栈内无登记),
       应用侧订阅镜像必须同步复位 —— 否则发送端仍按"已订阅"放行, 通知在栈内被静默丢弃,
       表现为 "notify OK 但PC收不到按键"(此前 rebuild 后按键全丢的根因) */
    g_input_ntf_on = false;
    g_ccc_written  = false;
    g_log_drop_unsub = false;
    /* 诊断: 打印同步返回值 —— 该栈在状态不满足(如服务使用中)时同步返回错误码且不触发
       stop_service 回调(实测 delete 同场景返回 0x80006007), 此日志可确认拒绝原因 */
    errcode_t ret = gatts_stop_service(g_server_id, g_service_handle);
    log_info("%s %s: gatts_stop_service ret[0x%x]\r\n", LOG_TAG, __func__, ret);
    return ret;
}

/* 取消与当前主机的配对(删除键盘侧bond)。
 * 注意: 实测删除键盘侧bond后Windows仍按原地址/原bond重连, 加密失败(配对 status[0x80006000]),
 *       形成"连接→配对失败→断开→重连"死循环, 自动恢复流程已不再使用此函数(改用换地址)。 */
errcode_t ble_hid_remove_pair(void)
{
    bd_addr_t saved = {0};
    errcode_t ret;

    if (!g_host_connected) {
        return ERRCODE_BT_NOT_READY;
    }
    /* 先保存地址副本: ble_hid_disconnect_host() 后 DISCONNECTED 回调会清零 g_host_addr */
    (void)memcpy_s(&saved, sizeof(saved), &g_host_addr, sizeof(g_host_addr));
    log_info("%s %s: remove pair[%02X:%02X:%02X:%02X:%02X:%02X] to force re-pair\r\n",
        LOG_TAG, __func__,
        saved.addr[0], saved.addr[1], saved.addr[2],
        saved.addr[3], saved.addr[4], saved.addr[5]);
    ret = gap_ble_remove_pair(&saved);
    if (ret != ERRCODE_BT_SUCCESS) {
        log_error("%s %s: remove pair ret[0x%x]\r\n", LOG_TAG, __func__, ret);
    }
    (void)ble_hid_disconnect_host();   /* 断开, 断开回调里会自动重启广播 */
    ble_hid_restart_adv();             /* 兜底: 确保广播尽快恢复 */
    return ret;
}

/* 更换本地BLE地址(最后兜底): Windows视作全新设备强制重新配对/发现/订阅。
 * 新地址 = 旧地址(无覆盖时用DieID)整体异或0x5A, 保证每次不同;
 * 落盘持久化, 重启后沿用, 避免回到旧地址触发同样的"缓存不订阅"问题 */
errcode_t ble_hid_change_addr(void)
{
    bd_addr_t new_addr = {0};
    uint8_t old[BD_ADDR_LEN] = {0};
    uint8_t die_id[16] = {0};

    if (get_ble_addr_override(old, sizeof(old)) != 0) {
        for (uint8_t i = 0; i < BD_ADDR_LEN; i++) {
            new_addr.addr[i] = (uint8_t)(old[i] ^ 0x5A);
        }
    } else if (uapi_efuse_get_die_id(die_id, sizeof(die_id)) == ERRCODE_SUCC) {
        for (uint8_t i = 0; i < BD_ADDR_LEN; i++) {
            new_addr.addr[i] = (uint8_t)(die_id[i+6] ^ 0x5A);
        }
    } else {
        for (uint8_t i = 0; i < BD_ADDR_LEN; i++) {
            new_addr.addr[i] = 0x5A;
        }
    }

    log_info("%s %s: change local addr -> [%02X:%02X:%02X:%02X:%02X:%02X]\r\n",
        LOG_TAG, __func__,
        new_addr.addr[0], new_addr.addr[1], new_addr.addr[2],
        new_addr.addr[3], new_addr.addr[4], new_addr.addr[5]);

    if (g_host_connected) {
        (void)ble_hid_disconnect_host();
        usleep(200 * 1000);   /* 等待断开完成, 再设置新地址(连接中设置地址可能被栈拒绝) */
    }
    (void)gap_ble_remove_all_pairs();             /* 清理键盘侧旧地址的残留bond */
    (void)set_ble_addr_override(new_addr.addr, BD_ADDR_LEN);  /* 持久化: 重启后沿用 */
    (void)gap_ble_set_local_addr(&new_addr);
    ble_hid_restart_adv();
    return ERRCODE_BT_SUCCESS;
}

bool ble_hid_host_connected(void)
{
    return g_host_connected;
}

uint32_t ble_hid_host_count(void)
{
    return g_host_connected ? 1 : 0;
}

void ble_hid_get_host_addr(uint8_t *addr, uint8_t len)
{
    if (addr == NULL) {
        return;
    }
    if (len > BD_ADDR_LEN) {
        len = BD_ADDR_LEN;
    }
    if (g_host_connected) {
        (void)memcpy_s(addr, len, g_host_addr.addr, len);
    } else {
        memset_s(addr, len, 0, len);
    }
}

uint8_t ble_hid_get_host_leds(void)
{
    return g_host_leds;
}

uint8_t ble_hid_get_host_suspend(void)
{
    return g_host_suspend;
}
