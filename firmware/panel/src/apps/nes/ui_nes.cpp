#include "panel_ui.h"
#include "panel_theme.h"
#include "panel_main.h"
#include "nes_manager.h"
#include "nes_bgm.h"
#include "nes_config.h"

LV_IMG_DECLARE(nes_mascot);

lv_obj_t* nesLauncherScreen = nullptr;
lv_obj_t* nesRomListContainer = nullptr;
lv_obj_t* nesSettingsScreen = nullptr;

void refresh_nes_rom_list() {
    if (!nesRomListContainer) return;
    lv_obj_clean(nesRomListContainer);

    std::vector<std::string> roms = nes_manager_scan_roms();

    if (roms.empty()) {
        lv_obj_t* card = lv_obj_create(nesRomListContainer);
        lv_obj_set_size(card, 280, 160);
        lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
        ui_apply_card_style(card);

        lv_obj_t* icon = lv_label_create(card);
        lv_label_set_text(icon, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_color(icon, lv_color_hex(0xffb74d), 0);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t* lblTitle = lv_label_create(card);
        lv_label_set_text(lblTitle, "No .nes Games Found");
        lv_obj_set_style_text_color(lblTitle, UI_COLOR_TEXT_MAIN, 0);
        lv_obj_align(lblTitle, LV_ALIGN_TOP_MID, 0, 32);

        lv_obj_t* lblDesc = lv_label_create(card);
        lv_label_set_text(lblDesc, "Please insert an SD card with .nes files\ninside the /sdcard/NES directory.");
        lv_obj_set_style_text_color(lblDesc, UI_COLOR_TEXT_MUTED, 0);
        lv_obj_set_style_text_align(lblDesc, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lblDesc, LV_ALIGN_TOP_MID, 0, 56);

        lv_obj_t* btnRefresh = lv_btn_create(card);
        lv_obj_set_size(btnRefresh, 160, 36);
        lv_obj_align(btnRefresh, LV_ALIGN_BOTTOM_MID, 0, -10);
        ui_apply_btn_style(btnRefresh, UI_COLOR_PRIMARY);

        lv_obj_t* lblRef = lv_label_create(btnRefresh);
        lv_label_set_text(lblRef, LV_SYMBOL_REFRESH " REFRESH SD");
        lv_obj_set_style_text_color(lblRef, lv_color_hex(0x000000), 0);
        lv_obj_center(lblRef);

        lv_obj_add_event_cb(btnRefresh, [](lv_event_t* e) {
            refresh_nes_rom_list();
        }, LV_EVENT_CLICKED, NULL);
    } else {
        int y_offset = 0;
        for (const auto& rom : roms) {
            lv_obj_t* item = lv_obj_create(nesRomListContainer);
            lv_obj_set_size(item, 300, 42);
            lv_obj_align(item, LV_ALIGN_TOP_MID, 0, y_offset);
            ui_apply_card_style(item);

            lv_obj_t* lblIcon = lv_label_create(item);
            lv_label_set_text(lblIcon, LV_SYMBOL_AUDIO);
            lv_obj_set_style_text_color(lblIcon, UI_COLOR_PRIMARY, 0);
            lv_obj_align(lblIcon, LV_ALIGN_LEFT_MID, 8, 0);

            lv_obj_t* lblName = lv_label_create(item);
            lv_label_set_text(lblName, rom.c_str());
            lv_obj_set_style_text_color(lblName, UI_COLOR_TEXT_MAIN, 0);
            lv_obj_align(lblName, LV_ALIGN_LEFT_MID, 32, 0);

            lv_obj_t* btnPlay = lv_btn_create(item);
            lv_obj_set_size(btnPlay, 64, 28);
            lv_obj_align(btnPlay, LV_ALIGN_RIGHT_MID, -4, 0);
            ui_apply_btn_style(btnPlay, UI_COLOR_SECONDARY);

            lv_obj_t* lblPlay = lv_label_create(btnPlay);
            lv_label_set_text(lblPlay, LV_SYMBOL_PLAY " PLAY");
            lv_obj_set_style_text_color(lblPlay, lv_color_hex(0x000000), 0);
            lv_obj_center(lblPlay);

            std::string full_path = "/sdcard/NES/" + rom;
            char* path_copy = strdup(full_path.c_str());

            lv_obj_add_event_cb(btnPlay, [](lv_event_t* e) {
                char* path = (char*)lv_event_get_user_data(e);
                if (path) {
                    nes_bgm_pause();
                    nes_manager_start_game(path);
                    panel_ui_switch_screen(ScreenState::NES_GAME);
                }
            }, LV_EVENT_CLICKED, path_copy);

            lv_obj_add_event_cb(btnPlay, [](lv_event_t* e) {
                char* path = (char*)lv_event_get_user_data(e);
                if (path) {
                    free(path);
                }
            }, LV_EVENT_DELETE, path_copy);

            y_offset += 48;
        }
    }
}

void create_nes_menu_screen() {
    nesLauncherScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(nesLauncherScreen, UI_COLOR_BG, 0);

    // Header bar
    lv_obj_t* header = lv_obj_create(nesLauncherScreen);
    lv_obj_set_size(header, 320, 36);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);

    lv_obj_t* btnBack = lv_btn_create(header);
    lv_obj_set_size(btnBack, 70, 28);
    lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 4, 0);
    ui_apply_btn_style(btnBack, UI_COLOR_SURFACE);
    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(lblBack);

    lv_obj_add_event_cb(btnBack, [](lv_event_t* e) {
        nes_bgm_pause();
        panel_ui_switch_screen(ScreenState::OPERATIONAL);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "NES EMULATOR");
    lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t* btnSettings = lv_btn_create(header);
    lv_obj_set_size(btnSettings, 32, 28);
    lv_obj_align(btnSettings, LV_ALIGN_RIGHT_MID, -4, 0);
    ui_apply_btn_style(btnSettings, UI_COLOR_SURFACE);
    lv_obj_t* lblSettings = lv_label_create(btnSettings);
    lv_label_set_text(lblSettings, LV_SYMBOL_SETTINGS);
    lv_obj_center(lblSettings);

    lv_obj_add_event_cb(btnSettings, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::NES_SETTINGS);
    }, LV_EVENT_CLICKED, NULL);

    // List Container
    nesRomListContainer = lv_obj_create(nesLauncherScreen);
    lv_obj_set_size(nesRomListContainer, 320, 204);
    lv_obj_align(nesRomListContainer, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(nesRomListContainer, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(nesRomListContainer, 0, 0);
    lv_obj_set_style_pad_all(nesRomListContainer, 8, 0);

    refresh_nes_rom_list();

    // Floating NES Mascot Character (Ardo Zapp)
    lv_obj_t* mascotImg = lv_img_create(nesLauncherScreen);
    lv_img_set_src(mascotImg, &nes_mascot);
    lv_obj_align(mascotImg, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
    lv_obj_clear_flag(mascotImg, LV_OBJ_FLAG_CLICKABLE);
}

void create_nes_settings_screen() {
    nesSettingsScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(nesSettingsScreen, UI_COLOR_BG, 0);

    // Header bar
    lv_obj_t* header = lv_obj_create(nesSettingsScreen);
    lv_obj_set_size(header, 320, 36);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, UI_COLOR_SURFACE_LIGHT, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);

    lv_obj_t* btnBack = lv_btn_create(header);
    lv_obj_set_size(btnBack, 70, 28);
    lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 4, 0);
    ui_apply_btn_style(btnBack, UI_COLOR_SURFACE);
    lv_obj_t* lblBack = lv_label_create(btnBack);
    lv_label_set_text(lblBack, LV_SYMBOL_LEFT " BACK");
    lv_obj_center(lblBack);

    lv_obj_add_event_cb(btnBack, [](lv_event_t* e) {
        panel_ui_switch_screen(ScreenState::NES_MENU);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* title = lv_label_create(header);
    lv_label_set_text(title, "CONTROLLER MAPPING");
    lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Settings Card
    lv_obj_t* card = lv_obj_create(nesSettingsScreen);
    lv_obj_set_size(card, 300, 190);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 42);
    ui_apply_card_style(card);

    lv_obj_t* lblInfo = lv_label_create(card);
    lv_label_set_text(lblInfo, "DualShock 2 USB Gamepad Mapping:");
    lv_obj_set_style_text_color(lblInfo, UI_COLOR_PRIMARY, 0);
    lv_obj_align(lblInfo, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* lblDetails = lv_label_create(card);
    lv_label_set_text(lblDetails, "NES A Button  : Cross (X)\nNES B Button  : Circle (O)\nNES Select    : SELECT\nNES Start     : START\nMovement      : D-Pad / Left Stick\nExit Hotkey   : SELECT + START (1s)");
    lv_obj_set_style_text_color(lblDetails, UI_COLOR_TEXT_MAIN, 0);
    lv_obj_align(lblDetails, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_t* btnReset = lv_btn_create(card);
    lv_obj_set_size(btnReset, 130, 32);
    lv_obj_align(btnReset, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    ui_apply_btn_style(btnReset, UI_COLOR_DANGER);
    lv_obj_t* lblReset = lv_label_create(btnReset);
    lv_label_set_text(lblReset, "RESET DEFAULTS");
    lv_obj_center(lblReset);

    lv_obj_add_event_cb(btnReset, [](lv_event_t* e) {
        nes_config_reset_defaults();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btnSave = lv_btn_create(card);
    lv_obj_set_size(btnSave, 130, 32);
    lv_obj_align(btnSave, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    ui_apply_btn_style(btnSave, UI_COLOR_SECONDARY);
    lv_obj_t* lblSave = lv_label_create(btnSave);
    lv_label_set_text(lblSave, "SAVE MAPPING");
    lv_obj_set_style_text_color(lblSave, lv_color_hex(0x000000), 0);
    lv_obj_center(lblSave);

    lv_obj_add_event_cb(btnSave, [](lv_event_t* e) {
        NesControllerConfig cfg = nes_config_get();
        nes_config_save(&cfg);
    }, LV_EVENT_CLICKED, NULL);
}
