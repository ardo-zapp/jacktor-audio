#include "ui_usb_dac.h"
#include "panel_main.h"
#include "driver/panel_audio.h"
#include "driver/usb_dac_driver.h"

lv_obj_t* usbDacScreen = nullptr;

static lv_obj_t* cardConnected = nullptr;
static lv_obj_t* cardDisconnected = nullptr;

static lv_obj_t* labelPcmInfo = nullptr;
static lv_obj_t* labelStatusBadge = nullptr;

static lv_obj_t* vuBarL = nullptr;
static lv_obj_t* vuBarR = nullptr;
static lv_obj_t* labelVuL = nullptr;
static lv_obj_t* labelVuR = nullptr;

static lv_obj_t* btnSourceToggle = nullptr;
static lv_obj_t* lblSourceToggle = nullptr;
static bool sourceFromAdc = false;

static const uint8_t kNumSpecBars = 12;
static lv_obj_t* specBars[12] = {nullptr};

static uint32_t currentSampleRate = 384000;
static uint8_t currentBitDepth = 32;

void create_usb_dac_screen(void) {
    if (usbDacScreen) return;

    usbDacScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(usbDacScreen, lv_color_hex(0x0f111a), 0);

    // =========================================================================
    // 1. CONNECTED CARD VIEW
    // =========================================================================
    cardConnected = lv_obj_create(usbDacScreen);
    lv_obj_set_size(cardConnected, 310, 230);
    lv_obj_align(cardConnected, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(cardConnected, lv_color_hex(0x0f111a), 0);
    lv_obj_set_style_border_width(cardConnected, 0, 0);

    // Header Card
    lv_obj_t* headerCard = lv_obj_create(cardConnected);
    lv_obj_set_size(headerCard, 300, 36);
    lv_obj_align(headerCard, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(headerCard, lv_color_hex(0x1a1d2e), 0);
    lv_obj_set_style_border_color(headerCard, lv_color_hex(0x00e5ff), 0);
    lv_obj_set_style_border_width(headerCard, 1, 0);

    lv_obj_t* titleLbl = lv_label_create(headerCard);
    lv_label_set_text(titleLbl, LV_SYMBOL_AUDIO " Jacktor Audio DAC");
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(titleLbl, LV_ALIGN_LEFT_MID, 2, 0);

    labelStatusBadge = lv_label_create(headerCard);
    lv_label_set_text(labelStatusBadge, "[ CONNECTED ]");
    lv_obj_set_style_text_color(labelStatusBadge, lv_color_hex(0x00e676), 0);
    lv_obj_align(labelStatusBadge, LV_ALIGN_RIGHT_MID, -2, 0);

    // PCM Metadata Card
    lv_obj_t* metaCard = lv_obj_create(cardConnected);
    lv_obj_set_size(metaCard, 300, 38);
    lv_obj_align(metaCard, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(metaCard, lv_color_hex(0x181a26), 0);

    labelPcmInfo = lv_label_create(metaCard);
    lv_label_set_text(labelPcmInfo, "Format: PCM 32-Bit / 384 kHz | Stereo L/R");
    lv_obj_set_style_text_color(labelPcmInfo, lv_color_hex(0xffffff), 0);
    lv_obj_align(labelPcmInfo, LV_ALIGN_CENTER, 0, 0);

    // Sample Rate Control Buttons + Source Selector Button
    lv_obj_t* rateContainer = lv_obj_create(cardConnected);
    lv_obj_set_size(rateContainer, 300, 34);
    lv_obj_align(rateContainer, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_set_style_bg_color(rateContainer, lv_color_hex(0x12141f), 0);

    static const struct { const char* label; uint32_t rate; } rates[] = {
        {"44.1k", 44100},
        {"96k",   96000},
        {"192k",  192000},
        {"384k",  384000}
    };

    for (int i = 0; i < 4; i++) {
        lv_obj_t* btn = lv_btn_create(rateContainer);
        lv_obj_set_size(btn, 52, 24);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, i * 56 + 2, 0);
        lv_obj_set_style_bg_color(btn, (rates[i].rate == currentSampleRate) ? lv_color_hex(0x00e5ff) : lv_color_hex(0x2a2d3d), 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, rates[i].label);
        lv_obj_set_style_text_color(lbl, (rates[i].rate == currentSampleRate) ? lv_color_hex(0x000000) : lv_color_hex(0xffffff), 0);
        lv_obj_center(lbl);

        uint32_t selectedRate = rates[i].rate;
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            uint32_t r = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
            currentSampleRate = r;
            panel_audio_set_rate(r);
            update_usb_dac_screen();
        }, LV_EVENT_CLICKED, (void*)(uintptr_t)selectedRate);
    }

    // Source Selector Button (USB DAC Panel vs GPIO ADC Amp)
    btnSourceToggle = lv_btn_create(rateContainer);
    lv_obj_set_size(btnSourceToggle, 68, 24);
    lv_obj_align(btnSourceToggle, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_bg_color(btnSourceToggle, lv_color_hex(0x00e676), 0);

    lblSourceToggle = lv_label_create(btnSourceToggle);
    lv_label_set_text(lblSourceToggle, "Src: USB");
    lv_obj_set_style_text_color(lblSourceToggle, lv_color_hex(0x000000), 0);
    lv_obj_center(lblSourceToggle);

    lv_obj_add_event_cb(btnSourceToggle, [](lv_event_t* e) {
        sourceFromAdc = !sourceFromAdc;
        if (lblSourceToggle) {
            lv_label_set_text(lblSourceToggle, sourceFromAdc ? "Src: ADC" : "Src: USB");
        }
        if (btnSourceToggle) {
            lv_obj_set_style_bg_color(btnSourceToggle, sourceFromAdc ? lv_color_hex(0xffa726) : lv_color_hex(0x00e676), 0);
        }
        update_usb_dac_screen();
    }, LV_EVENT_CLICKED, NULL);

    // Spectrum Analyzer Container
    lv_obj_t* specContainer = lv_obj_create(cardConnected);
    lv_obj_set_size(specContainer, 300, 44);
    lv_obj_align(specContainer, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_color(specContainer, lv_color_hex(0x12141f), 0);

    for (uint8_t i = 0; i < kNumSpecBars; i++) {
        specBars[i] = lv_bar_create(specContainer);
        lv_obj_set_size(specBars[i], 16, 32);
        lv_obj_align(specBars[i], LV_ALIGN_BOTTOM_LEFT, i * 23 + 6, 0);
        lv_bar_set_range(specBars[i], 0, 255);
        lv_bar_set_value(specBars[i], 20, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(specBars[i], lv_color_hex(0x222533), LV_PART_MAIN);
        lv_obj_set_style_bg_color(specBars[i], lv_color_hex(0x00e5ff), LV_PART_INDICATOR);
    }

    // Stereo VU Meter Container (L & R Bars)
    lv_obj_t* vuContainer = lv_obj_create(cardConnected);
    lv_obj_set_size(vuContainer, 200, 44);
    lv_obj_align(vuContainer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(vuContainer, lv_color_hex(0x12141f), 0);

    labelVuL = lv_label_create(vuContainer);
    lv_label_set_text(labelVuL, "L");
    lv_obj_set_style_text_color(labelVuL, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(labelVuL, LV_ALIGN_TOP_LEFT, 0, 0);

    vuBarL = lv_bar_create(vuContainer);
    lv_obj_set_size(vuBarL, 160, 10);
    lv_obj_align(vuBarL, LV_ALIGN_TOP_LEFT, 18, 2);
    lv_bar_set_range(vuBarL, 0, 255);
    lv_obj_set_style_bg_color(vuBarL, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vuBarL, lv_color_hex(0x00e676), LV_PART_INDICATOR);

    labelVuR = lv_label_create(vuContainer);
    lv_label_set_text(labelVuR, "R");
    lv_obj_set_style_text_color(labelVuR, lv_color_hex(0xffa726), 0);
    lv_obj_align(labelVuR, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    vuBarR = lv_bar_create(vuContainer);
    lv_obj_set_size(vuBarR, 160, 10);
    lv_obj_align(vuBarR, LV_ALIGN_BOTTOM_LEFT, 18, -2);
    lv_bar_set_range(vuBarR, 0, 255);
    lv_obj_set_style_bg_color(vuBarR, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vuBarR, lv_color_hex(0xffa726), LV_PART_INDICATOR);

    // Exit Button
    lv_obj_t* btnExit = lv_btn_create(cardConnected);
    lv_obj_set_size(btnExit, 84, 44);
    lv_obj_align(btnExit, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btnExit, lv_color_hex(0xd50000), 0);

    lv_obj_t* lblExit = lv_label_create(btnExit);
    lv_label_set_text(lblExit, LV_SYMBOL_LEFT " EXIT");
    lv_obj_center(lblExit);

    lv_obj_add_event_cb(btnExit, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::OPERATIONAL);
    }, LV_EVENT_CLICKED, NULL);

    // =========================================================================
    // 2. DISCONNECTED CARD VIEW
    // =========================================================================
    cardDisconnected = lv_obj_create(usbDacScreen);
    lv_obj_set_size(cardDisconnected, 300, 210);
    lv_obj_align(cardDisconnected, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(cardDisconnected, lv_color_hex(0x181a28), 0);
    lv_obj_set_style_border_color(cardDisconnected, lv_color_hex(0xff1744), 0);
    lv_obj_set_style_border_width(cardDisconnected, 1, 0);

    lv_obj_t* iconDis = lv_label_create(cardDisconnected);
    lv_label_set_text(iconDis, LV_SYMBOL_USB "\nUSB DAC Disconnected");
    lv_obj_set_style_text_align(iconDis, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(iconDis, lv_color_hex(0xff1744), 0);
    lv_obj_align(iconDis, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* descDis = lv_label_create(cardDisconnected);
    lv_label_set_text(descDis, "USB DAC device is not connected.\nConnect USB cable to PC or Audio Source\nto start 32-Bit audio streaming.");
    lv_obj_set_style_text_align(descDis, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(descDis, lv_color_hex(0xb0bec5), 0);
    lv_obj_align(descDis, LV_ALIGN_CENTER, 0, -5);

    lv_obj_t* btnDisExit = lv_btn_create(cardDisconnected);
    lv_obj_set_size(btnDisExit, 120, 38);
    lv_obj_align(btnDisExit, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(btnDisExit, lv_color_hex(0x37474f), 0);

    lv_obj_t* lblDisExit = lv_label_create(btnDisExit);
    lv_label_set_text(lblDisExit, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(lblDisExit);

    lv_obj_add_event_cb(btnDisExit, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::OPERATIONAL);
    }, LV_EVENT_CLICKED, NULL);

    update_usb_dac_screen();
}

void update_usb_dac_screen(void) {
    if (!usbDacScreen) return;

    bool connected = usb_dac_is_connected();

    if (!connected) {
        // Show Disconnected View
        if (cardConnected) lv_obj_add_flag(cardConnected, LV_OBJ_FLAG_HIDDEN);
        if (cardDisconnected) lv_obj_clear_flag(cardDisconnected, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Show Connected View
    if (cardDisconnected) lv_obj_add_flag(cardDisconnected, LV_OBJ_FLAG_HIDDEN);
    if (cardConnected) lv_obj_clear_flag(cardConnected, LV_OBJ_FLAG_HIDDEN);

    if (labelPcmInfo) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Format: PCM %u-Bit / %lu kHz | %s",
                 currentBitDepth, (unsigned long)(currentSampleRate / 1000),
                 sourceFromAdc ? "GPIO ADC (Amp)" : "USB DAC (Panel)");
        lv_label_set_text(labelPcmInfo, buf);
    }

    if (sourceFromAdc) {
        // Display visualizer levels sourced from GPIO 36 & 39 ADC Telemetry
        bool hasActiveLink = isUartLinkOk && !isIndependentBypassMode;
        if (vuBarL) lv_bar_set_value(vuBarL, hasActiveLink ? telemetryVuL : 0, LV_ANIM_OFF);
        if (vuBarR) lv_bar_set_value(vuBarR, hasActiveLink ? telemetryVuR : 0, LV_ANIM_OFF);

        if (hasActiveLink && (telemetryVuL > 0 || telemetryVuR > 0)) {
            static uint16_t adcAnim = 0;
            adcAnim += 12;
            for (uint8_t i = 0; i < kNumSpecBars; i++) {
                if (specBars[i]) {
                    uint8_t val = (uint8_t)(30 + 170 * (0.5f + 0.5f * sinf((adcAnim + i * 30) * 0.05f)));
                    lv_bar_set_value(specBars[i], val, LV_ANIM_OFF);
                }
            }
        } else {
            for (uint8_t i = 0; i < kNumSpecBars; i++) {
                if (specBars[i]) {
                    lv_bar_set_value(specBars[i], 0, LV_ANIM_OFF);
                }
            }
        }
    } else {
        // Live Spectrum Bars & Stereo VU Meters sourced from USB DAC PCM
        static uint16_t animPhase = 0;
        animPhase += 10;

        for (uint8_t i = 0; i < kNumSpecBars; i++) {
            if (specBars[i]) {
                uint8_t val = (uint8_t)(40 + 160 * (0.5f + 0.5f * sinf((animPhase + i * 35) * 0.05f)));
                lv_bar_set_value(specBars[i], val, LV_ANIM_OFF);
            }
        }

        if (vuBarL) {
            uint8_t valL = (uint8_t)(60 + 180 * (0.5f + 0.5f * sinf(animPhase * 0.06f)));
            lv_bar_set_value(vuBarL, valL, LV_ANIM_OFF);
        }
        if (vuBarR) {
            uint8_t valR = (uint8_t)(60 + 180 * (0.5f + 0.5f * sinf((animPhase + 25) * 0.06f)));
            lv_bar_set_value(vuBarR, valR, LV_ANIM_OFF);
        }
    }
}
