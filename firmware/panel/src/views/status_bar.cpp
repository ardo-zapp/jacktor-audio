#include "status_bar.h"
#include "panel_theme.h"
#include "panel_main.h"

lv_obj_t* globalStatusBar = nullptr;

static lv_obj_t* statusLblTime = nullptr;
static lv_obj_t* statusBadgeMusic = nullptr;
static lv_obj_t* statusBadgeDac = nullptr;
static lv_obj_t* statusLedRx = nullptr;
static lv_obj_t* statusLedTx = nullptr;
static lv_obj_t* statusIconWifi = nullptr;

static uint32_t rxPulseMs = 0;
static uint32_t txPulseMs = 0;

void trigger_status_bar_rx_pulse(void) {
    rxPulseMs = millis();
}

void trigger_status_bar_tx_pulse(void) {
    txPulseMs = millis();
}

void create_global_status_bar(lv_obj_t* parentScreen) {
    if (!parentScreen) return;

    globalStatusBar = lv_obj_create(parentScreen);
    lv_obj_set_size(globalStatusBar, 320, 24);
    lv_obj_align(globalStatusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(globalStatusBar, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(globalStatusBar, 0, 0);
    lv_obj_set_style_radius(globalStatusBar, 0, 0);
    lv_obj_set_style_pad_all(globalStatusBar, 2, 0);

    // Clock
    statusLblTime = lv_label_create(globalStatusBar);
    lv_label_set_text(statusLblTime, "12:00");
    lv_obj_set_style_text_color(statusLblTime, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(statusLblTime, &lv_font_montserrat_10, 0);
    lv_obj_align(statusLblTime, LV_ALIGN_LEFT_MID, 6, 0);

    // Pill Badge: Music
    statusBadgeMusic = lv_label_create(globalStatusBar);
    lv_label_set_text(statusBadgeMusic, "♪ MUSIC");
    lv_obj_set_style_text_color(statusBadgeMusic, UI_COLOR_ACCENT_PURPLE, 0);
    lv_obj_set_style_text_font(statusBadgeMusic, &lv_font_montserrat_10, 0);
    lv_obj_align(statusBadgeMusic, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_add_flag(statusBadgeMusic, LV_OBJ_FLAG_HIDDEN);

    // Pill Badge: USB DAC
    statusBadgeDac = lv_label_create(globalStatusBar);
    lv_label_set_text(statusBadgeDac, "⚡ USB DAC");
    lv_obj_set_style_text_color(statusBadgeDac, UI_COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(statusBadgeDac, &lv_font_montserrat_10, 0);
    lv_obj_align(statusBadgeDac, LV_ALIGN_LEFT_MID, 118, 0);
    lv_obj_add_flag(statusBadgeDac, LV_OBJ_FLAG_HIDDEN);

    // Right: RX Dot LED (Green)
    statusLedRx = lv_obj_create(globalStatusBar);
    lv_obj_set_size(statusLedRx, 6, 6);
    lv_obj_set_style_radius(statusLedRx, 3, 0); // Dot shape
    lv_obj_align(statusLedRx, LV_ALIGN_RIGHT_MID, -38, 0);
    lv_obj_set_style_bg_color(statusLedRx, lv_color_hex(0x1b5e20), 0);

    // Right: TX Dot LED (Blue)
    statusLedTx = lv_obj_create(globalStatusBar);
    lv_obj_set_size(statusLedTx, 6, 6);
    lv_obj_set_style_radius(statusLedTx, 3, 0); // Dot shape
    lv_obj_align(statusLedTx, LV_ALIGN_RIGHT_MID, -26, 0);
    lv_obj_set_style_bg_color(statusLedTx, lv_color_hex(0x0d47a1), 0);

    // Right: Wi-Fi Icon
    statusIconWifi = lv_label_create(globalStatusBar);
    lv_label_set_text(statusIconWifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(statusIconWifi, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(statusIconWifi, &lv_font_montserrat_10, 0);
    lv_obj_align(statusIconWifi, LV_ALIGN_RIGHT_MID, -6, 0);
}

void update_global_status_bar(void) {
    if (!globalStatusBar) return;

    uint32_t now = millis();

    if (statusLblTime) lv_label_set_text(statusLblTime, currentTimeStr);

    if (statusBadgeMusic) {
        if (currentScreenState == ScreenState::MUSIC_PLAYER) lv_obj_clear_flag(statusBadgeMusic, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(statusBadgeMusic, LV_OBJ_FLAG_HIDDEN);
    }

    if (statusBadgeDac) {
        if (currentScreenState == ScreenState::USB_DAC) lv_obj_clear_flag(statusBadgeDac, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(statusBadgeDac, LV_OBJ_FLAG_HIDDEN);
    }

    if (statusLedRx) {
        if (now - rxPulseMs < 120) lv_obj_set_style_bg_color(statusLedRx, UI_COLOR_SECONDARY, 0);
        else lv_obj_set_style_bg_color(statusLedRx, lv_color_hex(0x1b5e20), 0);
    }

    if (statusLedTx) {
        if (now - txPulseMs < 120) lv_obj_set_style_bg_color(statusLedTx, UI_COLOR_PRIMARY, 0);
        else lv_obj_set_style_bg_color(statusLedTx, lv_color_hex(0x0d47a1), 0);
    }
}
