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

#ifndef __NET_WIFI_CONFIG_H__
#define __NET_WIFI_CONFIG_H__

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "cmsis_os2.h"

#include "lwip/netifapi.h"
#include "lwip/nettool/misc.h"
#include "soc_osal.h"
#include "errcode.h"
#include "wifi_hotspot.h"
#include "wifi_hotspot_config.h"
#include "wifi_event.h"
#include "td_base.h"
#include "td_type.h"

#define HOTSPOT_CHANNEL  7
#define KB_HOTSPOT_SSID  "Xiaohong_KB"   /* 键盘配置模式热点的SSID */
#define KB_HOTSPOT_PSWD  ""              /* 开放热点，无需密码 */

errcode_t start_hotspot(char *ssid, char *pswd);
errcode_t stop_hotspot(void);
uint32_t KbConfigServerTaskEntry(void);
void kb_config_server_stop(void);

#endif  /* __NET_WIFI_CONFIG_H__ */
