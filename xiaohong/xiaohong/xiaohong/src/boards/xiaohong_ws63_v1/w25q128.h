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

#ifndef W25Q128_H
#define W25Q128_H

#include <stdint.h>

#include "errcode.h"

#define W25X_P0_ADDR               (0x00000000U)
#define W25X_P0_SIZE               (0x01000000U)

#define W25X_PAGE_SIZE             (256U)
#define W25X_SECTOR_SIZE           (4096U)
#define W25X_BLOCK_SIZE            (16U)
#define W25X_SECTOR_COUNT          (256U * 16U)

#define W25X_BYTE_DUMMY            0x00
#define W25X_BYTE_ZERO             0x00

#define W25X_WriteEnable           0x06
#define W25X_VSRWriteEnable        0x50
#define W25X_WriteDisable          0x04
#define W25X_ReleasePowerDown      0xAB
#define W25X_ReadDeviceID          0xAB
#define W25X_ManufactDeviceID      0x90
#define W25X_JedecDeviceID         0x9F
#define W25X_ReadUniqueID          0x4B
#define W25X_ReadData              0x03
#define W25X_FastReadData          0x0B
#define W25X_PageProgram           0x02
#define W25X_SectorErase           0x20
#define W25X_BlockErase32KB        0x52
#define W25X_BlockErase64KB        0xD8
#define W25X_ChipErase             0xC7
#define W25X_ReadStatusReg1        0x05
#define W25X_ReadStatusReg2        0x35
#define W25X_ReadStatusReg3        0x15
#define W25X_WriteStatusReg1       0x01
#define W25X_WriteStatusReg2       0x31
#define W25X_WriteStatusReg3       0x11

#define W25X_FastReadDual          0x3B
#define W25X_PowerDown             0xB9

uint16_t W25QXX_ReadID(void);
errcode_t W25QXX_Init(void);

errcode_t W25QXX_Write_Enable(void);
errcode_t W25QXX_Write_Disable(void);
errcode_t W25QXX_Wait_Busy(void);
errcode_t W25QXX_Erase_Sector(uint32_t sector_idx);

errcode_t W25QXX_Read(uint8_t *buff, uint32_t addr, uint32_t len);
errcode_t W25QXX_Prog(const uint8_t *buff, uint32_t addr, uint32_t len);
errcode_t W25QXX_Write(const uint8_t *buff, uint32_t addr, uint32_t len);

#endif  /* W25Q128_H */
