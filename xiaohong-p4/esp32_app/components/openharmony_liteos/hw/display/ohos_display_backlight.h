#ifndef OHOS_DISPLAY_BACKLIGHT_H
#define OHOS_DISPLAY_BACKLIGHT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LCD 背光 LEDC PWM（GPIO20，低边有效）
 *
 * 防重入：已初始化时直接返回 true。
 * 开机与 UI 设置页都会调用，调用后由 OhosDisplayBacklightSetPercent() 设亮度。
 */
bool OhosDisplayBacklightInit(void);

/**
 * @brief 设置背光亮度 0-100（百分比）
 *
 * 内部自动初始化 LEDC，可安全地在 UI 事件回调中调用。
 * 值域外自动裁剪。
 */
bool OhosDisplayBacklightSetPercent(uint32_t percent);

/**
 * @brief 读取最近一次设置的亮度百分比
 */
uint32_t OhosDisplayBacklightGetPercent(void);

#ifdef __cplusplus
}
#endif

#endif /* OHOS_DISPLAY_BACKLIGHT_H */
