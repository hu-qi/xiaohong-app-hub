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

#ifndef __BLE_CLIENT_H__
#define __BLE_CLIENT_H__

#include <stdbool.h>
#include <stdint.h>
#include "bts_def.h"
#include "bts_le_gap.h"
#include "bts_gatt_stru.h"
#include "bts_gatt_server.h"

/* 键盘作为 BLE Peripheral(Server)广播的设备名: PC 蓝牙列表里显示的名字 */
#ifndef BLE_HID_DEV_NAME
#define BLE_HID_DEV_NAME  "AtomGit-KB"
#endif

/* HID 输入报告(键盘按键): 与 SLE/USB 通道一致的 8 字节 Boot Keyboard 报告
 *   Byte[0]  : Modifier 键(bit0 LCtrl/bit1 LShift/bit2 LAlt/bit3 LGUI/bit4~7 右侧)
 *   Byte[1]  : 保留(0x00)
 *   Byte[2~7]: 按键键码(最多同时 6 键)
 * 采用经典"无Report ID"布局(与USB Boot键盘一致, Windows兼容性最好):
 * BLE 通知/读写的载荷就是上述8字节原样报告, 不加任何前缀;
 * Report Reference 描述符里的 Report ID 填 0x00(BLE_HID_REPORT_ID_NONE)。 */
#define BLE_HID_INPUT_REPORT_LEN        (8)
#define BLE_HID_OUTPUT_REPORT_LEN       (1)
#define BLE_HID_REPORT_ID_NONE          (0x00)

// HID 服务的标准 16bit UUID(蓝牙 SIG 定义)
#define BLE_UUID_SERVICE_HID            0x1812  // Human Interface Device 服务
#define BLE_UUID_CHAR_HID_INFO          0x2A4A  // HID Information 特征
#define BLE_UUID_CHAR_HID_REPORT_MAP    0x2A4B  // HID Report Map 特征
#define BLE_UUID_CHAR_HID_CONTROL_POINT 0x2A4C  // HID Control Point 特征
#define BLE_UUID_CHAR_HID_REPORT        0x2A4D  // HID Report 特征(输入/输出各一个实例)
#define BLE_UUID_DESC_REPORT_REF        0x2908  // Report Reference 描述符
#define BLE_UUID_DESC_CCCD              0x2902  // Client Characteristic Configuration 描述符

// Report Reference 描述符里的报告类型(Bluetooth HID over GATT Profile)
#define BLE_HID_REPORT_TYPE_INPUT       0x01
#define BLE_HID_REPORT_TYPE_OUTPUT      0x02

// HID Information 特征标志位
#define BLE_HID_INFO_FLAG_REMOTE_WAKE   0x01
#define BLE_HID_INFO_FLAG_NORM_CONN     0x02

/* 广播数据里的 Appearance(键盘=0x03C1), 与 HID 服务 UUID 一起帮助 PC 识别为键盘 */
#define BLE_ADV_APPEARANCE_KEYBOARD     0x03C1

/* 诊断: 换用测试本地地址广播(使PC把键盘视为全新设备, 判别"配对后不读HID服务"是
 * 固件侧服务发现问题, 还是PC对旧地址的顽固状态)。仅诊断用, 产品默认关闭。 */
#ifndef BLE_HID_TEST_ADDR_SWITCH
#define BLE_HID_TEST_ADDR_SWITCH        0
#endif

/**
 * 初始化 BLE-HID 服务端(使能BLE协议栈+GAP/GATT回调+HID服务+开始广播)。
 * 由 BleClientTask 在任务上下文调用一次; 失败返回非 ERRCODE_SUCC。
 */
errcode_t ble_hid_server_init(void);
errcode_t ble_hid_server_deinit(void);

/**
 * 主动断开当前已连接的 BLE 主机(Central), 之后继续广播等待重新连接。
 * 无主机连接时直接返回。
 */
errcode_t ble_hid_disconnect_host(void);

/** 本次连接是否已收到主机写CCCD订阅(重连兜底: 判断主机是否重新订阅/栈是否已登记通知) */
bool ble_hid_ccc_written_since_connect(void);

/** 主动发起与主机的配对/加密(重连链路未加密时使用, 促使Windows完成加密与订阅) */
errcode_t ble_hid_pair_with_host(void);

/**
 * 删除并重建HID服务(重连兜底): 促使Windows缓存失效并重新发现/重写CCCD。
 * 仅当已连接且服务已建好时有效。
 */
errcode_t ble_hid_rebuild_service(void);

/**
 * 取消与当前主机的配对(删除键盘侧bond)。
 * 注意: 实测删除键盘侧bond后Windows仍按原地址/原bond重连, 加密失败(配对 status[0x80006000]),
 *       形成"连接→配对失败→断开→重连"死循环, 故自动恢复流程不再使用(改用 ble_hid_change_addr)。
 * 仅当已连接时有效; 成功后断开主机并恢复广播。
 */
errcode_t ble_hid_remove_pair(void);

/**
 * 更换本地BLE地址(最后兜底): 使Windows视作全新设备强制重新配对/发现/订阅。
 * 新地址由旧地址(或DieID)异或0x5A派生并落盘持久化, 重启后沿用。
 */
errcode_t ble_hid_change_addr(void);

/**
 * 重新开始广播(设置广播数据/参数后启动)。通常在主机断开/重置配对后调用。
 */
errcode_t ble_hid_restart_adv(void);

/**
 * 向已连接主机发送一帧 HID 键盘输入报告(report 为 8 字节原始报告, 无 Report ID 前缀,
 * 与 SLE/USB 通道的报文完全一致)。仅在 已连接 && 主机已订阅(CCCD=0x0001) 时才会真正发出。
 * 返回 ERRCODE_BT_SUCCESS 表示已投递, 否则(未连接/未订阅)返回 ERRCODE_BT_NOT_READY。
 */
errcode_t ble_hid_send_input_report(const uint8_t *report, uint16_t len);

/** 是否有 BLE 主机(Central)已连接 */
bool ble_hid_host_connected(void);

/** 当前已连接的 BLE 主机数量(0 或 1) */
uint32_t ble_hid_host_count(void);

/** 获取已连接主机的蓝牙地址(未连接时清零) */
void ble_hid_get_host_addr(uint8_t *addr, uint8_t len);

/** 获取主机通过 HID Output Report 下发的 LED 状态(bit0 NumLock/bit1 CapsLock/bit2 ScrollLock...) */
uint8_t ble_hid_get_host_leds(void);

/** 获取主机通过 HID Control Point 下发的挂起状态(0x00=Suspend/0x01=Exit Suspend) */
uint8_t ble_hid_get_host_suspend(void);

/**
 * 弱回调: 主机写 HID Output Report(如键盘LED)时触发, 由应用层覆盖以点亮本机状态灯等。
 * leds: 见 ble_hid_get_host_leds() 的位定义。默认实现为空。
 */
__attribute__((weak)) void ble_hid_on_host_leds(uint8_t leds);

/**
 * 弱回调: 主机写 HID Control Point(Suspend/Exit Suspend)时触发。默认实现为空。
 */
__attribute__((weak)) void ble_hid_on_host_suspend(uint8_t suspend);

#endif /* __BLE_CLIENT_H__ */
