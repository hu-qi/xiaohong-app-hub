/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#ifndef __APP_INSTALLER_H__
#define __APP_INSTALLER_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * 应用安装器接口 — 为未来动态安装预留
 *
 * 当前版本（MVP）不实现动态安装：
 *   - 无 WASM 运行时
 *   - 无动态链接
 *   - 无 OTA 应用分发
 *
 * 此接口仅定义未来扩展点，实现为空 stub。
 * 后续可通过以下方式扩展：
 *   1. WASM 模块加载（wasmer/wasm3）
 *   2. ELF 动态加载（dlopen 风格）
 *   3. OTA 下载 + 校验 + 安装到 LittleFS
 *   4. Lua/JS 脚本引擎
 */

/* 安装状态 */
enum {
    XH_INSTALL_IDLE = 0,
    XH_INSTALL_DOWNLOADING,
    XH_INSTALL_VERIFYING,
    XH_INSTALL_INSTALLING,
    XH_INSTALL_DONE,
    XH_INSTALL_FAILED,
};

/* 安装器初始化 */
void xh_app_installer_init(void);

/*
 * 安装应用（未来实现）
 *   url: 应用包下载地址
 *   返回 true 表示安装已启动（异步），false 表示不支持
 */
bool xh_app_installer_install(const char *url);

/* 卸载应用（未来实现） */
bool xh_app_installer_uninstall(uint8_t app_id);

/* 查询安装状态 */
uint8_t xh_app_installer_get_status(void);

#endif /* __APP_INSTALLER_H__ */
