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
#include <unistd.h>

#include "securec.h"

#include "board_config.h"
#include "w25q128.h"

#define W25QXX_BUSY_POLL_DELAY_US      (1000U)
#define W25QXX_BUSY_POLL_TIMEOUT_MS    (2000U)
#define W25QXX_READ_RETRY_TIMES        (3U)

static uint8_t g_w25qxx_sector_buffer[W25X_SECTOR_SIZE] = {0};

static errcode_t w25qxx_flash_write_read(const uint8_t *tx_buff, uint32_t tx_len,
                                         uint8_t *rx_buff, uint32_t rx_len)
{
    return flash_write_read((uint8_t *)tx_buff, tx_len, rx_buff, rx_len);
}

static errcode_t w25qxx_write_command(uint8_t cmd)
{
    return w25qxx_flash_write_read(&cmd, 1, NULL, 0);
}

static errcode_t w25qxx_write_address_command(uint8_t cmd, uint32_t addr)
{
    uint8_t tx_buff[4] = {
        cmd,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr >> 0),
    };

    return w25qxx_flash_write_read(tx_buff, sizeof(tx_buff), NULL, 0);
}

static errcode_t W25QXX_ReadSR(uint8_t *value)
{
    uint8_t cmd = W25X_ReadStatusReg1;

    if (value == NULL) {
        return ERRCODE_FAIL;
    }

    *value = 0xFF;
    return w25qxx_flash_write_read(&cmd, 1, value, 1);
}

static errcode_t W25QXX_Write_Page(const uint8_t *buff, uint32_t addr, uint16_t len)
{
    uint8_t tx_buff[W25X_PAGE_SIZE + 4];
    errcode_t ret;

    if ((buff == NULL) || (len == 0) || (len > W25X_PAGE_SIZE)) {
        return ERRCODE_FAIL;
    }

    ret = W25QXX_Wait_Busy();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = W25QXX_Write_Enable();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    tx_buff[0] = W25X_PageProgram;
    tx_buff[1] = (uint8_t)(addr >> 16);
    tx_buff[2] = (uint8_t)(addr >> 8);
    tx_buff[3] = (uint8_t)(addr >> 0);
    if (memcpy_s(&tx_buff[4], sizeof(tx_buff) - 4, buff, len) != EOK) {
        return ERRCODE_FAIL;
    }

    ret = w25qxx_flash_write_read(tx_buff, (uint32_t)len + 4U, NULL, 0);
    if (ret != ERRCODE_SUCC) {
        printf("W25QXX_Write_Page: page program failed, addr[0x%06X], len[%u]",
               (unsigned int)addr, (unsigned int)len);
        return ret;
    }

    return W25QXX_Wait_Busy();
}

static errcode_t W25QXX_Write_NoCheck(const uint8_t *buff, uint32_t addr, uint32_t len)
{
    uint16_t page_remain;
    errcode_t ret;

    if ((buff == NULL) || (len == 0)) {
        return ERRCODE_FAIL;
    }

    page_remain = (uint16_t)(W25X_PAGE_SIZE - (addr % W25X_PAGE_SIZE));
    if (len <= page_remain) {
        page_remain = (uint16_t)len;
    }

    while (1) {
        ret = W25QXX_Write_Page(buff, addr, page_remain);
        if (ret != ERRCODE_SUCC) {
            return ret;
        }

        if (len == page_remain) {
            break;
        }

        buff += page_remain;
        addr += page_remain;
        len -= page_remain;
        page_remain = (len > W25X_PAGE_SIZE) ? W25X_PAGE_SIZE : (uint16_t)len;
    }

    return ERRCODE_SUCC;
}

uint16_t W25QXX_ReadID(void)
{
    uint8_t tx_buff[4] = {
        W25X_ManufactDeviceID,
        W25X_BYTE_DUMMY,
        W25X_BYTE_DUMMY,
        W25X_BYTE_ZERO,
    };
    uint8_t rx_buff[2] = {0};

    if (w25qxx_flash_write_read(tx_buff, sizeof(tx_buff), rx_buff, sizeof(rx_buff)) != ERRCODE_SUCC) {
        printf("W25QXX_ReadID: flash_write_read failed\r\n");
        return 0;
    }

    printf("NorFlash: ReadID: mf/id[0x%02X%02X]\r\n", rx_buff[0], rx_buff[1]);
    return (uint16_t)((rx_buff[0] << 8) | rx_buff[1]);
}

errcode_t W25QXX_Init(void)
{
    uint16_t flash_id = W25QXX_ReadID();

    if (flash_id == 0U || flash_id == 0xFFFFU) {
        return ERRCODE_FAIL;
    }
    return ERRCODE_SUCC;
}

errcode_t W25QXX_Erase_Sector(uint32_t sector_idx)
{
    uint32_t addr = sector_idx * W25X_SECTOR_SIZE;
    errcode_t ret;

    ret = W25QXX_Wait_Busy();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = W25QXX_Write_Enable();
    if (ret != ERRCODE_SUCC) {
        return ret;
    }

    ret = w25qxx_write_address_command(W25X_SectorErase, addr);
    if (ret != ERRCODE_SUCC) {
        printf("W25QXX_Erase_Sector: erase failed, sector[%u]", (unsigned int)sector_idx);
        return ret;
    }

    return W25QXX_Wait_Busy();
}

errcode_t W25QXX_Read(uint8_t *buff, uint32_t addr, uint32_t len)
{
    uint8_t tx_buff[4] = {
        W25X_ReadData,
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >> 8),
        (uint8_t)(addr >> 0),
    };

    if ((buff == NULL) || (len == 0)) {
        return ERRCODE_FAIL;
    }

    errcode_t ret = ERRCODE_FAIL;
    for (uint32_t i = 0; i < W25QXX_READ_RETRY_TIMES; i++) {
        ret = w25qxx_flash_write_read(tx_buff, sizeof(tx_buff), buff, len);
        if (ret == ERRCODE_SUCC) {
            return ERRCODE_SUCC;
        }
        (void)W25QXX_Wait_Busy();
        usleep(200U);
    }

    printf("W25QXX_Read: read failed, addr[0x%06X], len[%u]\r\n",
           (unsigned int)addr, (unsigned int)len);
    return ret;
}

errcode_t W25QXX_Prog(const uint8_t *buff, uint32_t addr, uint32_t len)
{
    if ((buff == NULL) || (len == 0)) {
        return ERRCODE_FAIL;
    }

    return W25QXX_Write_NoCheck(buff, addr, len);
}

errcode_t W25QXX_Write(const uint8_t *buff, uint32_t addr, uint32_t len)
{
    uint32_t sector_idx;
    uint32_t sector_offset;
    uint32_t sector_remain;
    uint32_t i;
    errcode_t ret;

    if ((buff == NULL) || (len == 0)) {
        return ERRCODE_FAIL;
    }

    sector_idx = addr / W25X_SECTOR_SIZE;
    sector_offset = addr % W25X_SECTOR_SIZE;
    sector_remain = W25X_SECTOR_SIZE - sector_offset;

    if (len <= sector_remain) {
        sector_remain = len;
    }

    while (1) {
        ret = W25QXX_Read(g_w25qxx_sector_buffer, sector_idx * W25X_SECTOR_SIZE, W25X_SECTOR_SIZE);
        if (ret != ERRCODE_SUCC) {
            return ret;
        }

        for (i = 0; i < sector_remain; i++) {
            if (g_w25qxx_sector_buffer[sector_offset + i] != 0xFF) {
                break;
            }
        }

        if (i < sector_remain) {
            ret = W25QXX_Erase_Sector(sector_idx);
            if (ret != ERRCODE_SUCC) {
                return ret;
            }

            if (memcpy_s(&g_w25qxx_sector_buffer[sector_offset],
                         W25X_SECTOR_SIZE - sector_offset, buff, sector_remain) != EOK) {
                return ERRCODE_FAIL;
            }

            ret = W25QXX_Write_NoCheck(g_w25qxx_sector_buffer,
                                       sector_idx * W25X_SECTOR_SIZE, W25X_SECTOR_SIZE);
        } else {
            ret = W25QXX_Write_NoCheck(buff, addr, sector_remain);
        }

        if (ret != ERRCODE_SUCC) {
            return ret;
        }

        if (len == sector_remain) {
            break;
        }

        sector_idx++;
        sector_offset = 0;
        buff += sector_remain;
        addr += sector_remain;
        len -= sector_remain;
        sector_remain = (len > W25X_SECTOR_SIZE) ? W25X_SECTOR_SIZE : len;
    }

    return ERRCODE_SUCC;
}

errcode_t W25QXX_Wait_Busy(void)
{
    uint32_t timeout_ms = W25QXX_BUSY_POLL_TIMEOUT_MS;
    uint8_t status = 0xFF;
    errcode_t ret;

    while (1) {
        ret = W25QXX_ReadSR(&status);
        if (ret != ERRCODE_SUCC) {
            printf("W25QXX_Wait_Busy: read status failed\r\n");
            return ret;
        }

        if ((status & 0x01U) == 0U) {
            return ERRCODE_SUCC;
        }

        if (timeout_ms == 0U) {
            printf("W25QXX_Wait_Busy: timeout");
            return ERRCODE_FAIL;
        }

        usleep(W25QXX_BUSY_POLL_DELAY_US);
        timeout_ms--;
    }
}

errcode_t W25QXX_Write_Enable(void)
{
    errcode_t ret = w25qxx_write_command(W25X_WriteEnable);

    if (ret != ERRCODE_SUCC) {
        printf("W25QXX_Write_Enable: command failed\r\n");
    }
    return ret;
}

errcode_t W25QXX_Write_Disable(void)
{
    errcode_t ret = w25qxx_write_command(W25X_WriteDisable);

    if (ret != ERRCODE_SUCC) {
        printf("W25QXX_Write_Disable: command failed");
    }
    return ret;
}
