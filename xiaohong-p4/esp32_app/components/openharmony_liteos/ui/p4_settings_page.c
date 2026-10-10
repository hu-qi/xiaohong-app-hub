#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "config_service.h"
#include "device_identity_service.h"
#include "ohos_uart_protocol.h"
#include "ohos_uart_link_ui.h"
#include "p4_system_font.h"
#include "p4_ui_model.h"
#include "p4_settings_page.h"
#include "ohos_audio_es8311_port.h"
#include "ohos_display_backlight.h"

static const char *TAG = "p4_settings";

static P4SettingsPageBackHandler s_back_handler;
static volatile uint32_t s_page_visible;
static volatile uint32_t s_dirty;

static lv_obj_t *s_set_bri;
static lv_obj_t *s_set_vol;
static lv_obj_t *s_set_int;
//static lv_obj_t *s_set_ota_addr;  // 设置OTA服务器地址（暂不提供修改功能）


static lv_obj_t *s_info_dev_id;
static lv_obj_t *s_info_ver_p4;
static lv_obj_t *s_info_ver_ws63;
static lv_obj_t *s_info_ota_addr;    // OTA服务器地址
static lv_obj_t *s_info_zkt_addr;    // 智控台地址

static void P4BrightnessSelected(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target(e);
    char buf[16];
    lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    uint32_t val = (uint32_t)atoi(buf);
    if (val < 1 || val > 100) {
        return;
    }

    ConfigServiceData cfg;
    if (ConfigServiceGet(&cfg) == 0U) {
        cfg.brightness = val;
        /* 延迟保存：UI 任务栈在 PSRAM，直接写 flash 会触发 assert 重启 */
        (void)ConfigServiceSetValues(&cfg);
    }
    /* 立即把亮度下发到背光 PWM (GPIO20)，否则 UI 上改了但屏幕亮度不变 */
    (void)OhosDisplayBacklightSetPercent(val);
    P4SettingsPageMarkDirty();
}

static void P4VolumeSelected(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target(e);
    char buf[16];
    lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    uint32_t val = (uint32_t)atoi(buf);
    if (val < 0 || val > 100) {
        return;
    }

    ConfigServiceData cfg;
    if (ConfigServiceGet(&cfg) == 0U) {
        cfg.volume = val;
        /* 延迟保存：UI 任务栈在 PSRAM，直接写 flash 会触发 assert 重启 */
        (void)ConfigServiceSetValues(&cfg);
    }
    /* 立即把音量下发到 ES8311，否则 UI 上改了但芯片音量不变 */
    (void)OhosAudioEs8311PortSetVolume(val);
    P4SettingsPageMarkDirty();
}

static void P4InterruptSelected(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target(e);
    char buf[16];
    uint8_t val = 0;
    lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    if (memcmp(buf, "On", 2) == 0) {
        val = 1;
    } else {
        val = 0;
    }

    ConfigServiceData cfg;
    if (ConfigServiceGet(&cfg) == 0U) {
        cfg.interrupt_mode = (uint32_t) val;
        /* 延迟保存：UI 任务栈在 PSRAM，直接写 flash 会触发 assert 重启 */
        (void)ConfigServiceSetValues(&cfg);
    }
    OhosUartLinkUiSetInterruptMode(val);
    P4SettingsPageMarkDirty();
}

static lv_obj_t *P4SettingsCreateCard(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_make(0xD9, 0xE2, 0xEC), LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_make(0xF0, 0xF0, 0xF0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void P4SettingsBackEvent(lv_event_t *e)    // 返回桌面
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ESP_LOGI(TAG, "WiFi back clicked");
    P4SettingsPageHide();
    if (s_back_handler != NULL) {
        s_back_handler();
    }
}










static lv_obj_t *P4CreateInfoRow(lv_obj_t *parent, const char *key, int y)
{
    lv_obj_t *key_label = lv_label_create(parent);
    lv_obj_set_pos(key_label, 4, y);
    lv_obj_set_size(key_label, 400, 28);
    lv_obj_set_style_text_font(key_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(key_label, lv_color_make(0x64, 0x74, 0x8B), LV_PART_MAIN);
    lv_label_set_text(key_label, key);
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_DOT);
    return key_label;
}

void P4SettingsPageSetBackHandler(P4SettingsPageBackHandler handler)
{
    s_back_handler = handler;
}

void P4SettingsPageHide(void)
{
    s_page_visible = 0U;

    s_set_bri = NULL;
    s_set_vol = NULL;
    s_set_int = NULL;

    s_info_dev_id = NULL;
    s_info_ver_p4 = NULL;
    s_info_ver_ws63 = NULL;
    s_info_ota_addr = NULL;
    s_info_zkt_addr = NULL;
}

void P4SettingsPageCreate(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *lable;
    lv_obj_t *card;
    lv_obj_t *back_btn;
    lv_obj_t *back_label;

    if (parent == NULL || !lv_obj_is_valid(parent)) {
        return;
    }

    P4SystemFontApply(parent);
    lv_obj_set_style_bg_color(parent, lv_color_make(0xF1, 0xF5, 0xF9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

    /* ===== Title ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "设置/设备信息");
    lv_obj_set_width(title, 480);  // font->line_height ~= 28
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(title, 0, 20);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    /* ===== Section 1: WiFi Connection Status ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "设置(功能待完善)");
    lv_obj_set_pos(title, 24, 56);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    card = P4SettingsCreateCard(parent, 24, 92, 432, 200);
    {
        //s_conn_st_label = P4CreateInfoRow(card, "状态:", 0);
        //s_ssid_label = P4CreateInfoRow(card, "热点:", 32);
        //s_ip_label = P4CreateInfoRow(card, "IP:", 64);

        //static lv_obj_t *s_set_ota_addr;
        ConfigServiceData cfg;
        ConfigServiceGet(&cfg);

// brightness
        lable = lv_label_create(card);
        lv_label_set_text(lable, "亮度: ");
        lv_obj_set_pos(lable, 4, 10);
        lv_obj_set_size(lable, 120, 48);
        lv_obj_set_style_text_font(lable, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lable, lv_color_make(0x64, 0x74, 0x8B), LV_PART_MAIN);
        lv_obj_set_style_text_align(lable, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

        s_set_bri = lv_dropdown_create(card);
        lv_obj_set_pos(s_set_bri, 130, 4);
        lv_obj_set_size(s_set_bri, 80, 48);
        lv_obj_set_style_text_font(s_set_bri, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_set_bri, 4, LV_PART_MAIN);
        lv_dropdown_set_symbol(s_set_bri, LV_SYMBOL_DOWN);
        lv_dropdown_set_options(s_set_bri, "1\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100");
        lv_obj_add_event_cb(s_set_bri, P4BrightnessSelected, LV_EVENT_VALUE_CHANGED, NULL);

        /* Set initial dropdown selection to current brightness */
        if (cfg.brightness >= 1 && cfg.brightness <= 100) {
            lv_dropdown_set_selected(s_set_bri, cfg.brightness / 10);
        }
        /* 打开设置页时把持久化亮度同步到背光，保证与配置一致 */
        (void)OhosDisplayBacklightSetPercent(cfg.brightness > 100U ? 100U : cfg.brightness);
// volume
        lable = lv_label_create(card);
        lv_label_set_text(lable, "音量: ");
        lv_obj_set_pos(lable, 4, 62);
        lv_obj_set_size(lable, 120, 48);
        lv_obj_set_style_text_font(lable, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lable, lv_color_make(0x64, 0x74, 0x8B), LV_PART_MAIN);
        lv_obj_set_style_text_align(lable, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

        s_set_vol = lv_dropdown_create(card);
        lv_obj_set_pos(s_set_vol, 130, 56);
        lv_obj_set_size(s_set_vol, 80, 48);
        lv_obj_set_style_text_font(s_set_vol, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_set_vol, 4, LV_PART_MAIN);
        lv_dropdown_set_symbol(s_set_vol, LV_SYMBOL_DOWN);
        lv_dropdown_set_options(s_set_vol, "0\n10\n20\n30\n40\n50\n60\n70\n80\n90\n100");
        lv_obj_add_event_cb(s_set_vol, P4VolumeSelected, LV_EVENT_VALUE_CHANGED, NULL);

        /* Set initial dropdown selection to current volume */
        if (cfg.volume >= 1 && cfg.volume <= 100) {
            lv_dropdown_set_selected(s_set_vol, cfg.volume / 10);
        }

        /* 打开设置页时把持久化音量同步到 ES8311，保证开机后音量与配置一致 */
        (void)OhosAudioEs8311PortSetVolume(cfg.volume > 100U ? 100U : cfg.volume);

// voice interrupt mode
        lable = lv_label_create(card);
        lv_label_set_text(lable, "对话打断模式: ");
        lv_obj_set_pos(lable, 4, 114);
        lv_obj_set_size(lable, 120, 48);
        lv_obj_set_style_text_font(lable, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lable, lv_color_make(0x64, 0x74, 0x8B), LV_PART_MAIN);
        lv_obj_set_style_text_align(lable, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

        s_set_int = lv_dropdown_create(card);
        lv_obj_set_pos(s_set_int, 130, 108);
        lv_obj_set_size(s_set_int, 80, 48);
        lv_obj_set_style_text_font(s_set_int, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_set_int, 4, LV_PART_MAIN);
        lv_dropdown_set_symbol(s_set_int, LV_SYMBOL_DOWN);
        lv_dropdown_set_options(s_set_int, "Off\nOn");
        lv_obj_add_event_cb(s_set_int, P4InterruptSelected, LV_EVENT_VALUE_CHANGED, NULL);
        /* Set initial dropdown selection to current interrupt mode */
        lv_dropdown_set_selected(s_set_int, (cfg.interrupt_mode>0)?(1):(0));
    }

    /* ===== Section 2: Network Configuration ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "设备信息");
    lv_obj_set_pos(title, 24, 320);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    card = P4SettingsCreateCard(parent, 24, 360, 432, 300);
    {
        s_info_dev_id = P4CreateInfoRow(card, "设备ID:  ", 0);
        s_info_ver_p4 = P4CreateInfoRow(card, "固件版本(P4):  ", 32);
        s_info_ver_ws63 = P4CreateInfoRow(card, "固件版本(WS63):  ", 64);
        s_info_ota_addr = P4CreateInfoRow(card, "OTA服务器地址:  ", 96);
        lv_label_set_long_mode(s_info_ota_addr, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(s_info_ota_addr, LV_SIZE_CONTENT);
        s_info_zkt_addr = P4CreateInfoRow(card, "智控台登录地址:  ", 192);
        lv_label_set_long_mode(s_info_zkt_addr, LV_LABEL_LONG_WRAP);
        lv_obj_set_height(s_info_zkt_addr, LV_SIZE_CONTENT);
    }

    /* ===== Section: Back Button ===== */
    back_btn = lv_button_create(parent);
    lv_obj_set_pos(back_btn, 165, 730);
    lv_obj_set_size(back_btn, 150, 48);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, P4SettingsBackEvent, LV_EVENT_CLICKED, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回桌面");
    lv_obj_set_style_text_font(back_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
    lv_obj_center(back_label);
}

void P4SettingsPageRefresh(void)
{
    ConfigServiceData cfg;
    ConfigServiceGet(&cfg);

    if (s_info_dev_id != NULL && lv_obj_is_valid(s_info_dev_id)) {
        lv_label_set_text_fmt(s_info_dev_id, "设备ID:  %s", (cfg.device_id_hex[0] !='\0') ? &cfg.device_id_hex[20] : "----");
    }

    if (s_info_ver_p4 != NULL && lv_obj_is_valid(s_info_ver_p4)) {
        lv_label_set_text_fmt(s_info_ver_p4, "固件版本(P4):  %s", (cfg.p4_version[0] !='\0') ? cfg.p4_version : "----");
    }

    if (s_info_ver_ws63 != NULL && lv_obj_is_valid(s_info_ver_ws63)) {
        lv_label_set_text_fmt(s_info_ver_ws63, "固件版本(WS63):  %s", (cfg.ws63_version[0] !='\0') ? cfg.ws63_version : "----");
    }

    if (s_info_ota_addr != NULL && lv_obj_is_valid(s_info_ota_addr)) {
        lv_label_set_text_fmt(s_info_ota_addr, "OTA服务器地址:  \n%s", (cfg.ota_url[0] !='\0') ? cfg.ota_url : "----");
    }

    if (s_info_zkt_addr != NULL && lv_obj_is_valid(s_info_zkt_addr)) {
        lv_label_set_text_fmt(s_info_zkt_addr, "智控台登录地址:  \n%s", XIAOHONG_SERVER_URL);
    }
}

void P4SettingsPageMarkDirty(void)
{
    s_dirty = 1U;
}

void P4SettingsPageProcessPending(void)
{
    if (!s_page_visible) {
        return;
    }

    if (!s_dirty) {
        return;
    }
    s_dirty = 0U;

    P4SettingsPageRefresh();
}

void P4SettingsPageShow(void)
{
    lv_obj_t *scr = lv_screen_active();

    if (scr == NULL) {
        return;
    }

    P4SettingsPageHide();
    s_page_visible = 0U;
    s_dirty = 0U;

    lv_obj_clean(scr);
    P4SettingsPageCreate(scr);
    s_page_visible = 1U;

    P4SettingsPageRefresh();

    ESP_LOGI(TAG, "Settings page created");
}
