/* LVGL 9.2 native offscreen smoke test. Real LVGL renderer, mocked board only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "lvgl.h"
#include "app_hub.h"
#include "app_registry.h"
#include "app_installer.h"
#include "key_config.h"

static uint8_t frame[240 * 240 * 2];
static uint8_t render_buf[240 * 240 * 2];

/* Keep this test independent of board startup/network functions. */
static bool create_game(lv_obj_t *screen) {
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Snake started");
    return true;
}
static void destroy_game(void) {}
static void key_game(uint8_t key) { (void)key; }
static void tick_game(void) {}
static const xh_app_entry_t entries[] = {
    {.id=XH_APP_ID_SNAKE,.name="Snake",.icon="S",.available=true,.on_create=create_game,.on_destroy=destroy_game,.on_key=key_game,.on_tick=tick_game},
    {.id=XH_APP_ID_TETRIS,.name="Tetris",.icon="T",.available=false},
    {.id=XH_APP_ID_BREAKOUT,.name="Breakout",.icon="B",.available=false},
    {.id=XH_APP_ID_LIQUID_DUCK,.name="LiquidDuck",.icon="D",.available=false}
};
const xh_app_entry_t *xh_app_registry_get_all(uint8_t *count) {
    *count = sizeof(entries)/sizeof(entries[0]);
    return entries;
}
void xh_app_installer_init(void) {}

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px) {
    const int width = area->x2 - area->x1 + 1;
    const int height = area->y2 - area->y1 + 1;
    for(int y = 0;y < height;y++) {
        size_t dest = ((size_t)(area->y1 + y)*240 + (size_t)area->x1)*2;
        for(int x=0;x<width*2;x++) frame[dest+x] = px[(size_t)y*width*2+(size_t)x];
    }
    lv_display_flush_ready(display);
}

static void export_ppm(const char *filename) {
    FILE *out = fopen(filename,"wb");
    if(!out) { perror(filename);exit(2); }
    fprintf(out, "P6\n240 240\n255\n");
    for(size_t i=0;i<240*240;i++){
        uint16_t c=(uint16_t)frame[i*2]|((uint16_t)frame[i*2+1]<<8);
        uint8_t rgb[3]={(uint8_t)(((c>>11)&31)*255/31),
                        (uint8_t)(((c>>5)&63)*255/63),
                        (uint8_t)((c&31)*255/31)};
        assert(fwrite(rgb,1,3,out)==3);
    }
    fclose(out);
}
int main(int argc,char **argv) {
    const char *ppm = argc>1?argv[1]:"apphub-240x240.ppm";
    lv_init();
    lv_display_t *display = lv_display_create(240,240);
    assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display,flush_cb);
    lv_display_set_buffers(display,render_buf,NULL,sizeof(render_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_obj_t *ai = lv_scr_act();
    xh_app_hub_init();
    xh_app_hub_open();
    assert(xh_app_hub_is_active() && ai != lv_scr_act());
    lv_obj_update_layout(lv_scr_act());
    lv_refr_now(display);
    export_ppm(ppm);
    xh_app_hub_handle_key(eKey_VolDw_ShortPressed);
    lv_refr_now(display);
    xh_app_hub_handle_key(eKey_VolUp_ShortPressed);
    xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
    assert(xh_app_hub_is_active());
    xh_app_hub_handle_key(eKey_Wakeup_LongPressed);
    xh_app_hub_handle_key(eKey_Wakeup_LongPressed);
    assert(!xh_app_hub_is_active() && lv_scr_act()==ai);
    printf("PASS: real LVGL 9 rendered 240x240; navigation & screen roundtrip\n");
    return 0;
}
