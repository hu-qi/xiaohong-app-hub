#ifndef XH_TEST_BOARD_CONFIG_H
#define XH_TEST_BOARD_CONFIG_H
#include <stdint.h>
#include <stdbool.h>
typedef uint32_t pin_t;
typedef int errcode_t;
#define ERRCODE_SUCC 0
#define KEY_VOLUP_PIN 13
#define KEY_VOLDW_PIN 12
#define KEY_WAKEUP_PIN 11
#define KEY_LONG_PRESS_INTERVAL 500U
#define KEY_LONG_PRESS_INTERVAL_5S 5000U
#define GPIO_DIRECTION_INPUT 0
#define GPIO_INTERRUPT_DEDGE 3
typedef uint32_t (*key_event_callback_t)(uint8_t event);
int uapi_gpio_get_val(pin_t pin);
uint32_t uapi_tcxo_get_ms(void);
errcode_t uapi_pin_set_mode(pin_t pin,int value);
errcode_t uapi_gpio_set_dir(pin_t pin,int dir);
errcode_t uapi_gpio_set_val(pin_t pin,int value);
errcode_t uapi_gpio_register_isr_func(pin_t pin,int trigger,
                                      void (*callback)(pin_t,uintptr_t));
#endif
