#include "p4_sensor_page.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "ohos_uart_link_ui.h"
#include "p4_status_bar.h"
#include "p4_system_font.h"
#include "p4_ui_model.h"

static const char *TAG = "p4_sensor";

typedef struct {
    const char *name;
    const char *state;
} P4SensorReserveItem;

static const P4SensorReserveItem s_reserved_items[] = {
    {"智能插座", "预留"},
    {"智能风扇", "预留"},
    {"声光报警", "预留"},
    {"智能灯光", "预留"},
    {"烟雾传感器", "预留"},
    {"光照传感器", "预留"},
    {"人在传感", "预留"},
};

static P4SensorPageBackHandler s_back_handler;
static lv_obj_t *s_status_label;
static lv_obj_t *s_temp_value_label;
static lv_obj_t *s_humi_value_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_count_label;
static lv_obj_t *s_recent_label;
static uint32_t s_visible;
static uint32_t s_last_valid = 0xFFFFFFFFU;
static uint32_t s_last_rx_count = 0xFFFFFFFFU;
static uint32_t s_last_invalid_count = 0xFFFFFFFFU;
static int32_t s_last_temp100 = INT32_MIN;
static int32_t s_last_humi100 = INT32_MIN;

static char s_temp_cache[32];
static char s_humi_cache[32];
static char s_state_cache[64];
static char s_count_cache[64];
static char s_recent_cache[64];

static lv_obj_t *P4SensorCreateRect(lv_obj_t *parent,
                                    int x,
                                    int y,
                                    int w,
                                    int h,
                                    lv_color_t color,
                                    int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);

    return obj;
}

static lv_obj_t *P4SensorCreateLabel(lv_obj_t *parent,
                                     const char *text,
                                     int x,
                                     int y,
                                     int w,
                                     int h,
                                     lv_color_t color,
                                     lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_label_set_text(label, text);

    return label;
}

static void P4SensorSetText(lv_obj_t *label, char *cache, size_t cacheLen, const char *text)
{
    if (label == NULL || cache == NULL || cacheLen == 0U || text == NULL || !lv_obj_is_valid(label)) {
        return;
    }

    if (strncmp(cache, text, cacheLen) == 0) {
        return;
    }

    (void)snprintf(cache, cacheLen, "%s", text);
    lv_label_set_text(label, cache);
}

static void P4SensorFormatFixed100(char *buf, size_t len, int32_t value100, const char *unit)
{
    const char *sign = "";

    if (buf == NULL || len == 0U) {
        return;
    }

    if (value100 < 0) {
        sign = "-";
        value100 = -value100;
    }

    (void)snprintf(buf,
                   len,
                   "%s%d.%02d %s",
                   sign,
                   (int)(value100 / 100),
                   (int)(value100 % 100),
                   unit ? unit : "");
}

static void P4SensorBackEventCb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    P4SensorPageHide();
    if (s_back_handler != NULL) {
        s_back_handler();
    }
}

static void P4SensorCreateThermoIcon(lv_obj_t *parent)
{
    lv_color_t green = lv_color_make(0x10, 0xB9, 0x81);
    lv_color_t blue = lv_color_make(0x38, 0xB2, 0xDF);
    lv_obj_t *tube = P4SensorCreateRect(parent, 36, 16, 24, 88, green, 12);
    lv_obj_t *bulb = P4SensorCreateRect(parent, 25, 90, 46, 38, green, LV_RADIUS_CIRCLE);

    P4SensorCreateRect(tube, 8, 14, 8, 64, lv_color_white(), LV_RADIUS_CIRCLE);
    P4SensorCreateRect(parent, 80, 28, 32, 32, blue, LV_RADIUS_CIRCLE);
    P4SensorCreateRect(parent, 91, 72, 18, 18, blue, LV_RADIUS_CIRCLE);
    (void)bulb;
}

static void P4SensorCreateReservedGrid(lv_obj_t *parent)
{
    const int card_w = 204;
    const int card_h = 50;
    const int start_x = 24;
    const int start_y = 430;
    const int gap_x = 24;
    const int gap_y = 12;

    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_reserved_items) / sizeof(s_reserved_items[0])); ++i) {
        int col = (int)(i % 2U);
        int row = (int)(i / 2U);
        int x = start_x + col * (card_w + gap_x);
        int y = start_y + row * (card_h + gap_y);
        lv_obj_t *card = lv_obj_create(parent);

        lv_obj_set_pos(card, x, y);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_style_radius(card, 6, LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_make(0xD9, 0xE2, 0xEC), LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
        lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        P4SensorCreateLabel(card,
                            s_reserved_items[i].name,
                            12,
                            9,
                            120,
                            28,
                            lv_color_make(0x1F, 0x29, 0x37),
                            LV_TEXT_ALIGN_LEFT);
        P4SensorCreateLabel(card,
                            s_reserved_items[i].state,
                            138,
                            9,
                            50,
                            28,
                            lv_color_make(0x64, 0x74, 0x8B),
                            LV_TEXT_ALIGN_RIGHT);
    }
}

void P4SensorPageSetBackHandler(P4SensorPageBackHandler handler)
{
    s_back_handler = handler;
}

void P4SensorPageHide(void)
{
    s_visible = 0U;
    s_status_label = NULL;
    s_temp_value_label = NULL;
    s_humi_value_label = NULL;
    s_state_label = NULL;
    s_count_label = NULL;
    s_recent_label = NULL;
}

void P4SensorPageRefresh(void)
{
    P4UiModelSnapshot model;
    char text[64];

    if (!s_visible || s_temp_value_label == NULL || !lv_obj_is_valid(s_temp_value_label)) {
        return;
    }

    P4UiModelGetSnapshot(&model);
    if (s_status_label != NULL && lv_obj_is_valid(s_status_label)) {
        P4StatusBarUpdate(s_status_label, &model);
    }

    if (model.sensor_th.valid) {
        P4SensorFormatFixed100(text, sizeof(text), model.sensor_th.temp100, "C");
        P4SensorSetText(s_temp_value_label, s_temp_cache, sizeof(s_temp_cache), text);
        P4SensorFormatFixed100(text, sizeof(text), model.sensor_th.humi100, "%");
        P4SensorSetText(s_humi_value_label, s_humi_cache, sizeof(s_humi_cache), text);
        P4SensorSetText(s_state_label, s_state_cache, sizeof(s_state_cache), "状态：已更新");
    } else {
        P4SensorSetText(s_temp_value_label, s_temp_cache, sizeof(s_temp_cache), "--.-- C");
        P4SensorSetText(s_humi_value_label, s_humi_cache, sizeof(s_humi_cache), "--.-- %");
        P4SensorSetText(s_state_label, s_state_cache, sizeof(s_state_cache), "状态：等待上报");
    }

    (void)snprintf(text, sizeof(text), "上报次数：%u", (unsigned)model.sensor_th.rx_count);
    P4SensorSetText(s_count_label, s_count_cache, sizeof(s_count_cache), text);
    (void)snprintf(text, sizeof(text), "最近上报：%u ms", (unsigned)model.sensor_th.last_ms);
    P4SensorSetText(s_recent_label, s_recent_cache, sizeof(s_recent_cache), text);

    s_last_valid = model.sensor_th.valid;
    s_last_rx_count = model.sensor_th.rx_count;
    s_last_invalid_count = model.sensor_th.invalid_count;
    s_last_temp100 = model.sensor_th.temp100;
    s_last_humi100 = model.sensor_th.humi100;
}

void P4SensorPageShow(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *title;
    lv_obj_t *summary;
    lv_obj_t *back;
    lv_obj_t *back_label;

    if (scr == NULL) {
        return;
    }

    P4SensorPageHide();
    lv_obj_clean(scr);
    P4SystemFontApply(scr);
    lv_obj_set_style_bg_color(scr, lv_color_make(0xF1, 0xF5, 0xF9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    s_status_label = P4StatusBarCreate(scr);
    title = P4SensorCreateLabel(scr,
                                "传感器数据",
                                24,
                                58,
                                220,
                                34,
                                lv_color_make(0x17, 0x24, 0x33),
                                LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);

    P4SensorCreateLabel(scr,
                        "环境数据",
                        272,
                        60,
                        184,
                        30,
                        lv_color_make(0x47, 0x55, 0x69),
                        LV_TEXT_ALIGN_RIGHT);

    summary = lv_obj_create(scr);
    lv_obj_set_pos(summary, 24, 110);
    lv_obj_set_size(summary, 432, 292);
    lv_obj_set_style_radius(summary, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(summary, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(summary, lv_color_make(0xD9, 0xE2, 0xEC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(summary, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(summary, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(summary, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(summary, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    P4SensorCreateThermoIcon(summary);
    P4SensorCreateLabel(summary,
                        "温度",
                        150,
                        32,
                        80,
                        28,
                        lv_color_make(0x64, 0x74, 0x8B),
                        LV_TEXT_ALIGN_LEFT);
    s_temp_value_label = P4SensorCreateLabel(summary,
                                             "--.-- C",
                                             150,
                                             62,
                                             240,
                                             42,
                                             lv_color_make(0x10, 0xB9, 0x81),
                                             LV_TEXT_ALIGN_LEFT);
    P4SensorCreateLabel(summary,
                        "湿度",
                        150,
                        126,
                        80,
                        28,
                        lv_color_make(0x64, 0x74, 0x8B),
                        LV_TEXT_ALIGN_LEFT);
    s_humi_value_label = P4SensorCreateLabel(summary,
                                             "--.-- %",
                                             150,
                                             156,
                                             240,
                                             42,
                                             lv_color_make(0x38, 0xB2, 0xDF),
                                             LV_TEXT_ALIGN_LEFT);
    s_state_label = P4SensorCreateLabel(summary,
                                        "状态：等待上报",
                                        24,
                                        222,
                                        180,
                                        28,
                                        lv_color_make(0x33, 0x41, 0x55),
                                        LV_TEXT_ALIGN_LEFT);
    s_count_label = P4SensorCreateLabel(summary,
                                        "上报次数：0",
                                        210,
                                        222,
                                        180,
                                        28,
                                        lv_color_make(0x33, 0x41, 0x55),
                                        LV_TEXT_ALIGN_LEFT);
    s_recent_label = P4SensorCreateLabel(summary,
                                         "最近上报：0 ms",
                                         24,
                                         252,
                                         250,
                                         28,
                                         lv_color_make(0x64, 0x74, 0x8B),
                                         LV_TEXT_ALIGN_LEFT);

    P4SensorCreateLabel(scr,
                        "其他传感器",
                        24,
                        406,
                        180,
                        26,
                        lv_color_make(0x33, 0x41, 0x55),
                        LV_TEXT_ALIGN_LEFT);
    P4SensorCreateReservedGrid(scr);

    back = lv_button_create(scr);
    lv_obj_set_pos(back, 110, 720);
    lv_obj_set_size(back, 260, 54);
    lv_obj_set_style_bg_color(back, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_radius(back, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(back, P4SensorBackEventCb, LV_EVENT_CLICKED, NULL);
    back_label = lv_label_create(back);
    lv_obj_set_style_text_font(back_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_label_set_text(back_label, "返回桌面");
    lv_obj_center(back_label);

    s_last_valid = 0xFFFFFFFFU;
    s_last_rx_count = 0xFFFFFFFFU;
    s_last_invalid_count = 0xFFFFFFFFU;
    s_last_temp100 = INT32_MIN;
    s_last_humi100 = INT32_MIN;
    s_temp_cache[0] = '\0';
    s_humi_cache[0] = '\0';
    s_state_cache[0] = '\0';
    s_count_cache[0] = '\0';
    s_recent_cache[0] = '\0';
    s_visible = 1U;
    P4SensorPageRefresh();

    ESP_LOGI(TAG, "sensor page created");
}

void P4SensorPageProcessPending(void)
{
    OhosUartLinkSensorSnapshot sensor;

    if (!s_visible) {
        return;
    }

    (void)memset(&sensor, 0, sizeof(sensor));
    OhosUartLinkUiGetSensorSnapshot(&sensor);
    if (sensor.valid != s_last_valid ||
        sensor.rx_count != s_last_rx_count ||
        sensor.invalid_count != s_last_invalid_count ||
        sensor.temp100 != s_last_temp100 ||
        sensor.humi100 != s_last_humi100) {
        P4SensorPageRefresh();
    }
}
