#ifndef P4_ACTIVATE_HINT_H
#define P4_ACTIVATE_HINT_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 显示激活码提示浮层
 * @param type  消息类型: 1=激活码, 2=激活消息, 3=激活挑战
 * @param hint  提示文本内容
 */
void P4ActivateHintShow(uint32_t type, const char *hint);

/**
 * @brief 隐藏激活码提示浮层
 */
void P4ActivateHintHide(void);

#ifdef __cplusplus
}
#endif

#endif
