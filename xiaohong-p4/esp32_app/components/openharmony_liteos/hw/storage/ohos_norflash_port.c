#include "ohos_norflash_port.h"

#include <string.h>

#include "esp_err.h"
#include "esp_flash.h"
#include "esp_attr.h"
#include "esp_rom_sys.h"
#include "hal/spi_flash_types.h"

#define OHOS_NORFLASH_UID_CMD_RDUID  0x4BU
#define OHOS_NORFLASH_UID_DUMMY_BITS 32U
#define OHOS_NORFLASH_UID_LEN        16U

static uint64_t OhosNorFlashLoadU64Be(const uint8_t *in)
{
    uint64_t value = 0;

    if (in == NULL) {
        return 0;
    }

    for (uint32_t i = 0; i < 8U; ++i) {
        value = (value << 8U) | in[i];
    }
    return value;
}

static esp_err_t IRAM_ATTR NOINLINE_ATTR OhosNorFlashReadUniqueId16(esp_flash_t *chip, uint8_t *out)
{
    esp_err_t err;
    uint32_t allZero = 1U;
    uint32_t allOnes = 1U;

    if (chip == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 与 IDF 内部 read_unique_id() 一致：先做 OS 级保护(关 cache/中断、取总线锁) */
    if (chip->os_func != NULL && chip->os_func->start != NULL) {
        err = chip->os_func->start(chip->os_func_data, 0);
        if (err != ESP_OK) {
            return err;
        }
    }

    chip->host->driver->dev_config(chip->host);

    spi_flash_trans_t transfer = {
        .command = OHOS_NORFLASH_UID_CMD_RDUID,
        .miso_len = OHOS_NORFLASH_UID_LEN,
        .miso_data = out,
        .dummy_bitlen = OHOS_NORFLASH_UID_DUMMY_BITS, /* RDUID 命令后跟 4 字节(32bit) dummy 时钟 */
    };
    err = chip->host->driver->common_command(chip->host, &transfer);

    if (chip->os_func != NULL && chip->os_func->end != NULL) {
        esp_err_t endErr = chip->os_func->end(chip->os_func_data);
        if (err == ESP_OK) {
            err = endErr;
        }
    }

    if (err != ESP_OK) {
        return err;
    }

    /* 全 0 或全 0xFF 视为芯片无响应/不支持，交由 JEDEC ID 回退 */
    for (uint32_t i = 0; i < OHOS_NORFLASH_UID_LEN; ++i) {
        if (out[i] != 0x00U) {
            allZero = 0U;
        }
        if (out[i] != 0xFFU) {
            allOnes = 0U;
        }
    }
    if (allZero || allOnes) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

static void OhosNorFlashStoreU64Be(uint8_t *out, uint64_t value)
{
    if (out == NULL) {
        return;
    }

    for (uint32_t i = 0; i < 8U; ++i) {
        out[i] = (uint8_t)((value >> ((7U - i) * 8U)) & 0xFFU);
    }
}

static void OhosNorFlashStoreJedecBe(uint8_t *out, uint32_t value)
{
    if (out == NULL) {
        return;
    }

    out[0] = (uint8_t)((value >> 16U) & 0xFFU);
    out[1] = (uint8_t)((value >> 8U) & 0xFFU);
    out[2] = (uint8_t)(value & 0xFFU);
}

const char *OhosNorFlashPortIdKindName(uint8_t kind)
{
    switch (kind) {
        case OHOS_NORFLASH_ID_KIND_UNIQUE64:
            return "UNIQUE64";
        case OHOS_NORFLASH_ID_KIND_UNIQUE128:
            return "UNIQUE128";
        case OHOS_NORFLASH_ID_KIND_JEDEC24:
            return "JEDEC24";
        case OHOS_NORFLASH_ID_KIND_NONE:
        default:
            return "NONE";
    }
}

uint32_t OhosNorFlashPortReadId(OhosNorFlashIdInfo *out)
{
    uint8_t uniqueId16[OHOS_NORFLASH_UID_LEN] = {0};
    uint32_t jedecId = 0;
    uint32_t flashSize = 0;
    esp_err_t uniqueRet;
    esp_err_t jedecRet;
    esp_err_t sizeRet;

    if (out == NULL) {
        return 1U;
    }

    (void)memset(out, 0, sizeof(*out));
    out->unique_ret = (int32_t)ESP_FAIL;
    out->jedec_ret = (int32_t)ESP_FAIL;
    out->size_ret = (int32_t)ESP_FAIL;

    uniqueRet = OhosNorFlashReadUniqueId16(esp_flash_default_chip, uniqueId16);
    out->unique_ret = (int32_t)uniqueRet;
    if (uniqueRet == ESP_OK) {
        (void)memcpy(out->bytes, uniqueId16, OHOS_NORFLASH_UID_LEN);
        out->unique_id = OhosNorFlashLoadU64Be(uniqueId16);
        out->kind = (uint8_t)OHOS_NORFLASH_ID_KIND_UNIQUE128;
        out->len = OHOS_NORFLASH_UID_LEN;
    }

    jedecRet = esp_flash_read_id(esp_flash_default_chip, &jedecId);
    out->jedec_ret = (int32_t)jedecRet;
    if (jedecRet == ESP_OK) {
        out->jedec_id = jedecId;
        if (out->len == 0U) {
            out->kind = (uint8_t)OHOS_NORFLASH_ID_KIND_JEDEC24;
            out->len = 3U;
            OhosNorFlashStoreJedecBe(out->bytes, jedecId);
        }
    }

    sizeRet = esp_flash_get_size(esp_flash_default_chip, &flashSize);
    out->size_ret = (int32_t)sizeRet;
    if (sizeRet == ESP_OK) {
        out->flash_size = flashSize;
    }

    esp_rom_printf("[P4-NORFLASH] read id kind=%s len=%u uniqueRet=%d unique=0x%08X%08X%08X%08X jedecRet=%d jedec=0x%06x sizeRet=%d size=%u\n",
                   OhosNorFlashPortIdKindName(out->kind),
                   out->len,
                   out->unique_ret,
                   (uint32_t)(((uint32_t)out->bytes[0] << 24U) | ((uint32_t)out->bytes[1] << 16U) |
                              ((uint32_t)out->bytes[2] << 8U) | out->bytes[3]),
                   (uint32_t)(((uint32_t)out->bytes[4] << 24U) | ((uint32_t)out->bytes[5] << 16U) |
                              ((uint32_t)out->bytes[6] << 8U) | out->bytes[7]),
                   (uint32_t)(((uint32_t)out->bytes[8] << 24U) | ((uint32_t)out->bytes[9] << 16U) |
                              ((uint32_t)out->bytes[10] << 8U) | out->bytes[11]),
                   (uint32_t)(((uint32_t)out->bytes[12] << 24U) | ((uint32_t)out->bytes[13] << 16U) |
                              ((uint32_t)out->bytes[14] << 8U) | out->bytes[15]),
                   out->jedec_ret,
                   out->jedec_id,
                   out->size_ret,
                   out->flash_size);

    return (out->len > 0U) ? 0U : 2U;
}
