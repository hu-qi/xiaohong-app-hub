/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)
*/

#include "app_installer.h"
#include "log.h"

/*
 * 安装器 stub 实现 — 当前为空操作
 *
 * 保留扩展点，未来可接入：
 *   - WASM 模块加载
 *   - OTA 应用分发
 *   - LittleFS 持久化安装
 */

static uint8_t s_install_status = XH_INSTALL_IDLE;

void xh_app_installer_init(void)
{
    s_install_status = XH_INSTALL_IDLE;
    log_info("[AppInstaller] init (stub mode, dynamic install not supported)\r\n");
}

bool xh_app_installer_install(const char *url)
{
    (void)url;
    log_info("[AppInstaller] install: not supported in MVP\r\n");
    s_install_status = XH_INSTALL_FAILED;
    return false;
}

bool xh_app_installer_uninstall(uint8_t app_id)
{
    (void)app_id;
    log_info("[AppInstaller] uninstall: not supported in MVP\r\n");
    return false;
}

uint8_t xh_app_installer_get_status(void)
{
    return s_install_status;
}
