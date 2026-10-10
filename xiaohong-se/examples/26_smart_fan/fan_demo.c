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

#define LOG_TAG "[fan_demo]"

static void button_key_event_cb(void)
{
    uint8_t key = uapi_gpio_get_val(FAN_BUTTON_PIN);
    if (key == 0) {  // Press
        printf("%s: button key Press, toggle Fan On\r\n", LOG_TAG);
        uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_HIGH);  // Lo:Off, Hi:On
    } else {
        printf("%s: button key Release, toggle Fan Off\r\n", LOG_TAG);
        uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_LOW);  // Lo:Off, Hi:On
        uapi_gpio_set_val(FAN_BUTTON_PIN, 1);
    }
}

static void button_init(void)
{
    uapi_pin_set_mode(FAN_BUTTON_PIN, FAN_BUTTON_MODE);
    uapi_gpio_set_dir(FAN_BUTTON_PIN, GPIO_DIRECTION_INPUT);
    uapi_gpio_set_val(FAN_BUTTON_PIN, 1);
    uapi_gpio_register_isr_func(FAN_BUTTON_PIN, GPIO_INTERRUPT_DEDGE, button_key_event_cb);
}

static void fan_gpio_init(void)
{
    uapi_pin_set_mode(FAN_GPIO_PIN, FAN_GPIO_MODE);
    uapi_pin_set_ds(FAN_GPIO_PIN, PIN_DS_7);
    uapi_gpio_set_dir(FAN_GPIO_PIN, GPIO_DIRECTION_OUTPUT);
    uapi_gpio_set_val(FAN_GPIO_PIN, GPIO_LEVEL_LOW);  // Lo:Off, Hi:On
}

void FanTask(void)
{
    fan_gpio_init();
    button_init();

    while (1) {
        printf("%s: FanTask is running ...\r\n", LOG_TAG);
        sleep(1);
    }
}

static void FanTaskEntry(void)
{
    osThreadAttr_t attr = {0};
    attr.name = "FanTask";
    attr.stack_size = 1024*4;
    attr.priority = 25;
    if (osThreadNew(FanTask, NULL, &attr) == NULL) {
        printf("%s FanTaskEntry creat FanTask NG\r\n", LOG_TAG);
    } else {
        printf("%s FanTaskEntry creat FanTask OK\r\n", LOG_TAG);
    }
}
SYS_RUN(FanTaskEntry);