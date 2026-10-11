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

#include "board_config.h"

static errcode_t gpio_init(void)
{
    //1. adc 电量检测
    uapi_adc_init(ADC_CLOCK_NONE);

    //2. SPI-1 管脚配置
    uapi_pin_set_mode(SPI_CLK_PIN, SPI_GPIO_PIN_MODE);
    uapi_pin_set_ds(SPI_CLK_PIN, PIN_DS_7);
    uapi_pin_set_mode(SPI_MOSI_PIN, SPI_GPIO_PIN_MODE);
    uapi_pin_set_mode(SPI_MISO_PIN, SPI_GPIO_PIN_MODE);

    //3. UART2 管脚配置
    uapi_pin_set_mode(UART2_TXD_PIN, UART2_GPIO_PIN_MODE);
    uapi_pin_set_mode(UART2_RXD_PIN, UART2_GPIO_PIN_MODE);


    //4. LCD CTRL 管脚配置
    uapi_pin_set_mode(LCD_RST_PIN, 0);
    uapi_gpio_set_dir(LCD_RST_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LCD_RST_PIN, GPIO_LEVEL_LOW);

    uapi_pin_set_mode(LCD_DC_PIN, 2);
    uapi_gpio_set_dir(LCD_DC_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LCD_DC_PIN, GPIO_LEVEL_LOW);

    //4. LCD CS 管脚配置
    uapi_pin_set_mode(LCD_CS_PIN, 4);
    uapi_gpio_set_dir(LCD_CS_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LCD_CS_PIN, GPIO_LEVEL_HIGH);

    //5. FLASH CS 管脚配置
    uapi_pin_set_mode(FLASH_CS_PIN, 0);
    uapi_gpio_set_dir(FLASH_CS_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(FLASH_CS_PIN, GPIO_LEVEL_HIGH);

    uapi_pin_set_mode(LCD_BLK_PIN, 1);
    uapi_pwm_init();

    return ERRCODE_SUCC;
}

static errcode_t spi1_init(void)
{
    errcode_t ret = ERRCODE_FAIL;
    static uint8_t spi1_inited = 0;
    if (spi1_inited) {
        printf("[BoardCfg] spi1_init:   spi1_inited[1]\r\n");
        return ERRCODE_SUCC;
    }

    spi_attr_t config       = {0};
    config.is_slave         = false;
    config.slave_num        = 1;
    config.bus_clk          = SPI_CLK_FREQ;          // 32000000 or 40000000;
    config.freq_mhz         = SPI_WORKING_FREQUENCY; // SPI working freq[xMHz, 100xMHz]
    config.clk_polarity     = 0;  // [0]SPI_CFG_CLK_CPOL_0/[1]SPI_CFG_CLK_CPOL_1
    config.clk_phase        = 0;  // [0]SPI_CFG_CLK_CPHA_0/[1]SPI_CFG_CLK_CPHA_1
    config.frame_format     = 0;  // [0]SPI_CFG_FRAME_FORMAT_MOTOROLA_SPI/[1]SPI_CFG_FRAME_FORMAT_TEXAS_SSP
    config.spi_frame_format = HAL_SPI_FRAME_FORMAT_STANDARD; // [0]HAL_SPI_FRAME_FORMAT_STANDARD: 标准的单线SPI帧格式
                                                             // [1]HAL_SPI_FRAME_FORMAT_DUAL: 双线SPI帧格式
                                                             // [2]HAL_SPI_FRAME_FORMAT_QUAD: 四线SPI帧格式
    config.frame_size       = HAL_SPI_FRAME_SIZE_8;          // [0x07]HAL_SPI_FRAME_SIZE_8: 8-位串行数据传输
                                                             // [0x1F]HAL_SPI_FRAME_SIZE_32
    config.tmod             = HAL_SPI_TRANS_MODE_TXRX;       // [0]HAL_SPI_TRANS_MODE_TXRX
                                                             // [1]HAL_SPI_TRANS_MODE_TX
                                                             // [2]HAL_SPI_TRANS_MODE_RX
    config.sste             = 0;                             // [0]SPI_CFG_SSTE_DISABLE/[1]SPI_CFG_SSTE_ENABLE

    if (SPI_TRAN_MODE == 1) {
        config.clk_polarity   = 0;  // [0]SPI_CFG_CLK_CPOL_0/[1]SPI_CFG_CLK_CPOL_1
        config.clk_phase      = 1;  // [0]SPI_CFG_CLK_CPHA_0/[1]SPI_CFG_CLK_CPHA_1
    } else if (SPI_TRAN_MODE == 2) {
        config.clk_polarity   = 1;  // [0]SPI_CFG_CLK_CPOL_0/[1]SPI_CFG_CLK_CPOL_1
        config.clk_phase      = 0;  // [0]SPI_CFG_CLK_CPHA_0/[1]SPI_CFG_CLK_CPHA_1
    } else if (SPI_TRAN_MODE == 3) {
        config.clk_polarity   = 1;  // [0]SPI_CFG_CLK_CPOL_0/[1]SPI_CFG_CLK_CPOL_1
        config.clk_phase      = 1;  // [0]SPI_CFG_CLK_CPHA_0/[1]SPI_CFG_CLK_CPHA_1
    }

    spi_extra_attr_t ext_config = { 0 };
    ext_config.qspi_param.wait_cycles = 0x10;

    ret = uapi_spi_init(SPI_IDX, &config, &ext_config);

    #if defined(CONFIG_SPI_SUPPORT_DMA) && (CONFIG_SPI_SUPPORT_DMA == 1)
    uapi_dma_init();
    uapi_dma_open();
    #endif

    if (ret == ERRCODE_SUCC) {
        printf("[BoardCfg] spi1_init:   spi1_inited[0->1]\r\n");
        spi1_inited = 1;
    } else {
        printf("[BoardCfg] spi1_init:   spi1_inited[0]\r\n");
    }
    return ret;
}

static uint8_t g_uart_rx_buff[UART_RX_BUFF_SIZE] = {0};
static uart_buffer_config_t g_uart_buffer_config = {
    .rx_buffer = g_uart_rx_buff,
    .rx_buffer_size = UART_RX_BUFF_SIZE
};
static errcode_t uart_init(void)
{
    static uint8_t uart2_inited = 0;
    if (uart2_inited) {
        printf("[BoardCfg] uart2_init: uart2_inited[1]\r\n");
        return ERRCODE_SUCC;
    }

    uart_attr_t attr = {
        .baud_rate = UART_BAUDRATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE
    };
    uart_pin_config_t pin_config = {
        .tx_pin = UART2_TXD_PIN,
        .rx_pin = UART2_RXD_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE
    };

    errcode_t ret = uapi_uart_init(UART_BUS_ID, &pin_config, &attr, NULL, &g_uart_buffer_config);
    if (ret == ERRCODE_SUCC) {
        printf("[BoardCfg] uart2_init: uart2_inited[0->1]\r\n");
        uart2_inited = 1;
    } else {
        printf("[BoardCfg] uart2_init: uart2_inited[0]\r\n");
    }
    return ret;
}

errcode_t board_hw_init(void)
{
    errcode_t ret = ERRCODE_SUCC;
    static uint8_t hw_inited = 0;
    if (hw_inited) {
        printf("[BoardCfg] board_hw_init: hw_inited[1]\r\n");
        return ERRCODE_SUCC;
    }

    ret |= gpio_init();
    ret |= key_init();
    ret |= spi1_init();
    ret |= uart_init();

    if (ret == ERRCODE_SUCC) {
        hw_inited = 1;
        printf("[BoardCfg] board_hw_init: hw_inited[0->1]\r\n");
    }

    return ret;
}

errcode_t spi_cs_pin_race(uint8_t pin, uint8_t level)
{
    if (level) {  // cs pin set Hi:unSelect
        return uapi_gpio_set_val(pin, level);
    } else {      // cs pin set Lo:Select
        if ((pin == LCD_CS_PIN) &&
            (GPIO_LEVEL_HIGH == uapi_gpio_get_val(FLASH_CS_PIN))) {
            return uapi_gpio_set_val(pin, level);
        }
        if ((pin == FLASH_CS_PIN) &&
            (GPIO_LEVEL_HIGH == uapi_gpio_get_val(LCD_CS_PIN))) {
            return uapi_gpio_set_val(pin, level);
        }
    }
    return ERRCODE_FAIL;
}

errcode_t flash_set_cs_pin(uint8_t level)
{
    uint16_t timeout = 0;
    errcode_t ret = spi_cs_pin_race(FLASH_CS_PIN, level);
    while (ERRCODE_SUCC != ret) {
        usleep(1000);  // wait 1ms
        timeout++;
        if (timeout >= SPI_CS_PIN_RACE_TIMEOUT) {  // timeout
            printf("flash_set_cs_pin: spi_cs_pin_race ==>> NG, timeout\r\n");
            return ERRCODE_FAIL;
        }
        ret = spi_cs_pin_race(FLASH_CS_PIN, level);
    }
    return ret;
}

/* HAL SPI timeout is loop-iteration count, not ms; avoid 0xFFFF false timeouts on TXRX RX drain. */
#define FLASH_SPI_XFER_TIMEOUT  (0xFFFFFFFFU)

errcode_t flash_write_read(uint8_t* write_buff, uint32_t write_len, uint8_t* read_buff, uint32_t read_len)
{
    errcode_t ret = ERRCODE_FAIL;
    if ((write_len > 0) && (write_buff == NULL)) {
        printf("[BoardCfg] flash_write_read: NG, write_buff invalid\r\n");
        return ERRCODE_FAIL;
    }
    if ((read_len > 0) && (read_buff == NULL)) {
        printf("[BoardCfg] flash_write_read: NG, read_buff invalid\r\n");
        return ERRCODE_FAIL;
    }
    if ((write_len == 0) && (read_len == 0)) {
        printf("[BoardCfg] flash_write_read: NG, len invalid\r\n");
        return ERRCODE_FAIL;
    }

    spi_xfer_data_t spi_data = {0};
    if (write_len > 0) {
        spi_data.tx_buff  = write_buff;
        spi_data.tx_bytes = write_len;
    }
    if (read_len > 0) {
        spi_data.rx_buff  = read_buff;
        spi_data.rx_bytes = read_len;
    }

    if (ERRCODE_SUCC != flash_set_cs_pin(GPIO_LEVEL_LOW)) {
        return ERRCODE_FAIL;
    }
    if ((write_len > 0) && (read_len > 0)) {
    #if 0  // sdkv106-OK  sdkv101-NG
        uapi_spi_set_tmod(SPI_IDX, HAL_SPI_TRANS_MODE_TXRX, 0);
        ret = uapi_spi_master_writeread(SPI_IDX, &spi_data, FLASH_SPI_XFER_TIMEOUT);
    #else  // sdkv106-OK  sdkv101-NG
        /*
         * uapi_spi_master_writeread() does hal_spi_write then hal_spi_read in TXRX mode.
         * hal_spi_read_txrx_mode_prepare() queues all rx_bytes dummy TX at once; combined with
         * hal_spi_v151_read() treating RFFE as error, this returns ERRCODE_SPI_RX_FIFO_FULL
         * (0x80001334) for typical NOR read sizes. SPI flash needs: CS low, send cmd+addr, then
         * clock data out — split into TX write + RX-only read while CS stays low.
         */
        spi_xfer_data_t tx_phase = {0};
        tx_phase.tx_buff = write_buff;
        tx_phase.tx_bytes = write_len;
        ret = uapi_spi_master_write(SPI_IDX, &tx_phase, FLASH_SPI_XFER_TIMEOUT);
        if (ret == ERRCODE_SUCC) {
            ret = uapi_spi_set_tmod(SPI_IDX, HAL_SPI_TRANS_MODE_RX, 0);
            if (ret == ERRCODE_SUCC) {
                spi_xfer_data_t rx_phase = {0};
                rx_phase.rx_buff = read_buff;
                rx_phase.rx_bytes = read_len;
                ret = uapi_spi_master_read(SPI_IDX, &rx_phase, FLASH_SPI_XFER_TIMEOUT);
            } else {
                printf("[BoardCfg] flash_write_read[W/R]: R/NG, ret[0x%X]\r\n", ret);
            }
            (void)uapi_spi_set_tmod(SPI_IDX, HAL_SPI_TRANS_MODE_TXRX, 0);
        } else {
            printf("[BoardCfg] flash_write_read[W/R]: W/NG, ret[0x%X]\r\n", ret);
        }
    #endif
    } else if ((write_len > 0) && (read_len == 0)) {
        /*
         * TXRX + hal_spi_write ends with hal_spi_clear_rx_fifo (one dummy RX per TX byte).
         * Long page programs (cmd+addr+data) often hit ERRCODE_SPI_TIMEOUT (0x80001333).
         * Transmit-only mode avoids that; restore TXRX for combined read / other devices.
         */
        ret = uapi_spi_set_tmod(SPI_IDX, HAL_SPI_TRANS_MODE_TX, 0);
        if (ret == ERRCODE_SUCC) {
            ret = uapi_spi_master_write(SPI_IDX, &spi_data, FLASH_SPI_XFER_TIMEOUT);
        } else {
            printf("[BoardCfg] flash_write_read[W/-]: W/NG, ret[0x%X]\r\n", ret);
        }
        (void)uapi_spi_set_tmod(SPI_IDX, HAL_SPI_TRANS_MODE_TXRX, 0);
    } else if ((read_len > 0) && (write_len == 0)) {
        ret = uapi_spi_master_read(SPI_IDX, &spi_data, FLASH_SPI_XFER_TIMEOUT);
        if (ret != ERRCODE_SUCC) {
            printf("[BoardCfg] flash_write_read[-/R]: R/NG, ret[0x%X]\r\n", ret);
        }
    } else {
        ret = ERRCODE_FAIL;
    }
    flash_set_cs_pin(GPIO_LEVEL_HIGH);

    if (ret != ERRCODE_SUCC) {
        printf("[BoardCfg] flash_write_read: NG, ret[0x%X]\r\n", ret);
    }
    return ret;
}

errcode_t flash_read_id(uint8_t* recv_buff)
{
    uint8_t send_buff[FLASH_ID_RCMD_LEN] = {0x4B, 0x00, 0x00, 0x00, 0x00};
    return flash_write_read(send_buff, FLASH_ID_RCMD_LEN, recv_buff, FLASH_ID_LEN);
}
