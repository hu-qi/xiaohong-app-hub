#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "device_identity_service.h"
#include "ohos_uart_protocol.h"
#include "ohos_uart_link_ui.h"
#include "p4_system_font.h"
#include "p4_ui_model.h"
#include "p4_wifi_page.h"

static const char *TAG = "p4_wifi";

static P4WiFiPageBackHandler s_back_handler;
static lv_obj_t *s_conn_st_label;
static lv_obj_t *s_ssid_label;
static lv_obj_t *s_ip_label;
static lv_obj_t *s_scan_btn;  // "开始扫描(蓝底白字)"/"正在扫描(灰底白字)"/"扫描完成(绿底白字)"
static lv_obj_t *s_scan_btn_label;
static lv_obj_t *s_status_value;
static lv_obj_t *s_option_ssid_list;
static lv_obj_t *s_in_pswd;
static lv_obj_t *s_keyboard;
static volatile uint32_t s_page_visible;
static volatile uint32_t s_dirty;
static volatile uint32_t s_scan_stat;
static char s_selected_ssid[128] = "\0";
static char s_selected_pswd[128] = "\0";

// LVGL内置的虚拟按键组件太丑，也不好修改布局，这里重新生成一个比较顺眼的虚拟按键组件，
// 有待将其提升为一个独立组件，给其它页面需要输入字符的地方使用。
#define  USE_LVGL_DEFAULT_VKB   0

#if USE_LVGL_DEFAULT_VKB
static void VirtualKyeboardEventCb(lv_event_t *e)  // 虚拟键盘按键处理回调
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (s_keyboard) {
            lv_keyboard_set_textarea(s_keyboard, NULL);
            lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
        if (code == LV_EVENT_READY && s_in_pswd) {
            memset(s_selected_pswd, '\0', sizeof(s_selected_pswd));
            const char *text = lv_textarea_get_text(s_in_pswd);
            if (text) {
                strncpy(s_selected_pswd, text, sizeof(s_selected_pswd) - 1);
                s_selected_pswd[sizeof(s_selected_pswd) - 1] = '\0';
            }
            ESP_LOGI(TAG, "password: %s", s_selected_pswd);
        }
    }
}

static lv_obj_t *VirtualKyeboardCreat(lv_obj_t *src)  // 创建虚拟键盘
{
	lv_obj_t *vt_keyboard = lv_keyboard_create(src);
	//lv_obj_set_height(vt_keyboard, lv_obj_get_height(s_keyboard) / 2);  // 想调整高度失败
	lv_obj_add_event_cb(vt_keyboard, VirtualKyeboardEventCb, LV_EVENT_ALL, NULL);
    lv_keyboard_set_textarea(vt_keyboard, s_in_pswd);

    return vt_keyboard;
}

#else

static bool s_kb_num_mode;
/* 自定义4行WiFi密码键盘 — 字母模式 */
static const char * const s_kb_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "@", "_", "-", "\n",
    "123", " ", LV_SYMBOL_OK, ""
};
static const lv_buttonmatrix_ctrl_t s_kb_ctrl_lc[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 4, 2
};

/* 自定义4行WiFi密码键盘 — 数字/符号模式 */
static const char * const s_kb_map_num[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "-", "=", "+", "[", "]", "{", "}", "\\", "|", ";", ":", "\n",
    "abc", " ", LV_SYMBOL_OK, ""
};
static const lv_buttonmatrix_ctrl_t s_kb_ctrl_num[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 4, 2
};

static void VirtualKyeboardEventCb(lv_event_t *e)  // 虚拟键盘按键处理回调
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

    lv_obj_t *btnm = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const char *txt = lv_buttonmatrix_get_button_text(btnm, id);
    if (txt == NULL) return;

    // OK → 保存密码并隐藏键盘
    if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        if (s_in_pswd) {
            memset(s_selected_pswd, '\0', sizeof(s_selected_pswd));
            const char *text = lv_textarea_get_text(s_in_pswd);
            if (text) {
                strncpy(s_selected_pswd, text, sizeof(s_selected_pswd) - 1);
                s_selected_pswd[sizeof(s_selected_pswd) - 1] = '\0';
            }
            ESP_LOGI(TAG, "password: %s", s_selected_pswd);
        }
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // 退格键 → 删除光标前一个字符
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if (s_in_pswd) lv_textarea_delete_char(s_in_pswd);
        return;
    }

    // 回车键 → 隐藏键盘
    if (strcmp(txt, LV_SYMBOL_NEW_LINE) == 0) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // 模式切换
    if (strcmp(txt, "123") == 0) {
        s_kb_num_mode = true;
        lv_buttonmatrix_set_map(btnm, s_kb_map_num);
        lv_buttonmatrix_set_ctrl_map(btnm, s_kb_ctrl_num);
        return;
    }
    if (strcmp(txt, "abc") == 0) {
        s_kb_num_mode = false;
        lv_buttonmatrix_set_map(btnm, s_kb_map_lc);
        lv_buttonmatrix_set_ctrl_map(btnm, s_kb_ctrl_lc);
        return;
    }

    // 普通字符 → 插入到密码输入框
    if (s_in_pswd) lv_textarea_add_text(s_in_pswd, txt);
}

static lv_obj_t *VirtualKyeboardCreat(lv_obj_t *src)  // 创建虚拟键盘
{
	s_kb_num_mode = false;

    lv_obj_t *vt_keyboard = lv_buttonmatrix_create(src);
    lv_obj_set_size(vt_keyboard, LV_PCT(100), LV_PCT(27));
    lv_obj_align(vt_keyboard, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_color(vt_keyboard, lv_color_make(0xE0, 0xE0, 0xE0), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(vt_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    /* 按键边框与底色，清晰区分每个按键 */
    lv_obj_set_style_border_width(vt_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(vt_keyboard, lv_color_make(0xCC, 0xCC, 0xCC), LV_PART_ITEMS);
    lv_obj_set_style_border_opa(vt_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(vt_keyboard, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(vt_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_radius(vt_keyboard, 3, LV_PART_ITEMS);
    /* 缩小按键列间距，让左右按键宽度更大 */
    lv_obj_set_style_pad_column(vt_keyboard, 2, LV_PART_MAIN);
    lv_buttonmatrix_set_map(vt_keyboard, s_kb_map_lc);
    lv_buttonmatrix_set_ctrl_map(vt_keyboard, s_kb_ctrl_lc);
    lv_obj_add_event_cb(vt_keyboard, VirtualKyeboardEventCb, LV_EVENT_VALUE_CHANGED, NULL);

    return vt_keyboard;
}
#endif

static void P4WiFiPswdFocusHandler(lv_event_t *e)  // 点击输入框输入弹出键盘
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_FOCUSED) {
        return;
    }

    if (s_keyboard == NULL) {
        s_keyboard = VirtualKyeboardCreat(lv_scr_act());
    }

    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void P4WiFiApBtnClick(lv_event_t *e)  // 开始热点配网
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGI(TAG, "WiFi AP clicked");
    UartLinkSendWifiStartProvisioning(OHOS_UART_CMD_WIFI_RE_CONNECT, NULL, NULL, "WIFI_RE_CONNECT");
}

static void P4WiFiStartBtnClick(lv_event_t *e)  // 开始配网
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (s_selected_ssid[0] == '\0' || s_selected_pswd[0] == '\0') {
        ESP_LOGI(TAG, "selected SSID or password is empty");
        return;
    }
    ESP_LOGI(TAG, "selected SSID: %s, password: %s", s_selected_ssid, s_selected_pswd);
    UartLinkSendWifiStartProvisioning(OHOS_UART_CMD_WIFI_INIT_CONNECT, s_selected_ssid, s_selected_pswd, "WIFI_INIT_CONNECT");
    if (s_in_pswd) {
        s_selected_ssid[0] = '\0';
        s_selected_pswd[0] = '\0';
        char lists[128] = "请先扫描再选择待接入的WiFi热点\0";
        lv_dropdown_set_options(s_option_ssid_list, lists);
        lv_textarea_set_text(s_in_pswd, "");
    }
}


void P4WiFiScanBtnStyle(uint8_t stat)
{
    // 0: "开始扫描(蓝底白字)"/ 1: "正在扫描(灰底白字)"/ 2: "扫描完成(绿底白字)"
    if (stat == 0) {
        lv_label_set_text(s_scan_btn_label, "开始扫描");  // 蓝色
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    } else if (stat == 1) {
        lv_label_set_text(s_scan_btn_label, "正在扫描");  // 灰色
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_PART_MAIN);
    } else {
        // 0x0D07: WS63向P4报告WiFi的扫描结果并保存之后，刷新UI上的扫描按钮状态
        // if (frame->cmd_type == OHOS_UART_CMD_WIFI_SCAN_RESULT) {
        //    P4WiFiScanBtnStyle(2);  // 2-"扫描完成"
        // }
        lv_label_set_text(s_scan_btn_label, "扫描完成");  // 绿色
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0x22, 0xC5, 0x5E), LV_PART_MAIN);
    }
    s_scan_stat = stat;
}

static void P4WiFiScanBtnClick(lv_event_t *e)  // 开始扫描
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ESP_LOGI(TAG, "scan WiFi clicked");
    if ((s_scan_stat == 0) || (s_scan_stat == 2)) {
        P4WiFiScanBtnStyle(1);
        UartLinkSendWiFiStartScan("p4_wifi_page");
    }
}

static void P4WiFiBackEvent(lv_event_t *e)    // 返回桌面
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    ESP_LOGI(TAG, "WiFi back clicked");
    P4WiFiPageHide();
    if (s_back_handler != NULL) {
        s_back_handler();
    }
}

static lv_obj_t *P4WiFiCreateCard(lv_obj_t *parent, int x, int y, int w, int h)
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

static lv_obj_t *P4WiFiCreateInfoRow(lv_obj_t *parent, const char *key, int y)
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

static lv_obj_t *P4WiFiCreateActionBtn(lv_obj_t *parent, const char *text, int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 150, 48);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_center(label);

    return label;
}

static void P4WiFiOptionUpdate(lv_obj_t *option)
{
    const char *scan = OhosUartLinkUiGetWifiScanResult();
    char lists[512] = "请先扫描再选择待接入的WiFi热点\n\0";
    size_t out = strlen(lists);
    if (scan != NULL && scan[0] != '\0') {
        size_t pos = 0;
        while (scan[pos] != '\0' && out < sizeof(lists) - 2) {
            if (scan[pos] == '"') {
                pos++;
                while (scan[pos] != '\0' && scan[pos] != '"' && out < sizeof(lists) - 2) {
                    lists[out++] = scan[pos];
                    pos++;
                }
                if (scan[pos] == '"') {
                    pos++;
                }
                lists[out++] = '\n';
            } else {
                pos++;
            }
        }
        lists[out-1] = '\0';
        lists[out] = '\0';
    }
    lv_dropdown_set_options(option, lists);
}

static void P4WiFiOptionSelected(lv_event_t *e)
{
    if (s_scan_stat == 2) {
        P4WiFiScanBtnStyle(0);
    }

    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    memset(s_selected_ssid, '\0', sizeof(s_selected_ssid));

    lv_obj_t *dd = lv_event_get_target(e);
    lv_dropdown_get_selected_str(dd, s_selected_ssid, sizeof(s_selected_ssid));
    if (0 == memcmp(s_selected_ssid, "请先扫描再选择待接入的WiFi热点", strlen("请先扫描再选择待接入的WiFi热点"))) {
        ESP_LOGI(TAG, "selected SSID: item[0], invalid");
        s_selected_ssid[0] = '\0';
    } else {
        ESP_LOGI(TAG, "selected SSID: %s", s_selected_ssid);
    }
}

void P4WiFiPageSetBackHandler(P4WiFiPageBackHandler handler)
{
    s_back_handler = handler;
}

void P4WiFiPageHide(void)
{
    s_page_visible = 0U;
    if (s_keyboard) {
        #if USE_LVGL_DEFAULT_VKB
        lv_keyboard_set_textarea(s_keyboard, NULL);
        #endif
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    s_conn_st_label = NULL;
    s_ssid_label = NULL;
    s_ip_label = NULL;
    s_scan_btn = NULL;
    s_scan_btn_label = NULL;
    s_status_value = NULL;
    s_option_ssid_list = NULL;
    s_in_pswd = NULL;
    s_keyboard = NULL;
}

static uint16_t Get2ByteDevID(void)
{
    uint16_t id2byte = 0xAABB;

    uint8_t deviceId[DEVICE_ID_MAX_LEN];
    uint32_t deviceIdLen = 0;
    uint32_t getRet;

    getRet = DeviceIdentityGetRaw(deviceId, sizeof(deviceId), &deviceIdLen);
    if (getRet != 0U || deviceIdLen == 0U || deviceIdLen > DEVICE_ID_MAX_LEN) {
        ESP_LOGI(TAG, "Get2ByteDevID get device id fail");
    } else {
        id2byte = (deviceId[deviceIdLen-2] << 8 ) | (deviceId[deviceIdLen-1]);
        ESP_LOGI(TAG, "Get2ByteDevID get device id 0x%04X", id2byte);
    }

    return id2byte;
}
void P4WiFiPageCreate(lv_obj_t *parent)
{
    lv_obj_t *card;
    lv_obj_t *title;
    lv_obj_t *ap_btn;
    lv_obj_t *ap_btn_label;
    lv_obj_t *ap_name_lable;
    lv_obj_t *ap_hint_lable;
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
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_width(title, 480);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(title, 0, 20);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    /* ===== Section 1: WiFi Connection Status ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "当前连接状态");
    lv_obj_set_pos(title, 24, 56);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    card = P4WiFiCreateCard(parent, 24, 92, 432, 120);
    {
        s_conn_st_label = P4WiFiCreateInfoRow(card, "状态:", 0);
        s_ssid_label = P4WiFiCreateInfoRow(card, "热点:", 32);
        s_ip_label = P4WiFiCreateInfoRow(card, "IP:", 64);
    }

    /* ===== Section 2: Network Configuration ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "配网方式一: 本地配网");
    lv_obj_set_pos(title, 24, 230);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    card = P4WiFiCreateCard(parent, 24, 268, 432, 210);
    {
        /* ===== SSID List ===== */
        s_option_ssid_list = lv_dropdown_create(card);
        lv_obj_set_pos(s_option_ssid_list, 4, 4);
        lv_obj_set_size(s_option_ssid_list, 408, 60);
        lv_obj_set_style_text_font(s_option_ssid_list, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_option_ssid_list, 4, LV_PART_MAIN);
        lv_dropdown_set_symbol(s_option_ssid_list, LV_SYMBOL_DOWN);    // 没有 LV_SYMBOL_DOWN 图标
        lv_obj_add_event_cb(s_option_ssid_list, P4WiFiOptionSelected, LV_EVENT_VALUE_CHANGED, NULL);
        P4WiFiOptionUpdate(s_option_ssid_list);

        /* ===== Password Input ===== */
        s_in_pswd = lv_textarea_create(card);
        lv_obj_set_pos(s_in_pswd, 4, 70);
        lv_obj_set_size(s_in_pswd, 408, 60);
        lv_textarea_set_placeholder_text(s_in_pswd, "请输入WiFi密码");
        lv_obj_set_style_text_font(s_in_pswd, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_radius(s_in_pswd, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(s_in_pswd, P4WiFiPswdFocusHandler, LV_EVENT_FOCUSED, NULL);

        /* ===== Scan Button ===== */
        s_scan_btn = lv_button_create(card);
        lv_obj_set_pos(s_scan_btn, 13, 140);
        lv_obj_set_size(s_scan_btn, 150, 48);
        lv_obj_set_style_radius(s_scan_btn, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_scan_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_scan_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_event_cb(s_scan_btn, P4WiFiScanBtnClick, LV_EVENT_CLICKED, NULL);

        s_scan_btn_label = lv_label_create(s_scan_btn);
        lv_label_set_text(s_scan_btn_label, "开始扫描");
        lv_obj_set_style_text_font(s_scan_btn_label, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_scan_btn_label, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
        lv_obj_center(s_scan_btn_label);
        P4WiFiScanBtnStyle(0);

        /* ===== Start Button ===== */
        P4WiFiCreateActionBtn(card, "开始配网", 256, 140, P4WiFiStartBtnClick);
    }

    /* ===== Section 3: AP Button ===== */
    title = lv_label_create(parent);
    lv_label_set_text(title, "配网方式二: 热点配网");
    lv_obj_set_pos(title, 24, 488);
    lv_obj_set_style_text_font(title, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_make(0x17, 0x24, 0x33), LV_PART_MAIN);

    card = P4WiFiCreateCard(parent, 24, 520, 432, 200);
    {
        ap_name_lable = lv_label_create(card);
        lv_obj_set_pos(ap_name_lable, 4, 2);
        lv_obj_set_size(ap_name_lable, 180, 60);
        lv_label_set_text_fmt(ap_name_lable, "热点名称\nXiaohong_%04X", Get2ByteDevID());
        lv_obj_set_style_text_font(ap_name_lable, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(ap_name_lable, -8, LV_PART_MAIN);    // 减小行间距
        lv_obj_set_style_text_color(ap_name_lable, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
        //lv_obj_set_align(ap_name_lable, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_align(ap_name_lable, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        //lv_obj_set_style_bg_color(ap_name_lable, lv_color_make(0xE0, 0xE0, 0xE0), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ap_name_lable, LV_OPA_COVER, LV_PART_MAIN);

        ap_btn = lv_button_create(card);
        lv_obj_set_pos(ap_btn, 256, 6);
        lv_obj_set_size(ap_btn, 150, 48);
        lv_obj_set_style_radius(ap_btn, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ap_btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
        lv_obj_set_style_bg_color(ap_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ap_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_event_cb(ap_btn, P4WiFiApBtnClick, LV_EVENT_CLICKED, NULL);

        ap_btn_label = lv_label_create(ap_btn);
        lv_label_set_text(ap_btn_label, "开始配网");
        lv_obj_set_style_text_font(ap_btn_label, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(ap_btn_label, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
        lv_obj_center(ap_btn_label);

        ap_hint_lable = lv_label_create(card);
        lv_obj_set_pos(ap_hint_lable, 13, 70);
        lv_obj_set_size(ap_hint_lable, 430, 150);
        lv_label_set_text_fmt(ap_hint_lable, "1. 手机搜索并连接上述热点(无需密码)\n2. 用浏览器打开网址 \"http://192.168.1.1\"\n3. 输入热点信息后操作配网即可");
        lv_obj_set_style_text_font(ap_hint_lable, P4SystemFontGet(), LV_PART_MAIN);
        lv_obj_set_style_text_color(ap_hint_lable, lv_color_make(0x00, 0x00, 0x00), LV_PART_MAIN);
        lv_obj_set_style_text_align(ap_hint_lable, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    /* ===== Section 4: Back Button ===== */
    back_btn = lv_button_create(parent);
    lv_obj_set_pos(back_btn, 165, 730);
    lv_obj_set_size(back_btn, 150, 48);
    lv_obj_set_style_radius(back_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(0x3B, 0x82, 0xF6), LV_PART_MAIN);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(0xCC, 0xCC, 0xCC), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_event_cb(back_btn, P4WiFiBackEvent, LV_EVENT_CLICKED, NULL);

    back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "返回桌面");
    lv_obj_set_style_text_font(back_label, P4SystemFontGet(), LV_PART_MAIN);
    lv_obj_set_style_text_color(back_label, lv_color_make(0xFF, 0xFF, 0xFF), LV_PART_MAIN);
    lv_obj_center(back_label);
}

static const char *P4WiFiLinkStatusText(uint32_t status)
{
    switch (status) {
        case 0U:  return "未连接";
        case 1U:  return "连接中...";
        case 2U:  return "配网中...";
        case 3U:  return "已连接";
        default:  return "未知";
    }
}

void P4WiFiPageRefresh(void)
{
    P4UiModelSnapshot model;
    P4UiModelGetSnapshot(&model);

    if (s_conn_st_label != NULL && lv_obj_is_valid(s_conn_st_label)) {
        lv_label_set_text_fmt(s_conn_st_label, "状态:  %s", P4WiFiLinkStatusText(model.wifi_link_status));
    }

    if (model.wifi_link_status == 3U) {  // 已连接
        if (s_ssid_label != NULL && lv_obj_is_valid(s_ssid_label)) {
            const char *ssid = OhosUartLinkUiGetWifiSsid();
            lv_label_set_text_fmt(s_ssid_label, "热点:  %s", ssid ? ssid : "--");
        }
        if (s_ip_label != NULL && lv_obj_is_valid(s_ip_label)) {
            const char *ip = OhosUartLinkUiGetWifiLinkIp();
            lv_label_set_text_fmt(s_ip_label, "  I P:  %s", ip ? ip : "--");
        }
        if (s_in_pswd != NULL && lv_obj_is_valid(s_in_pswd)) {
            lv_textarea_set_placeholder_text(s_in_pswd, "请输入WiFi密码");
        }
    } else {
        if (s_ssid_label != NULL && lv_obj_is_valid(s_ssid_label)) {
            lv_label_set_text_fmt(s_ssid_label, "热点:  --");
        }
        if (s_ip_label != NULL && lv_obj_is_valid(s_ip_label)) {
            lv_label_set_text_fmt(s_ip_label, "  I P:  --");
        }
    }

    if (s_option_ssid_list != NULL && lv_obj_is_valid(s_option_ssid_list)) {
        P4WiFiOptionUpdate(s_option_ssid_list);
    }
}

void P4WiFiPageMarkDirty(void)
{
    s_dirty = 1U;
}

void P4WiFiPageProcessPending(void)
{
    if (!s_page_visible) {
        return;
    }

    if (!s_dirty) {
        return;
    }
    s_dirty = 0U;

    P4WiFiPageRefresh();
}

void P4WiFiPageShow(void)
{
    lv_obj_t *scr = lv_screen_active();

    if (scr == NULL) {
        return;
    }

    P4WiFiPageHide();
    s_page_visible = 0U;
    s_dirty = 0U;

    lv_obj_clean(scr);
    P4WiFiPageCreate(scr);
    s_page_visible = 1U;

    P4WiFiPageRefresh();

    ESP_LOGI(TAG, "WiFi page created");
}
