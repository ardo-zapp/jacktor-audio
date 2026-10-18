#include "ui_msc_transfer.h"
#include "panel_theme.h"
#include "panel_main.h"
#include "driver/panel_hardware.h"
#include <cmath>

lv_obj_t* usbMscScreen = nullptr;

static lv_obj_t* mscStatusBadge = nullptr;
static lv_obj_t* mscCapLabel = nullptr;
static lv_obj_t* mscBarTransfer = nullptr;

void create_usb_msc_screen(void) {
    if (usbMscScreen) return;

    usbMscScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(usbMscScreen, UI_COLOR_BG, 0);

    // Header bar
    lv_obj_t* headerCard = lv_obj_create(usbMscScreen);
    lv_obj_set_size(headerCard, 310, 36);
    lv_obj_align(headerCard, LV_ALIGN_TOP_MID, 0, 4);
    ui_apply_card_style(headerCard);

    lv_obj_t* titleLbl = lv_label_create(headerCard);
    lv_label_set_text(titleLbl, LV_SYMBOL_USB " Mass Storage Transfer");
    lv_obj_set_style_text_color(titleLbl, UI_COLOR_PRIMARY, 0);
    lv_obj_align(titleLbl, LV_ALIGN_LEFT_MID, 4, 0);

    mscStatusBadge = lv_label_create(headerCard);
    lv_label_set_text(mscStatusBadge, "[ CONNECTED TO PC ]");
    lv_obj_set_style_text_color(mscStatusBadge, UI_COLOR_SECONDARY, 0);
    lv_obj_set_style_text_font(mscStatusBadge, &lv_font_montserrat_10, 0);
    lv_obj_align(mscStatusBadge, LV_ALIGN_RIGHT_MID, -4, 0);

    // Drive Info Card
    lv_obj_t* infoCard = lv_obj_create(usbMscScreen);
    lv_obj_set_size(infoCard, 310, 52);
    lv_obj_align(infoCard, LV_ALIGN_TOP_MID, 0, 44);
    ui_apply_card_style(infoCard);

    lv_obj_t* sdIcon = lv_label_create(infoCard);
    lv_label_set_text(sdIcon, LV_SYMBOL_DIRECTORY);
    lv_obj_set_style_text_color(sdIcon, UI_COLOR_PRIMARY, 0);
    lv_obj_align(sdIcon, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* sdTitle = lv_label_create(infoCard);
    lv_label_set_text(sdTitle, "SD Card Drive (Jacktor Storage)");
    lv_obj_set_style_text_color(sdTitle, UI_COLOR_TEXT_MAIN, 0);
    lv_obj_set_style_text_font(sdTitle, &lv_font_montserrat_12, 0);
    lv_obj_align(sdTitle, LV_ALIGN_TOP_LEFT, 32, 2);

    mscCapLabel = lv_label_create(infoCard);
    uint64_t total_b = 0, free_b = 0;
    if (panel_hardware_get_sd_info(&total_b, &free_b)) {
        float used_gb = (float)(total_b - free_b) / (1024.0f * 1024.0f * 1024.0f);
        float total_gb = (float)total_b / (1024.0f * 1024.0f * 1024.0f);
        char cbuf[80];
        snprintf(cbuf, sizeof(cbuf), "Used: %.1f GB / %.1f GB | FAT32 / exFAT", used_gb, total_gb);
        lv_label_set_text(mscCapLabel, cbuf);
    } else {
        lv_label_set_text(mscCapLabel, "SD Card Storage: Not Mounted");
    }
    lv_obj_set_style_text_color(mscCapLabel, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(mscCapLabel, &lv_font_montserrat_10, 0);
    lv_obj_align(mscCapLabel, LV_ALIGN_BOTTOM_LEFT, 32, -2);

    // Read/Write Activity Bar Card
    lv_obj_t* xferCard = lv_obj_create(usbMscScreen);
    lv_obj_set_size(xferCard, 310, 48);
    lv_obj_align(xferCard, LV_ALIGN_TOP_MID, 0, 100);
    ui_apply_card_style(xferCard);

    lv_obj_t* xferTitle = lv_label_create(xferCard);
    lv_label_set_text(xferTitle, "Active Read / Write Transfer Stream:");
    lv_obj_set_style_text_color(xferTitle, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_text_font(xferTitle, &lv_font_montserrat_10, 0);
    lv_obj_align(xferTitle, LV_ALIGN_TOP_LEFT, 4, 0);

    mscBarTransfer = lv_bar_create(xferCard);
    lv_obj_set_size(mscBarTransfer, 290, 10);
    lv_obj_align(mscBarTransfer, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_bar_set_range(mscBarTransfer, 0, 100);
    lv_bar_set_value(mscBarTransfer, 40, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(mscBarTransfer, lv_color_hex(0x222533), LV_PART_MAIN);
    lv_obj_set_style_bg_color(mscBarTransfer, UI_COLOR_SECONDARY, LV_PART_INDICATOR);

    // Safety Warning Card
    lv_obj_t* warnCard = lv_obj_create(usbMscScreen);
    lv_obj_set_size(warnCard, 310, 44);
    lv_obj_align(warnCard, LV_ALIGN_TOP_MID, 0, 152);
    ui_apply_card_style(warnCard);

    lv_obj_t* warnIcon = lv_label_create(warnCard);
    lv_label_set_text(warnIcon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(warnIcon, lv_color_hex(0xffa726), 0);
    lv_obj_align(warnIcon, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t* warnText = lv_label_create(warnCard);
    lv_label_set_text(warnText, "Safety Notice: Do not disconnect USB cable during\nfile transfers to prevent SD Card data corruption.");
    lv_obj_set_style_text_color(warnText, UI_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_font(warnText, &lv_font_montserrat_10, 0);
    lv_obj_align(warnText, LV_ALIGN_LEFT_MID, 28, 0);

    // Eject & Exit Button
    lv_obj_t* btnEject = lv_btn_create(usbMscScreen);
    lv_obj_set_size(btnEject, 180, 32);
    lv_obj_align(btnEject, LV_ALIGN_BOTTOM_MID, 0, -4);
    ui_apply_btn_style(btnEject, UI_COLOR_DANGER);

    lv_obj_t* lblEject = lv_label_create(btnEject);
    lv_label_set_text(lblEject, "EJECT & EXIT");
    lv_obj_set_style_text_font(lblEject, &lv_font_montserrat_12, 0);
    lv_obj_center(lblEject);

    lv_obj_add_event_cb(btnEject, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::OPERATIONAL);
    }, LV_EVENT_CLICKED, NULL);
}

void update_usb_msc_screen(void) {
    if (!usbMscScreen) return;

    static uint16_t xferAnim = 0;
    xferAnim += 12;
    if (mscBarTransfer) {
        uint8_t val = (uint8_t)(30 + 170 * (0.5f + 0.5f * sinf(xferAnim * 0.08f)));
        lv_bar_set_value(mscBarTransfer, val, LV_ANIM_OFF);
    }
}
