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

#include <stdio.h>
#include <string.h>

#include "task_entry.h"
#include "settings.h"
#include "net_wifi_config.h"
#include "soc_osal.h"
#include "cmsis_os2.h"

void WiFiTask(void)
{
    errcode_t ret = ERRCODE_FAIL;

    printf("[WiFiTask] -----------------------------------------\r\n");
    while (wifi_is_wifi_inited() == 0) {
        (void)osDelay(10);               /* 10 tick = 100ms: 等待100ms后判断状态 */
    }

    char strSSID[WIFI_SSID_LEN_MAX+1] = {0};
    char strPSWD[WIFI_PSWD_LEN_MAX+1] = {0};
    ret = get_wifi_config(strSSID, WIFI_SSID_LEN_MAX, strPSWD, WIFI_PSWD_LEN_MAX);
    if ((ret == ERRCODE_SUCC) && (strlen(strSSID) > 0)) {  // 每次开机后的自动配网
        log_info("[WiFiTask] eWifi_St_AutoConnecting hotspot[%s][%s]\r\n", strSSID, strPSWD);
        set_wifi_status(eWifi_St_AutoConnecting);
        ret = connect_to_hotspot(strSSID, strPSWD);
        log_info("[WiFiTask] Connect to hotspot result[%d]:[%s]\r\n",
            ret, (ret == ERRCODE_SUCC)?"eWifi_St_Connected":"eWifi_St_DisConnected");

        set_wifi_status((ret == ERRCODE_SUCC)?(eWifi_St_Connected):(eWifi_St_DisConnected));
    }
    osThreadExit();
    printf("[WiFiTask] -------------------------------\r\n");
}

uint32_t WiFiTaskEntry(void)
{
    task_config_t task_conf = {
        "WiFiTask",      // task name
        1024*4,          // stack_size
        24,              // priority
        (void*)WiFiTask  // func
    };

    return create_task(&task_conf, NULL);
}
// SYS_RUN(WiFiTaskEntry);
