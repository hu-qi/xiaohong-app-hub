#include "p4_activate_hint.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "p4_system_font.h"
#include "config_service.h"

static const char *TAG = "p4_activate";

static lv_obj_t *s_overlay;
static lv_obj_t *s_text_label;
static lv_obj_t *s_type_label;

#if 0
static char* P4ActivateHintGetOtaUrlOrigin(void)
{
    uint8_t len = 0;
    static char url[CONFIG_SERVICE_URL_MAX];

    ConfigServiceData cfg;
    ConfigServiceGet(&cfg);

    memset(url, '\0', CONFIG_SERVICE_URL_MAX);
    if (cfg.ota_url[0] == '\0') {
        (void)snprintf(url, CONFIG_SERVICE_URL_MAX, "%s", CONFIG_DEFAULT_OTA_URL);
    } else {
        (void)snprintf(url, CONFIG_SERVICE_URL_MAX, "%s", &cfg.ota_url[0]);
    }

    const char *p = url;
    if ((p = strstr(url, "://")) != NULL) {
        p += 3;
    } else {
        p = url;
    }
    const char *end = strstr(p, "/");
    len = end ? (uint8_t)(end - url) : strlen(url);
    url[len] = '\0';

    ESP_LOGI(TAG, "P4ActivateHintGetOtaUrlOrigin: url[%s]\r\n", url);
    return url;
}
#endif

static void P4ActivateCloseBtnClick(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    P4ActivateHintHide();
}

static lv_obj_t *P4ActivateCreateBtn(lv_obj_t *parent, const char *text,
                                     int x, int y, int w, int h,
                                     lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
    lv_obj_center(label);

    return btn;
}

/* 初始化创建浮层（仅执行一次） */
static void P4ActivateHintInit(void)
{
    if (s_overlay != NULL) {
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    if (scr == NULL) {
        return;
    }

    /* 半透明遮罩层 */
    s_overlay = lv_obj_create(scr);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, 140, LV_PART_MAIN); /* 半透明 */
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_overlay, 0, LV_PART_MAIN);
    lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    /* 中央卡片容器 */
    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, 400, 400);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_make(0xCC, 0xCC, 0xCC), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, 10, LV_PART_MAIN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 类型 / 标题 */
    s_type_label = lv_label_create(card);
    lv_obj_set_pos(s_type_label, 0, 17);
    lv_obj_set_size(s_type_label, 400, 28);
    lv_obj_set_style_text_align(s_type_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_type_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_type_label, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);
    lv_label_set_text(s_type_label, "激活信息");

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(card);
    lv_obj_set_pos(line, 0, 46);
    lv_obj_set_size(line, 400, 2);
    lv_obj_set_style_bg_color(line, lv_color_make(0xE0, 0xE0, 0xE0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);

    /* 激活码/消息内容 */
    s_text_label = lv_label_create(card);
    lv_obj_set_pos(s_text_label, 16, 54);
    lv_obj_set_size(s_text_label, 370, 250);
    lv_label_set_long_mode(s_text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_text_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_text_label, lv_color_make(0x47, 0x55, 0x69), LV_PART_MAIN);
    lv_label_set_text(s_text_label, "");

    /* 关闭按钮（水平居中：x=150, w=100 → 150+100+150=400）*/
    P4ActivateCreateBtn(card, "关闭", 150, 320, 100, 50, P4ActivateCloseBtnClick);

    ESP_LOGI(TAG, "activate hint overlay created");
}

void P4ActivateHintShow(uint32_t type, const char *hint)
{
    if (hint == NULL || hint[0] == '\0') {
        return;
    }

    P4ActivateHintInit();

    if (s_overlay == NULL || s_text_label == NULL || s_type_label == NULL) {
        return;
    }

    const char *type_str;
    switch (type) {
        case 1U:  type_str = "激活码";  break;
        case 2U:  type_str = "激活消息"; break;
        case 3U:  type_str = "激活挑战"; break;
        case 4U:  type_str = "激活成功"; break;
        default:  type_str = "激活信息"; break;
    }

    char title[64];
    (void)snprintf(title, sizeof(title), "%s", type_str);
    lv_label_set_text(s_type_label, title);

    if (type == 1U) {
        lv_label_set_text_fmt(s_text_label,
                            "1. 请登录主页并注册账号:\n"
                            "     %s\n"
                            "2. 添加智能体后，在设备管理页面绑定设备\n"
                            "    设备绑定码: %.8s\n"
                            "3. 绑定设备完成后，请重启设备\n",
//                          P4ActivateHintGetOtaUrlOrigin(),
                            XIAOHONG_SERVER_URL,
                            hint);
    } else if (type == 4U) {
        lv_label_set_text_fmt(s_text_label, "设备激活成功。\n%s", hint);
    } else {
        lv_label_set_text(s_text_label, hint);
    }

    /* 移到最前台并显示 */
    lv_obj_move_foreground(s_overlay);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "activate hint show type=%u hint=[%s]", type, hint);
}

void P4ActivateHintHide(void)
{
    if (s_overlay != NULL) {
        lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    s_overlay = NULL;
    s_text_label = NULL;
    s_type_label = NULL;
    ESP_LOGI(TAG, "activate hint hide");
}
