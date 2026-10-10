#ifndef P4_SETTINGS_PAGE_H
#define P4_SETTINGS_PAGE_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*P4SettingsPageBackHandler)(void);

void P4SettingsPageSetBackHandler(P4SettingsPageBackHandler handler);
void P4SettingsPageCreate(lv_obj_t *parent);
void P4SettingsPageShow(void);
void P4SettingsPageHide(void);
void P4SettingsPageRefresh(void);
void P4SettingsPageMarkDirty(void);
void P4SettingsPageProcessPending(void);

#ifdef __cplusplus
}
#endif

#endif
