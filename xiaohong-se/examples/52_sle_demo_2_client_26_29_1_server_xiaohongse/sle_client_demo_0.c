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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ohos_init.h"
#include "cmsis_os2.h"

#include "sle_client.h"

int sle_client_send_data(uint8_t *data, uint16_t len)
{
    printf("[sle_client_send_data]==>> len[%d]{%s}\r\n", len, (char*)data);
    return sle_client_broadcast_msg(data, len);
}

int sle_client_recv_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    uint8_t server_address[SLE_ADDR_LEN] = {0};
    sle_client_get_server_address(conn_id, server_address, SLE_ADDR_LEN);
    printf("[sle_client_recv_data]<<== server_addr[0x%02X] conn_id[0x%02X] len[%d]{%s}\r\n",
        server_address[SLE_ADDR_LEN-1], conn_id, len, (char*)data);
}

void SleClientTask(void)
{
    sle_set_server_name(SLE_SERVER_NAME);
    sle_client_init();

#if USE_CUSTOM_MAC_ADDR  // 设置客户端本地地址，如果参数非法，则使用随机地址
    uint8_t client_addr[SLE_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    sle_client_set_local_address(client_addr, SLE_ADDR_LEN);
#else
    sle_client_set_local_address(NULL, 0);
#endif

    char send_buff[64] = {0};
    uint16_t count = 0;
    sle_addr_t local_address = {0};
    sle_get_local_addr(&local_address);

    while (1) {
        printf("\r\n");
        printf("[  SleClientTask         ] ========================================================\r\n");
        printf("[  conn/ready/ntf status ] max[%u] conn[%u] ready[%u] ntf[%u]\r\n",
            sle_client_get_max_slots(), sle_client_get_conn_count(),
            sle_client_get_ready_count(), sle_client_get_ntf_subscribed_count());

        if (sle_client_get_ready_count() == 0) {
            sleep(1);
            continue;
        }

        count++;
        sprintf(send_buff, "Client[0x%02X] heart beat: %u\0", local_address.addr[SLE_ADDR_LEN-1], count);
        sle_client_send_data((uint8_t *)send_buff, strlen(send_buff));

        sleep(5);
    }
}

static void SleClientTaskEntry(void)
{
    osThreadAttr_t attr;
    attr.name = "SleClientTask";
    attr.stack_size = 1024 * 10;
    attr.priority = osPriorityNormal;

    if (osThreadNew(SleClientTask, NULL, &attr) == NULL)
        printf("[SleClientTaskEntry] Create SleClientTask Failed!\n");
    else
        printf("[SleClientTaskEntry] Create SleClientTask Success!\n");
}

SYS_RUN(SleClientTaskEntry);
