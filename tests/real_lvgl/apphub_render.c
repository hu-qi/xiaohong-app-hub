/* LVGL 9.2 native offscreen smoke test. Real LVGL renderer, mocked board only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "app_hub.h"
#include "app_registry.h"
#include "app_installer.h"
#include "key_config.h"

static uint8_t frame[240 * 240 * 2];
static uint8_t render_buf[240 * 240 * 2];

/* Real LVGL 9 integration test: link actual registry and all four games. */
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
/* Keep all image outputs next to the historical Snake snapshot path. */
static void export_named(const char *reference,const char *kind,unsigned id) {
    char path[256];
    const char *slash=strrchr(reference,'/');
    if(slash) {
        int n=(int)(slash-reference);
        assert(n>=0 && n<160);
        snprintf(path,sizeof(path),"%.*s/%s-%u.ppm",n,reference,kind,id);
    } else {
        snprintf(path,sizeof(path),"%s-%u.ppm",kind,id);
    }
    export_ppm(path);
}
static unsigned icon_colors(void) {
    /* Inner tile area has at least background + solid sprite color.
     * RGB565 is compared directly in the true upstream LVGL framebuffer. */
    uint16_t first=0;bool found=false;
    unsigned distinct=0;
    for(int y=54;y<113;y++)for(int x=89;x<151;x++) {
        size_t offset=((size_t)y*240U+(size_t)x)*2;
        uint16_t pixel=(uint16_t)frame[offset]|((uint16_t)frame[offset+1]<<8);
        if(!found) {first=pixel;found=true;distinct=1;}
        else if(pixel!=first) {distinct=2;break;}
    }
    return distinct;
}
int main(int argc,char **argv) {
    const char *ppm = argc>1?argv[1]:"apphub-240x240.ppm";
    lv_init();
    lv_display_t *display=lv_display_create(240,240);
    assert(display);
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display,flush_cb);
    lv_display_set_buffers(display,render_buf,NULL,sizeof(render_buf),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_obj_t *ai=lv_scr_act();
    xh_app_hub_init();
    xh_app_hub_open();
    assert(xh_app_hub_is_active() && ai!=lv_scr_act());
    uint8_t count=0;
    const xh_app_entry_t *registry=xh_app_registry_get_all(&count);
    assert(count==5 && registry[0].system_app);
    for(unsigned id=0;id<count;id++) {
        fprintf(stderr,"[LVGL] app id=%u before icon render\n",id);
        if(id>0)xh_app_hub_handle_key(eKey_VolUp_ShortPressed);
        lv_obj_update_layout(lv_scr_act());lv_refr_now(display);
        assert(icon_colors()>=2 && "Icon must render above tile, not be blank");
        export_named(ppm,"icon",id);
        if(id==1)export_ppm(ppm); /* historical artifact is the Snake Hub tile */
        if(id==0)continue; /* AI Assistant is a resident screen, not a game. */

        lv_obj_t *hub=lv_scr_act();
        fprintf(stderr,"[LVGL] app id=%u before create\n",id);
        xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
        fprintf(stderr,"[LVGL] app id=%u created\n",id);
        assert(lv_scr_act()!=hub && "every game in registry must start");
        /* Each game starts/rotates with M and moves using +/-.
         * No 240px framebuffer inside firmware; this is native host LVGL. */
        xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
        for(unsigned tick=0;tick<360;tick++) {
            if(tick%13==0)xh_app_hub_handle_key(eKey_VolUp_ShortPressed);
            if(tick%19==0)xh_app_hub_handle_key(eKey_VolDw_ShortPressed);
            xh_app_hub_tick();
            if(tick%30==0)lv_refr_now(display);
        }
        lv_obj_update_layout(lv_scr_act());lv_refr_now(display);
        export_named(ppm,"game",id);
        fprintf(stderr,"[LVGL] app id=%u after 360 ticks\n",id);
        xh_app_hub_handle_key(eKey_Wakeup_LongPressed);
        fprintf(stderr,"[LVGL] app id=%u exited\n",id);
        assert(lv_scr_act()==hub && xh_app_hub_is_active());
    }
    xh_app_hub_handle_key(eKey_VolUp_ShortPressed); /* wrap Duck -> AI */
    xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
    assert(!xh_app_hub_is_active() && lv_scr_act()==ai);
    printf("PASS: 5 native LVGL shape icons visible, 4 games launch/tick/exit, 240x240 render\n");
    return 0;
}
