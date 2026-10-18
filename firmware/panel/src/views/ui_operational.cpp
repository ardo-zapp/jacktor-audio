#include "panel_ui.h"
#include "panel_theme.h"
#include "panel_main.h"

lv_obj_t* operationalScreen = nullptr;
lv_obj_t* labelTime = nullptr;
lv_obj_t* labelVoltages = nullptr;
lv_obj_t* labelTemps = nullptr;
lv_obj_t* labelInputBadge = nullptr;

static lv_obj_t* homeVuBarL = nullptr;
static lv_obj_t* homeVuBarR = nullptr;
static const uint8_t kNumHomeSpecBars = 10;
static lv_obj_t* homeSpecBars[10] = {nullptr};

static lv_obj_t* btnBtPower = nullptr;
static lv_obj_t* lblBtPower = nullptr;

void create_operational_screen() {
    operationalScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(operationalScreen, UI_COLOR_BG, 0);

    // =========================================================================
    // 1. SYSTEM TOP STATUS BAR (Android Style)
    // =========================================================================
    lv_obj_t* statusBar = lv_obj_create(operationalScreen);
    lv_obj_set_size(statusBar, 320, 26);
    lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(statusBar, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(statusBar, 0, 0);
    lv_obj_set_style_radius(statusBar, 0, 0);

    labelTime = lv_label_create(statusBar);
    lv_label_set_text(labelTime, "12:00");
    lv_obj_set_style_text_color(labelTime, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(labelTime, &lv_font_montserrat_10, 0);
    lv_obj_align(labelTime, LV_ALIGN_LEFT_MID, 4, 0);

    labelInputBadge = lv_label_create(statusBar);
    lv_label_set_text(labelInputBadge, "[ AUX ]");
    lv_obj_set_style_text_color(labelInputBadge, UI_COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(labelInputBadge, &lv_font_montserrat_10, 0);
    lv_obj_align(labelInputBadge, LV_ALIGN_LEFT_MID, 52, 0);

    labelVoltages = lv_label_create(statusBar);
    lv_label_set_text(labelVoltages, "12.0V | 3.3V | 5.0V");
    lv_obj_set_style_text_color(labelVoltages, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(labelVoltages, &lv_font_montserrat_10, 0);
    lv_obj_align(labelVoltages, LV_ALIGN_RIGHT_MID, -4, 0);

    labelNoLinkIndicator = lv_label_create(statusBar);
    lv_label_set_text(labelNoLinkIndicator, "[ BYPASS ]");
    lv_obj_set_style_text_color(labelNoLinkIndicator, UI_COLOR_DANGER, 0);
    lv_obj_set_style_text_font(labelNoLinkIndicator, &lv_font_montserrat_10, 0);
    lv_obj_align(labelNoLinkIndicator, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(labelNoLinkIndicator, LV_OBJ_FLAG_HIDDEN);

    // =========================================================================
    // 2. AUDIO VISUALIZER CARD (MONO SPECTRUM + STEREO L/R VU METER)
    // =========================================================================
    lv_obj_t* visCard = lv_obj_create(operationalScreen);
    lv_obj_set_size(visCard, 308, 62);
    lv_obj_align(visCard, LV_ALIGN_TOP_MID, 0, 30);
    ui_apply_card_style(visCard);

    for (uint8_t i = 0; i < kNumHomeSpecBars; i++) {
        homeSpecBars[i] = lv_bar_create(visCard);
        lv_obj_set_size(homeSpecBars[i], 11, 46);
        lv_obj_align(homeSpecBars[i], LV_ALIGN_LEFT_MID, i * 15 + 2, 0);
        lv_bar_set_range(homeSpecBars[i], 0, 255);
        lv_bar_set_value(homeSpecBars[i], 20, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(homeSpecBars[i], lv_color_hex(0x222533), LV_PART_MAIN);
        lv_obj_set_style_bg_color(homeSpecBars[i], UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    }

    lv_obj_t* vuBox = lv_obj_create(visCard);
    lv_obj_set_size(vuBox, 134, 46);
    lv_obj_align(vuBox, LV_ALIGN_RIGHT_MID, -2, 0);
    ui_apply_card_style(vuBox);

    lv_obj_t* lblL = lv_label_create(vuBox);
    lv_label_set_text(lblL, "L");
    lv_obj_set_style_text_color(lblL, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(lblL, &lv_font_montserrat_10, 0);
    lv_obj_align(lblL, LV_ALIGN_TOP_LEFT, 0, 2);

    homeVuBarL = lv_bar_create(vuBox);
    lv_obj_set_size(homeVuBarL, 108, 10);
    lv_obj_align(homeVuBarL, LV_ALIGN_TOP_LEFT, 14, 3);
    lv_bar_set_range(homeVuBarL, 0, 255);
    lv_obj_set_style_bg_color(homeVuBarL, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(homeVuBarL, UI_COLOR_SECONDARY, LV_PART_INDICATOR);

    lv_obj_t* lblR = lv_label_create(vuBox);
    lv_label_set_text(lblR, "R");
    lv_obj_set_style_text_color(lblR, lv_color_hex(0xffa726), 0);
    lv_obj_set_style_text_font(lblR, &lv_font_montserrat_10, 0);
    lv_obj_align(lblR, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    homeVuBarR = lv_bar_create(vuBox);
    lv_obj_set_size(homeVuBarR, 108, 10);
    lv_obj_align(homeVuBarR, LV_ALIGN_BOTTOM_LEFT, 14, -3);
    lv_bar_set_range(homeVuBarR, 0, 255);
    lv_obj_set_style_bg_color(homeVuBarR, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(homeVuBarR, lv_color_hex(0xffa726), LV_PART_INDICATOR);

    // =========================================================================
    // 3. MEDIA & QUICK SETTINGS CONTROL CARD
    // =========================================================================
    lv_obj_t* gaiaCard = lv_obj_create(operationalScreen);
    lv_obj_set_size(gaiaCard, 308, 44);
    lv_obj_align(gaiaCard, LV_ALIGN_TOP_MID, 0, 96);
    ui_apply_card_style(gaiaCard);

    btnBtPower = lv_btn_create(gaiaCard);
    lv_obj_set_size(btnBtPower, 36, 32);
    lv_obj_align(btnBtPower, LV_ALIGN_LEFT_MID, 2, 0);
    ui_apply_btn_style(btnBtPower, UI_COLOR_PRIMARY);

    lblBtPower = lv_label_create(btnBtPower);
    lv_label_set_text(lblBtPower, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(lblBtPower, lv_color_hex(0x000000), 0);
    lv_obj_center(lblBtPower);

    lv_obj_add_event_cb(btnBtPower, [](lv_event_t* e) {
        bool nextState = !isBtEnabledState;
        char cmdBuf[64];
        snprintf(cmdBuf, sizeof(cmdBuf), "{\"type\":\"cmd\",\"cmd\":{\"bt\":%s}}", nextState ? "true" : "false");
        send_uart_command(cmdBuf);
    }, LV_EVENT_CLICKED, NULL);

    static const struct { const char* symbol; const char* gaia_cmd; } mediaBtns[] = {
        {LV_SYMBOL_PREV,  "{\"type\":\"cmd\",\"cmd\":{\"prev\":true}}"},
        {LV_SYMBOL_PLAY,  "{\"type\":\"cmd\",\"cmd\":{\"play\":true}}"},
        {LV_SYMBOL_NEXT,  "{\"type\":\"cmd\",\"cmd\":{\"next\":true}}"},
        {LV_SYMBOL_MINUS, "{\"type\":\"cmd\",\"cmd\":{\"voldown\":true}}"},
        {LV_SYMBOL_PLUS,  "{\"type\":\"cmd\",\"cmd\":{\"volup\":true}}"}
    };

    for (int i = 0; i < 5; i++) {
        lv_obj_t* btn = lv_btn_create(gaiaCard);
        lv_obj_set_size(btn, 38, 32);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, i * 44 + 48, 0);
        ui_apply_btn_style(btn, UI_COLOR_SURFACE_LIGHT);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, mediaBtns[i].symbol);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_MAIN, 0);
        lv_obj_center(lbl);

        const char* cmdStr = mediaBtns[i].gaia_cmd;
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            const char* c = (const char*)lv_event_get_user_data(e);
            send_uart_command(c);
        }, LV_EVENT_CLICKED, (void*)cmdStr);
    }

    // =========================================================================
    // 4. ANDROID APP GRID TILES (NES, USB DAC, MUSIC, USB MSC, SETTINGS)
    // =========================================================================
    lv_obj_t* btnNes = lv_btn_create(operationalScreen);
    lv_obj_set_size(btnNes, 58, 38);
    lv_obj_align(btnNes, LV_ALIGN_TOP_LEFT, 4, 146);
    ui_apply_btn_style(btnNes, UI_COLOR_PRIMARY);

    lv_obj_t* lblNes = lv_label_create(btnNes);
    lv_label_set_text(lblNes, "NES");
    lv_obj_set_style_text_color(lblNes, lv_color_hex(0x000000), 0);
    lv_obj_center(lblNes);

    lv_obj_add_event_cb(btnNes, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::NES_MENU);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnDac = lv_btn_create(operationalScreen);
    lv_obj_set_size(btnDac, 60, 38);
    lv_obj_align(btnDac, LV_ALIGN_TOP_LEFT, 66, 146);
    ui_apply_btn_style(btnDac, UI_COLOR_SECONDARY);

    lv_obj_t* lblDac = lv_label_create(btnDac);
    lv_label_set_text(lblDac, "DAC");
    lv_obj_set_style_text_color(lblDac, lv_color_hex(0x000000), 0);
    lv_obj_center(lblDac);

    lv_obj_add_event_cb(btnDac, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::USB_DAC);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnMusic = lv_btn_create(operationalScreen);
    lv_obj_set_size(btnMusic, 60, 38);
    lv_obj_align(btnMusic, LV_ALIGN_TOP_LEFT, 130, 146);
    ui_apply_btn_style(btnMusic, UI_COLOR_ACCENT_PURPLE);

    lv_obj_t* lblMusic = lv_label_create(btnMusic);
    lv_label_set_text(lblMusic, "MUSIC");
    lv_obj_set_style_text_color(lblMusic, lv_color_hex(0xffffff), 0);
    lv_obj_center(lblMusic);

    lv_obj_add_event_cb(btnMusic, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::MUSIC_PLAYER);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnMsc = lv_btn_create(operationalScreen);
    lv_obj_set_size(btnMsc, 60, 38);
    lv_obj_align(btnMsc, LV_ALIGN_TOP_LEFT, 194, 146);
    ui_apply_btn_style(btnMsc, lv_color_hex(0x00838f));

    lv_obj_t* lblMsc = lv_label_create(btnMsc);
    lv_label_set_text(lblMsc, "MSC");
    lv_obj_set_style_text_color(lblMsc, lv_color_hex(0xffffff), 0);
    lv_obj_center(lblMsc);

    lv_obj_add_event_cb(btnMsc, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::USB_MSC);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnSettingsApp = lv_btn_create(operationalScreen);
    lv_obj_set_size(btnSettingsApp, 60, 38);
    lv_obj_align(btnSettingsApp, LV_ALIGN_TOP_LEFT, 256, 146);
    ui_apply_btn_style(btnSettingsApp, UI_COLOR_SURFACE_LIGHT);

    lv_obj_t* lblSettingsApp = lv_label_create(btnSettingsApp);
    lv_label_set_text(lblSettingsApp, "SET");
    lv_obj_center(lblSettingsApp);

    lv_obj_add_event_cb(btnSettingsApp, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::SETTINGS_APP);
    }, LV_EVENT_CLICKED, NULL);

    // =========================================================================
    // 5. ANDROID PILL BOTTOM NAVIGATION BAR
    // =========================================================================
    lv_obj_t* navBar = lv_obj_create(operationalScreen);
    lv_obj_set_size(navBar, 320, 36);
    lv_obj_align(navBar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(navBar, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(navBar, 0, 0);
    lv_obj_set_style_radius(navBar, 0, 0);

    lv_obj_t* btnOff = lv_btn_create(navBar);
    lv_obj_set_size(btnOff, 110, 28);
    lv_obj_align(btnOff, LV_ALIGN_LEFT_MID, 6, 0);
    ui_apply_btn_style(btnOff, UI_COLOR_DANGER);

    lv_obj_t* lblOff = lv_label_create(btnOff);
    lv_label_set_text(lblOff, LV_SYMBOL_POWER " POWER");
    lv_obj_center(lblOff);

    lv_obj_add_event_cb(btnOff, [](lv_event_t* e) {
        send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"power\":false}}");
        panel_ui_switch_screen(ScreenState::STANDBY);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnStandby = lv_btn_create(navBar);
    lv_obj_set_size(btnStandby, 120, 28);
    lv_obj_align(btnStandby, LV_ALIGN_RIGHT_MID, -6, 0);
    ui_apply_btn_style(btnStandby, lv_color_hex(0x37474f));

    lv_obj_t* lblStandby = lv_label_create(btnStandby);
    lv_label_set_text(lblStandby, LV_SYMBOL_PAUSE " STANDBY");
    lv_obj_center(lblStandby);

    lv_obj_add_event_cb(btnStandby, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::STANDBY);
    }, LV_EVENT_CLICKED, NULL);
}

void panel_ui_update_telemetry() {
    if (labelTime) lv_label_set_text(labelTime, currentTimeStr);
    if (labelVoltages) {
        char buf[64];
        snprintf(buf, sizeof(buf), "STBY: %.1fV | VDD: %.1fV", stbyVoltage, vddVoltage);
        lv_label_set_text(labelVoltages, buf);
    }
    if (labelInputBadge) {
        lv_label_set_text(labelInputBadge, isBtInputActive ? "[ BT ]" : "[ AUX ]");
        lv_obj_set_style_text_color(labelInputBadge, isBtInputActive ? UI_COLOR_PRIMARY : UI_COLOR_SECONDARY, 0);
    }

    if (btnBtPower && lblBtPower) {
        if (isBtEnabledState) {
            lv_obj_set_style_bg_color(btnBtPower, UI_COLOR_PRIMARY, 0);
            lv_obj_set_style_text_color(lblBtPower, lv_color_hex(0x000000), 0);
        } else {
            lv_obj_set_style_bg_color(btnBtPower, lv_color_hex(0x424242), 0);
            lv_obj_set_style_text_color(lblBtPower, lv_color_hex(0x757575), 0);
        }
    }

    bool hasActiveLink = isUartLinkOk && !isIndependentBypassMode;

    if (homeVuBarL) lv_bar_set_value(homeVuBarL, hasActiveLink ? telemetryVuL : 0, LV_ANIM_OFF);
    if (homeVuBarR) lv_bar_set_value(homeVuBarR, hasActiveLink ? telemetryVuR : 0, LV_ANIM_OFF);

    for (uint8_t i = 0; i < kNumHomeSpecBars; i++) {
        if (homeSpecBars[i]) {
            uint8_t specVal = (hasActiveLink && i < telemetryBandsLen) ? telemetryBands[i] : 0;
            lv_bar_set_value(homeSpecBars[i], specVal, LV_ANIM_OFF);
        }
    }

    if (labelNoLinkIndicator) {
        if (isIndependentBypassMode || (!isUartLinkOk && !linkBypassed)) {
            lv_label_set_text(labelNoLinkIndicator, "[ BYPASS ]");
            lv_obj_clear_flag(labelNoLinkIndicator, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(labelNoLinkIndicator, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
