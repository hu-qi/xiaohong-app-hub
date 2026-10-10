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
#include <stdbool.h>
#include <stdint.h>

#include "cmsis_os2.h"
#include "ohos_init.h"

#include "pinctrl.h"
#include "common_def.h"
#include "gpio.h"

#define LOG_TAG         "[  FanTask              ]"  // "[fan_demo]"

static int16_t  temp = 0;
static uint16_t humi = 0;

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

    // Client 发送4个字节的数据，Server端会在后面多加一个字节的'\0'，标识数据的结束。
    if (len == 5) {
        printf("[%s   ] <<<<<<<<<<<<<<< conn_id[0x%02X] client_addr[0x%02X] len[%d]{%02X,%02X,%02X,%02X}\r\n",
            __func__, conn_id, client_address[SLE_ADDR_LEN-1], len, data[0], data[1], data[2], data[3]);
        temp = (int16_t) ((data[1]<<8)|data[0]);
        humi = (uint16_t)((data[3]<<8)|data[2]);
    } else {
        printf("[%s   ] <<<<<<<<<<<<<<< conn_id[0x%02X] client_addr[0x%02X] len[%d]{%s}\r\n",
            __func__, conn_id, client_address[SLE_ADDR_LEN-1], len, (char*)data);
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



static void button_key_event_cb(void)
{
    uint8_t key = uapi_gpio_get_val(FAN_BUTTON_PIN);
    if (key == 0) {  // Press
        uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_HIGH);  // Lo:Off, Hi:On
    } else {
        uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_LOW);  // Lo:Off, Hi:On
        uapi_gpio_set_val(FAN_BUTTON_PIN, 1);
    }
}

static void button_init(void)
{
    uapi_pin_set_mode(FAN_BUTTON_PIN, FAN_BUTTON_MODE);
    uapi_gpio_set_dir(FAN_BUTTON_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(FAN_BUTTON_PIN, 1);
    uapi_gpio_register_isr_func(FAN_BUTTON_PIN, GPIO_INTERRUPT_LOW, button_key_event_cb);
}

static void fan_gpio_init(void)
{
    uapi_pin_set_mode(FAN_GPIO_PIN, FAN_GPIO_MODE);
    uapi_pin_set_ds(FAN_GPIO_PIN, PIN_DS_7);
    uapi_gpio_set_dir(FAN_GPIO_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_LOW);
}

void FanTask(void)
{
    fan_gpio_init();
    button_init();

#if USE_CUSTOM_MAC_ADDR  // 设置服务端地址，如果参数非法，则使用随机地址
    uint8_t server_addr[SLE_ADDR_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xDD};
    sle_server_set_local_address(server_addr, SLE_ADDR_LEN);
#else
    sle_server_set_local_address(NULL, 0);
#endif
    sle_server_init();

    while (1) {
        printf("\r\n");
        printf("[  FanTask              ] ========================================================\r\n");
        printf("[  conn status          ] max[%u] conn[%u]\r\n",
            sle_server_get_max_slots(), sle_server_get_conn_count());
        if (sle_server_get_conn_count() > 0) {
            printf("[  FanTask              ] temp[%d] humi[%d]\r\n", temp, humi);
            if (temp > FAN_TRIGGER_THRESHOLD) {  // 3400 => 34.00
                uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_HIGH);
            } else {
                uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_LOW);
            }
        }
        sleep(2);
    }
}

static void FanTaskEntry(void)
{
    osThreadAttr_t attr = {0};
    attr.name = "FanTask";
    attr.stack_size = 1024*8;
    attr.priority = 25;
    if (osThreadNew(FanTask, NULL, &attr) == NULL) {
        printf("%s FanTaskEntry creat FanTask NG\r\n", LOG_TAG);
    } else {
        printf("%s FanTaskEntry creat FanTask OK\r\n", LOG_TAG);
    }
}
SYS_RUN(FanTaskEntry);