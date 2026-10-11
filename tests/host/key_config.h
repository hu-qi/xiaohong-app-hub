#ifndef XH_HOST_KEY_CONFIG_H
#define XH_HOST_KEY_CONFIG_H
#include "board_config.h"
enum {
 eKey_VolUp_ShortPressed=0,
 eKey_VolUp_LongPressed,
 eKey_VolDw_ShortPressed,
 eKey_VolDw_LongPressed,
 eKey_Wakeup_ShortPressed,
 eKey_Wakeup_LongPressed,
 eKey_Wakeup_LongPressed_5S,
 eKey_AppHub_Toggle,
 eKey_MaxCount
};
uint32_t key_init(void);
uint8_t register_key_event_callback(key_event_callback_t cb);
#endif
