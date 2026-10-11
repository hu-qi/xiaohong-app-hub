/* Native host test: checks REAL App Hub C state transitions with a fake LVGL
 * backend; never claims to be a WS63 build or an LVGL screenshot. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "app_hub.h"
#include "app_registry.h"
#include "app_installer.h"
#include "key_config.h"

#define COUNT 500
static lv_obj_t *objects[COUNT];
static lv_obj_t *active;
static unsigned live,created,destroyed,ticks,key_count;
static bool fail_app;
const lv_font_t test_font = {0};
static bool on_create(lv_obj_t *screen) {
    assert(screen != NULL);
    created++;
    return !fail_app;
}
static void on_destroy(void) { destroyed++; }
static void on_key(uint8_t key) { assert(key<XH_APP_KEY_MAX); key_count++; }
static void on_tick(void) { ticks++; }
static const xh_app_entry_t apps[] = {
    {.id=XH_APP_ID_SNAKE,.name="Snake",.icon="S",.available=true,.on_create=on_create,.on_destroy=on_destroy,.on_key=on_key,.on_tick=on_tick},
    {.id=XH_APP_ID_TETRIS,.name="Tetris",.icon="T",.available=false},
    {.id=XH_APP_ID_BREAKOUT,.name="Breakout",.icon="B",.available=false},
    {.id=XH_APP_ID_LIQUID_DUCK,.name="LiquidDuck",.icon="D",.available=false}
};
const xh_app_entry_t *xh_app_registry_get_all(uint8_t *count) { *count=4;return apps; }
static unsigned install_init;
void xh_app_installer_init(void) { install_init++; }
static lv_obj_t *alloc_obj(lv_obj_t *parent) {
    lv_obj_t *o=calloc(1,sizeof(*o));
    assert(o);
    o->parent=parent;
    for(unsigned i=0;i<COUNT;i++) if(!objects[i]) {objects[i]=o;live++;return o;}
    abort();
}
lv_obj_t *lv_obj_create(lv_obj_t *parent) {return alloc_obj(parent);}
lv_obj_t *lv_label_create(lv_obj_t *parent) {return alloc_obj(parent);}
lv_obj_t *lv_image_create(lv_obj_t *parent) {return alloc_obj(parent);}
lv_obj_t *lv_scr_act(void) {return active;}
void lv_scr_load(lv_obj_t *o) {assert(o);active=o;}
void lv_obj_del(lv_obj_t *obj) {
    assert(obj && obj!=active); /* catches freeing active LVGL screen */
    for(unsigned i=0;i<COUNT;i++) if(objects[i] && objects[i]->parent==obj) lv_obj_del(objects[i]);
    for(unsigned i=0;i<COUNT;i++) if(objects[i]==obj){objects[i]=NULL;live--;free(obj);return;}
    abort();
}
void lv_obj_set_size(lv_obj_t *o,int w,int h) {o->w=w;o->h=h;}
void lv_obj_set_pos(lv_obj_t *o,int x,int y) {o->x=x;o->y=y;}
void lv_obj_set_width(lv_obj_t *o,int w) {o->w=w;}
void lv_label_set_text(lv_obj_t *o,const char *s) {
    assert(o && s);
    snprintf(o->text,sizeof(o->text),"%s",s);
}
void lv_label_set_text_fmt(lv_obj_t *o,const char *fmt,...) {
    va_list ap;va_start(ap,fmt);
    vsnprintf(o->text,sizeof(o->text),fmt,ap);
    va_end(ap);
}
void lv_image_set_src(lv_obj_t *o,const void *src) {assert(o && src);}
void lv_obj_add_flag(lv_obj_t *o,unsigned flags) {o->flags|=flags;}
void lv_obj_remove_flag(lv_obj_t *o,unsigned flags) {o->flags&=~flags;}
void lv_obj_clear_flag(lv_obj_t *o,unsigned flags) {o->flags&=~flags;}
static bool has_visible(const char *text) {
    for(unsigned i=0;i<COUNT;i++)if(objects[i]&&strcmp(objects[i]->text,text)==0 &&
       !(objects[i]->flags&LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}
int main(void) {
    lv_obj_t *ai=lv_obj_create(NULL);lv_scr_load(ai);
    xh_app_hub_init();
    assert(install_init==1 && !xh_app_hub_is_active());
    for(unsigned round=0;round<60;round++) {
        xh_app_hub_open();
        assert(xh_app_hub_is_active() && lv_scr_act()!=ai);
        assert(has_visible("Snake") && has_visible("Installed"));
        xh_app_hub_handle_key(eKey_VolDw_ShortPressed);
        assert(has_visible("Tetris") && has_visible("Coming soon"));
        lv_obj_t *hub=lv_scr_act();
        xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
        assert(lv_scr_act()==hub); /* cannot launch disabled apps */
        xh_app_hub_handle_key(eKey_VolUp_ShortPressed);
        assert(has_visible("Snake"));
        if(round==0) {
            fail_app=true;
            xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
            assert(lv_scr_act()==hub && xh_app_hub_is_active());
            fail_app=false;
        }
        xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
        assert(lv_scr_act()!=hub);
        xh_app_hub_tick();
        xh_app_hub_handle_key(eKey_Wakeup_ShortPressed);
        xh_app_hub_handle_key(eKey_Wakeup_LongPressed);
        assert(lv_scr_act()==hub);
        xh_app_hub_handle_key(eKey_Wakeup_LongPressed);
        assert(lv_scr_act()==ai && !xh_app_hub_is_active());
        assert(live==1);
    }
    assert(created==61 && destroyed==60 && ticks==60 && key_count==120);
    printf("PASS: 60 full cycles, disabled app, failed launch, keys, ticks, LVGL screen lifetime\n");
    return 0;
}
