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

#include "cmsis_os2.h"
#include "errcode.h"
#include "i2c.h"
#include "sht30.h"


static i2c_bus_t g_i2c_bus = (i2c_bus_t)CONFIG_I2C_MASTER_BUS_ID;

void sht30_set_i2c_bus(i2c_bus_t bus)
{
    g_i2c_bus = bus;
}

i2c_bus_t sht30_get_i2c_bus(void)
{
    return g_i2c_bus;
}

static uint8_t sht30_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFFU;
    uint8_t i;
    uint8_t j;

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 8; j > 0; j--) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x31U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static errcode_t sht30_write_cmd(uint8_t h, uint8_t l)
{
    uint8_t buf[2] = { h, l };
    i2c_data_t data = {0};
    data.send_buf = buf;
    data.send_len = 2;
    errcode_t ret = uapi_i2c_master_write(g_i2c_bus, (uint16_t)CONFIG_SHT30_I2C_ADDR_7B, &data);
    return ret;
}

static errcode_t sht30_read_bytes(uint8_t *buf, uint32_t len)
{
    i2c_data_t data = {0};
    data.receive_buf = buf;
    data.receive_len = len;
    errcode_t ret = uapi_i2c_master_read(g_i2c_bus, (uint16_t)CONFIG_SHT30_I2C_ADDR_7B, &data);
    return ret;
}

errcode_t SHT30_Calibrate(void)
{
    errcode_t ret = sht30_write_cmd(SHT30_CMD_SOFT_RST_H, SHT30_CMD_SOFT_RST_L);
    if (ret == ERRCODE_SUCC) {
        osal_mdelay(2);
        return ERRCODE_SUCC;
    }
    ret = sht30_write_cmd(SHT30_CMD_SINGLE_SHOT_HIGH_H, SHT30_CMD_SINGLE_SHOT_HIGH_L);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    osal_mdelay(2);
    return ERRCODE_SUCC;
}

errcode_t SHT30_StartMeasure(void)
{
    return sht30_write_cmd(SHT30_CMD_SINGLE_SHOT_HIGH_H, SHT30_CMD_SINGLE_SHOT_HIGH_L);
}

errcode_t SHT30_GetMeasureResult(float *temp, float *humi)
{
    uint8_t buf[6];

    if (temp == NULL || humi == NULL) {
        return ERRCODE_INVALID_PARAM;
    }
    (void)memset_s(buf, sizeof(buf), 0, sizeof(buf));
    osal_mdelay(SHT30_MEAS_DELAY_MS);
    errcode_t ret = sht30_read_bytes(buf, sizeof(buf));
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    if (sht30_crc8(buf, 2) != buf[2] || sht30_crc8(&buf[3], 2) != buf[5]) {
        printf("SHT30 CRC mismatch\r\n");
    }
    uint16_t traw = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    uint16_t hraw = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);
    *temp = -45.0f + (175.0f * (float)traw / 65535.0f);
    *humi = 100.0f * (float)hraw / 65535.0f;
    return ERRCODE_SUCC;
}
