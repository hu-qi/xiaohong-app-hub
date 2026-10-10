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
#include <unistd.h>
#include <string.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "task_entry.h"

#include "sle_server.h"
#include "sle_server_adv.h"

extern osMessageQueueId_t g_63tx_event_qid;
extern osMessageQueueId_t g_sle_server_event_qid;
int sle_server_recv_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0)) {
        printf("[%s] invalid args\r\n", __func__);
        return 0;
    }
    int32_t value = 0;
    uint16_t cmd = 0;

    if (strncmp(data, "temp:", 5) == 0) {
        value = atoi(data + 5);
        set_sensors_temp(value);
        cmd = 0x0F20;  // REPORT_SENSOR_TEMP
    } else if (strncmp(data, "humi:", 5) == 0) {
        value = atoi(data + 5);
        set_sensors_humi(value);
#if 1  // 先同时发送温度+湿度数据，兼容旧协议
        cmd = 0x0F10;
        osMessageQueuePut(g_63tx_event_qid, (void *)&cmd, 0, 0);
#endif
        cmd = 0x0F21;  // REPORT_SENSOR_HUMI
    } else if (strncmp(data, "smoke:", 6) == 0) {
        //value = atoi(data + 6);
        //set_sensors_smoke(value);
        //cmd = 0x0F22;  // REPORT_SENSOR_SMOKE
    } else if (strncmp(data, "light:", 6) == 0) {
        //value = atoi(data + 6);
        //set_sensors_light(value);
        //cmd = 0x0F23;  // REPORT_SENSOR_LIGHT
    }

    if (cmd > 0) {
        printf("[%s   ] len[%d]{%s} => value[%d]\r\n", __func__, len, (char*)data, value);
        osMessageQueuePut(g_63tx_event_qid, (void *)&cmd, 0, 0);
    }

    return len;
}

int sle_server_send_data(uint8_t *data, uint16_t len)
{
    uint8_t* ptr = data;
    uint16_t plen = len;

    if ((ptr == NULL) || (plen == 0)) {
        char buff[64] = {0};
        uint8_t local_address[SLE_ADDR_LEN] = {0};
        sle_server_get_local_address(local_address, SLE_ADDR_LEN);
        snprintf(buff, 64, "ACK_From_Server: %02X\0", local_address[SLE_ADDR_LEN-1]);

        ptr = (uint8_t *)buff;
        plen = strlen(buff);
    }

    printf("[%s   ] conn_cnt[%d] len[%d]{%s}\r\n", __func__,
            sle_server_get_conn_count(), plen, (char*)ptr);

    return sle_server_broadcast_msg(ptr, plen);
}

static void SleServerTask(void)
{
#if USE_CUSTOM_MAC_ADDR  // 设置服务端地址，如果参数非法，则使用随机地址
    uint8_t server_addr[SLE_ADDR_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xDD};
    sle_server_set_local_address(server_addr, SLE_ADDR_LEN);
#else
    sle_server_set_local_address(NULL, 0);
#endif
    sle_server_init();

    uint16_t msg_recv = 0;
    while (1) {
        if (sle_server_get_conn_count() == 0) {
            usleep(100*1000);
            continue;
        }

        if (osOK != osMessageQueueGet(g_sle_server_event_qid, (void *)&msg_recv, NULL, 40)) {
            continue;
        }

        printf("\r\n");
        printf("[  SleServerTask        ] ========================================================\r\n");
        printf("[  conn status          ] max[%u] conn[%u]\r\n",
            sle_server_get_max_slots(), sle_server_get_conn_count());

    // UART RX转发来的查询传感器数据的命令，SLE Server先判断对应的传感器是否有连接
    // 有正常连接再去发送TEMP/HUMI数据
        switch(msg_recv) {
            case 0x0F10: {  // REPORT_SENSOR_TEMP_AND_HUMI
                // TODO
                post_msg_event(g_63tx_event_qid, 0x0F10);
                break;
            }
            case 0x0F20: {  // REPORT_SENSOR_TEMP
                // TODO
                post_msg_event(g_63tx_event_qid, 0x0F20);
                break;
            }
            case 0x0F21: {  // REPORT_SENSOR_HUMI
                // TODO
                post_msg_event(g_63tx_event_qid, 0x0F21);
                break;
            }

            default: {
                break;
            }
        }
    }
}

uint32_t SleServerTaskEntry(void)
{
    task_config_t task_conf = {
        "SSvrTask",      // task name
        1024*8,          // stack_size
        25,              // priority
        (void*)SleServerTask  // func
    };

    return create_task(&task_conf, NULL);
}