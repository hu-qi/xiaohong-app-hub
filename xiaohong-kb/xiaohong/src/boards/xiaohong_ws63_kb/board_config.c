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

    uapi_pin_set_mode(BUZZER_PIN, 1);    // 蜂鸣器（PWM 模式，用于低电量告警等提示音）
    uapi_pin_set_mode(LCD_BLK_PIN, 1);   // 显示屏背光
    uapi_pwm_init();

    // LEDs WS2812C x3
    uapi_pin_set_mode(LED_DI_PIN, 0);
    uapi_gpio_set_dir(LED_DI_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(LED_DI_PIN, GPIO_LEVEL_LOW);

    //2. SPI-1 管脚配置
    uapi_pin_set_mode(SPI_CLK_PIN, SPI_GPIO_PIN_MODE);
    uapi_pin_set_ds(SPI_CLK_PIN, PIN_DS_7);
    uapi_pin_set_mode(SPI_MOSI_PIN, SPI_GPIO_PIN_MODE);

    //3. I2C 管脚配置
    uapi_pin_set_mode(UART1_TXD_PIN, UART1_GPIO_PIN_MODE);
    uapi_pin_set_mode(UART1_RXD_PIN, UART1_GPIO_PIN_MODE);


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

    if (ret == ERRCODE_SUCC) {
        hw_inited = 1;
        printf("[BoardCfg] board_hw_init: hw_inited[0->1]\r\n");
    }

    return ret;
}

// 蜂鸣器短鸣：按 BUZZER_FREQ 频率输出方波，持续 beep_ms 毫秒
errcode_t buzzer_beep(uint32_t beep_ms)
{
    errcode_t ret = ERRCODE_FAIL;
    uint32_t clk_freq = uapi_pwm_get_frequency((uint8_t)BUZZER_PWM);
    uint32_t div_num = (clk_freq > 0U) ? (clk_freq / BUZZER_FREQ) : 0U;
    if (div_num < 2U) {
        printf("[BoardCfg] buzzer_beep: div_num[%u] too small\r\n", div_num);
        return ERRCODE_FAIL;
    }

    uint32_t high_time = div_num / 2U;          // 50% 占空比
    uint32_t low_time  = div_num - high_time;
    pwm_config_t cfg = {
        .low_time = low_time,
        .high_time = high_time,
        .offset_time = 0,
        .cycles = 0,
        .repeat = true
    };

    uapi_pwm_close(BUZZER_PWM);  // close PWM port before open it
    if (uapi_pwm_open((uint8_t)BUZZER_PWM, &cfg) != ERRCODE_SUCC) {
        printf("[BoardCfg] buzzer_beep: uapi_pwm_open NG\r\n");
        return ERRCODE_FAIL;
    }
    if (uapi_pwm_start((uint8_t)BUZZER_PWM) != ERRCODE_SUCC) {
        printf("[BoardCfg] buzzer_beep: uapi_pwm_start NG\r\n");
        uapi_pwm_close(BUZZER_PWM);
        return ERRCODE_FAIL;
    }

    osal_msleep(beep_ms);

    uapi_pwm_close(BUZZER_PWM);  // 关闭 PWM 输出，停止鸣响
    return ERRCODE_SUCC;
}

