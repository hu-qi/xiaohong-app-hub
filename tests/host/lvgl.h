#ifndef XH_TEST_LVGL_H
#define XH_TEST_LVGL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
typedef struct lv_obj_t lv_obj_t;
struct lv_obj_t { lv_obj_t *parent; unsigned flags; char text[128]; int x,y,w,h; };
typedef struct { unsigned char r,g,b; } lv_color_t;
typedef struct { int unused; } lv_font_t;
typedef struct {
    struct { uint32_t magic; uint32_t cf; uint32_t w,h,stride; } header;
    uint32_t data_size;
    const uint8_t *data;
} lv_image_dsc_t;
#define LV_IMAGE_HEADER_MAGIC 0x19
#define LV_COLOR_FORMAT_RGB565 1
#define LV_OPA_COVER 255
#define LV_OPA_TRANSP 0
#define LV_TEXT_ALIGN_CENTER 1
#define LV_OBJ_FLAG_SCROLLABLE 1U
#define LV_OBJ_FLAG_HIDDEN 2U
extern const lv_font_t test_font;
#define LV_FONT_DEFAULT (&test_font)
lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
lv_obj_t *lv_image_create(lv_obj_t *parent);
lv_obj_t *lv_scr_act(void);
void lv_scr_load(lv_obj_t *);
void lv_obj_del(lv_obj_t *);
void lv_obj_set_size(lv_obj_t *,int,int);
void lv_obj_set_pos(lv_obj_t *,int,int);
void lv_obj_set_width(lv_obj_t *,int);
void lv_label_set_text(lv_obj_t *,const char *);
void lv_label_set_text_fmt(lv_obj_t *,const char *,...);
void lv_image_set_src(lv_obj_t *,const void *);
void lv_obj_add_flag(lv_obj_t *,unsigned);
void lv_obj_remove_flag(lv_obj_t *,unsigned);
void lv_obj_clear_flag(lv_obj_t *,unsigned);
#define STYLE(name,type) static inline void name(lv_obj_t *o,type value,int part) { (void)o;(void)value;(void)part; }
STYLE(lv_obj_set_style_bg_color,lv_color_t)
STYLE(lv_obj_set_style_text_color,lv_color_t)
STYLE(lv_obj_set_style_border_color,lv_color_t)
STYLE(lv_obj_set_style_bg_opa,int)
STYLE(lv_obj_set_style_text_align,int)
STYLE(lv_obj_set_style_border_width,int)
STYLE(lv_obj_set_style_radius,int)
STYLE(lv_obj_set_style_pad_all,int)
STYLE(lv_obj_set_style_text_font,const lv_font_t*)
static inline lv_color_t lv_color_make(uint8_t r,uint8_t g,uint8_t b) { return (lv_color_t){r,g,b}; }
#endif
