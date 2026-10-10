#include "ohos_display_backlight.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "ohos_backlight";

/* 硬件背光控制脚（最终板：GPIO20 接背光驱动，低边有效） */
#define OHOS_BACKLIGHT_GPIO         (GPIO_NUM_20)
#define OHOS_BACKLIGHT_LEDC_TIMER   ((ledc_timer_t)3)
#define OHOS_BACKLIGHT_LEDC_CHANNEL ((ledc_channel_t)3)
#define OHOS_BACKLIGHT_PWM_FREQ_HZ  (24000U)
#define OHOS_BACKLIGHT_DUTY_RES     (LEDC_TIMER_10_BIT)
#define OHOS_BACKLIGHT_DUTY_MAX     (1023U)

static volatile bool s_backlight_inited = false;
static volatile uint32_t s_backlight_percent = 100U;

bool OhosDisplayBacklightInit(void)
{
    if (s_backlight_inited) {
        return true;
    }

    const ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = OHOS_BACKLIGHT_DUTY_RES,
        .timer_num = OHOS_BACKLIGHT_LEDC_TIMER,
        .freq_hz = OHOS_BACKLIGHT_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t chan_cfg = {
        .gpio_num = OHOS_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = OHOS_BACKLIGHT_LEDC_CHANNEL,
        .timer_sel = OHOS_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
        .flags = { .output_invert = 0 },
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = ledc_channel_config(&chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(ret));
        return false;
    }

    s_backlight_inited = true;
    ESP_LOGI(TAG, "backlight ledc ready gpio=%d timer=%d ch=%d freq=%uHz",
             (int)OHOS_BACKLIGHT_GPIO,
             (int)OHOS_BACKLIGHT_LEDC_TIMER,
             (int)OHOS_BACKLIGHT_LEDC_CHANNEL,
             (unsigned)OHOS_BACKLIGHT_PWM_FREQ_HZ);
    return true;
}

bool OhosDisplayBacklightSetPercent(uint32_t percent)
{
    if (percent > 100U) {
        percent = 100U;
    }

    if (!s_backlight_inited && !OhosDisplayBacklightInit()) {
        return false;
    }

    uint32_t duty = (OHOS_BACKLIGHT_DUTY_MAX * percent) / 100U;
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, OHOS_BACKLIGHT_LEDC_CHANNEL, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, OHOS_BACKLIGHT_LEDC_CHANNEL);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set backlight %u%% failed: %s", (unsigned)percent, esp_err_to_name(ret));
        return false;
    }

    s_backlight_percent = percent;
    ESP_LOGI(TAG, "backlight set to %u%%", (unsigned)percent);
    return true;
}

uint32_t OhosDisplayBacklightGetPercent(void)
{
    return s_backlight_percent;
}
