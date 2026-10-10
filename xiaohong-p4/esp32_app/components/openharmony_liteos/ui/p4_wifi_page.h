#ifndef P4_WIFI_PAGE_H
#define P4_WIFI_PAGE_H

#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef void (*P4WiFiPageBackHandler)(void);

void P4WiFiPageSetBackHandler(P4WiFiPageBackHandler handler);
void P4WiFiPageCreate(lv_obj_t *parent);
void P4WiFiPageShow(void);
void P4WiFiPageHide(void);
void P4WiFiPageRefresh(void);
void P4WiFiPageMarkDirty(void);
void P4WiFiPageProcessPending(void);
void P4WiFiScanBtnStyle(uint8_t stat);

#ifdef __cplusplus
}
#endif

#endif
