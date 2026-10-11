/* Compiles the actual key_config.c and simulates GPIO IRQs on the host. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "board_config.h"
#include "key_config.h"
static uint32_t now;
static int gpio[16];
static void (*handler[16])(pin_t,uintptr_t);
static uint8_t emitted[32];
static unsigned count;
int uapi_gpio_get_val(pin_t pin) { return gpio[pin]; }
uint32_t uapi_tcxo_get_ms(void) { return now; }
errcode_t uapi_pin_set_mode(pin_t pin,int val) {(void)pin;(void)val;return 0;}
errcode_t uapi_gpio_set_dir(pin_t pin,int val) {(void)pin;(void)val;return 0;}
errcode_t uapi_gpio_set_val(pin_t pin,int value) {gpio[pin]=value;return 0;}
errcode_t uapi_gpio_register_isr_func(pin_t pin,int trig,
                                      void (*callback)(pin_t,uintptr_t)) {
    assert(trig==GPIO_INTERRUPT_DEDGE);
    handler[pin]=callback;
    return 0;
}
static uint32_t callback(uint8_t evt) {
    assert(count<32);
    emitted[count++]=evt;
    return 0;
}
static void irq(pin_t pin,int down,uint32_t t) {
    now=t;
    gpio[pin]=down?0:1;
    assert(handler[pin]!=NULL);
    handler[pin](pin,0);
}
int main(void) {
    assert(register_key_event_callback(callback)==ERRCODE_SUCC);
    assert(key_init()==ERRCODE_SUCC);
    /* Solo minus long MUST still lower brightness, never open Hub. */
    irq(KEY_VOLDW_PIN,1,100);irq(KEY_VOLDW_PIN,0,700);
    assert(count==1 && emitted[0]==eKey_VolDw_LongPressed);
    irq(KEY_VOLUP_PIN,1,1000);irq(KEY_VOLUP_PIN,0,1550);
    assert(count==2 && emitted[1]==eKey_VolUp_LongPressed);
    /* Short press remains volume up/down. */
    irq(KEY_VOLDW_PIN,1,1800);irq(KEY_VOLDW_PIN,0,2000);
    assert(count==3 && emitted[2]==eKey_VolDw_ShortPressed);
    /* Dual long sends one distinct hub event and suppresses brightness. */
    irq(KEY_VOLUP_PIN,1,2300);irq(KEY_VOLDW_PIN,1,2320);
    irq(KEY_VOLUP_PIN,0,3350);irq(KEY_VOLDW_PIN,0,3400);
    assert(count==4 && emitted[3]==eKey_AppHub_Toggle);
    /* M events still emitted by independent logic. */
    irq(KEY_WAKEUP_PIN,1,4000);irq(KEY_WAKEUP_PIN,0,4200);
    assert(count==5 && emitted[4]==eKey_Wakeup_ShortPressed);
    irq(KEY_WAKEUP_PIN,1,5000);irq(KEY_WAKEUP_PIN,0,10200);
    assert(count==6 && emitted[5]==eKey_Wakeup_LongPressed_5S);
    puts("PASS: actual key driver GPIO ISR simulation, preserving brightness, M and 5s reset");
    return 0;
}
