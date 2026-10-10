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

#ifndef SHT30_SENSOR_SHT30_H
#define SHT30_SENSOR_SHT30_H

#include <stdint.h>
#include "errcode.h"
#include "i2c.h"

#ifndef CONFIG_I2C_MASTER_BUS_ID
#define CONFIG_I2C_MASTER_BUS_ID      1
#endif
#ifndef CONFIG_SHT30_I2C_ADDR_7B
#define CONFIG_SHT30_I2C_ADDR_7B      0x44
#endif

#define SHT30_CMD_SINGLE_SHOT_HIGH_H  0x24U
#define SHT30_CMD_SINGLE_SHOT_HIGH_L  0x00U
#define SHT30_CMD_SOFT_RST_H          0x30U
#define SHT30_CMD_SOFT_RST_L          0xA2U
#define SHT30_MEAS_DELAY_MS           16U

void sht30_set_i2c_bus(i2c_bus_t bus);
i2c_bus_t sht30_get_i2c_bus(void);

errcode_t SHT30_Calibrate(void);
errcode_t SHT30_StartMeasure(void);
errcode_t SHT30_GetMeasureResult(float *temp, float *humi);

#endif
