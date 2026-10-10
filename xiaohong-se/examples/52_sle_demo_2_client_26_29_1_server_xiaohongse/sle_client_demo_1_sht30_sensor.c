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
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "ohos_init.h"

#include "errcode.h"
#include "pinctrl.h"
#include "i2c.h"
#include "sht30.h"

#define LOG_TAG                    "[  Sht30SensorTask       ]"  // "[sht30_demo]"
#define MEASURE_INTERVAL_TICKS     100

#ifndef CONFIG_I2C_SCL_MASTER_PIN
#define CONFIG_I2C_SCL_MASTER_PIN  16
#endif
#ifndef CONFIG_I2C_SDA_MASTER_PIN
#define CONFIG_I2C_SDA_MASTER_PIN  15
#endif
#ifndef CONFIG_I2C_MASTER_PIN_MODE
#define CONFIG_I2C_MASTER_PIN_MODE 2
#endif
#ifndef CONFIG_I2C_MASTER_BUS_ID
#define CONFIG_I2C_MASTER_BUS_ID   1
#endif
#ifndef I2C_SET_BANDRATE
#define I2C_SET_BANDRATE           400000
#endif
#ifndef I2C_MASTER_ADDR
#define I2C_MASTER_ADDR            0
#endif



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

void sht30_broadcast_data(float temp, float humi)
{
    char* dataT[16] = {0};
    char* dataH[16] = {0};

    int16_t t100 = (int16_t)(temp * 100.0f + (temp >= 0.0f ? 0.5f : -0.5f));
    int16_t h100 = (int16_t)(humi * 100.0f + 0.5f);
    if (h100 < 0) {
        h100 = 0;
    }

    snprintf(dataT, 16, "temp:%d\0", t100);
    sle_client_send_data(dataT, strlen(dataT));

    snprintf(dataH, 16, "humi:%d\0", h100);
    sle_client_send_data(dataH, strlen(dataH));
}


void log_print_float_th(float temp, float humi)
{
    int t100 = (int)(temp * 100.0f + (temp >= 0.0f ? 0.5f : -0.5f));
    int h100 = (int)(humi * 100.0f + 0.5f);
    if (h100 < 0) {
        h100 = 0;
    }
    if (t100 < 0) {
        printf("%s measure: temp[-%d.%02d] humi[%d.%02d]\r\n",
                LOG_TAG, (-t100)/100, (-t100)%100, h100/100, h100%100);
    } else {
        printf("%s measure: temp[%d.%02d] humi[%d.%02d]\r\n",
                LOG_TAG, t100/100, t100%100, h100/100, h100%100);
    }
}


static errcode_t i2c_bus_init(void)
{
    i2c_bus_t bus = (i2c_bus_t) CONFIG_I2C_MASTER_BUS_ID;
    uapi_i2c_deinit(bus);
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    sht30_set_i2c_bus(bus);
    return uapi_i2c_master_init(bus, I2C_SET_BANDRATE, I2C_MASTER_ADDR);
}

static void Sht30SensorTask(void)
{
    float temp = 0.0f;
    float humi = 0.0f;

    osal_msleep(1000);
    if (i2c_bus_init() != ERRCODE_SUCC) {
        printf("%s i2c init NG\r\n", LOG_TAG);
        return;
    } else {
        printf("%s i2c init OK\r\n", LOG_TAG);
    }
    
    if (SHT30_Calibrate() != ERRCODE_SUCC) {
        printf("%s SHT30_Calibrate NG\r\n", LOG_TAG);
        return;
    } else {
        printf("%s SHT30_Calibrate OK\r\n", LOG_TAG);
    }

    sle_set_server_name(SLE_SERVER_NAME);
    sle_client_init();
#if USE_CUSTOM_MAC_ADDR  // 设置客户端本地地址，如果参数非法，则使用随机地址
    uint8_t client_addr[SLE_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    sle_client_set_local_address(client_addr, SLE_ADDR_LEN);
#else
    sle_client_set_local_address(NULL, 0);
#endif

    while (1) {
        printf("\r\n");
        printf("[  Sht30SensorTask       ] ========================================================\r\n");
        printf("[  conn/ready/ntf status ] max[%u] conn[%u] ready[%u] ntf[%u]\r\n",
            sle_client_get_max_slots(), sle_client_get_conn_count(),
            sle_client_get_ready_count(), sle_client_get_ntf_subscribed_count());
        if (sle_client_get_ready_count() == 0) {
            sleep(1);
            continue;
        }

        errcode_t mr = SHT30_StartMeasure();
        errcode_t gr = SHT30_GetMeasureResult(&temp, &humi);
        if (mr == ERRCODE_SUCC && gr == ERRCODE_SUCC) {
            log_print_float_th(temp, humi);
            sht30_broadcast_data(temp, humi);
        } else {
            printf("%s measure fail: mr=%u gr=%u\r\n", LOG_TAG, (unsigned)mr, (unsigned)gr);
        }
        osDelay(MEASURE_INTERVAL_TICKS*3);
    }
}

static void Sht30SensorTaskEntry(void)
{
    osThreadAttr_t attr = {0};
    attr.name = "Sht30SensorTask";
    attr.stack_size = 1024*8;
    attr.priority = 25;
    if (osThreadNew(Sht30SensorTask, NULL, &attr) == NULL) {
        printf("%s Sht30SensorTaskEntry creat Sht30SensorTask NG\r\n", LOG_TAG);
    } else {
        printf("%s Sht30SensorTaskEntry creat Sht30SensorTask OK\r\n", LOG_TAG);
    }
}
APP_FEATURE_INIT(Sht30SensorTaskEntry);