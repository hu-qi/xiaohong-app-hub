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
#include "ohos_init.h"
#include "cmsis_os2.h"

#include "sle_server.h"
#include "sle_server_adv.h"

int sle_server_recv_data(uint16_t conn_id, uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0)) {
        printf("[%s] invalid args\r\n", __func__);
        return 0;
    }
    uint8_t client_address[SLE_ADDR_LEN] = {0};
    sle_server_get_client_address(conn_id, client_address, SLE_ADDR_LEN);
    printf("[%s   ] <<<<<<<<<<<<<<< conn_id[0x%02X] client_addr[0x%02X] len[%d]{%s}\r\n",
        __func__, conn_id, client_address[SLE_ADDR_LEN-1], len, (char*)data);

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

    char data[64] = {0};
    uint16_t cnt = 0;
    uint8_t local_address[SLE_ADDR_LEN] = {0};
    sle_server_get_local_address(local_address, SLE_ADDR_LEN);
    while (1) {
        printf("\r\n");
        printf("[  SleServerTask        ] ========================================================\r\n");
        printf("[  conn status          ] max[%u] conn[%u]\r\n",
            sle_server_get_max_slots(), sle_server_get_conn_count());
        if (sle_server_get_conn_count() > 0) {
            cnt++;
            snprintf(data, 64, "====>>>>cnt[%d] %02X:%02X:%02X:%02X:%02X:%02X ====>>>>\0",
                cnt, local_address[0], local_address[1], local_address[2],
                local_address[3], local_address[4], local_address[5]);

            sle_server_send_data((uint8_t *)data, strlen(data));
        }
        sleep(5);
    }
}

static void SleServerTaskEntry(void)
{
    osThreadAttr_t attr;
    attr.name = "SleServerTask";
    attr.stack_size = 1024 * 10;
    attr.priority = osPriorityNormal;

    if (osThreadNew(SleServerTask, NULL, &attr) == NULL) {
        printf("[SleServerTaskEntry] Create SleServerTask Failed!\n");
    } else {
        printf("[SleServerTaskEntry] Create SleServerTask Success!\n");
    }
}
SYS_RUN(SleServerTaskEntry);
