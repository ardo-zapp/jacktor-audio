#include "panel_ui.h"
#include "panel_main.h"

lv_obj_t* standbyScreen = nullptr;
lv_obj_t* bootTerminalScreen = nullptr;
lv_obj_t* bootTerminalArea = nullptr;

void append_boot_log(const char* msg) {
    if (!bootTerminalArea || !msg) return;
    std::string text = msg;

    if (text.find("PASS") != std::string::npos || text.find("OK") != std::string::npos) {
        text = "#00ff00 " + text + "#";
    } else if (text.find("FAIL") != std::string::npos || text.find("ERROR") != std::string::npos) {
        text = "#ff0000 " + text + "#";
    } else if (text.find("WARN") != std::string::npos) {
        text = "#ffff00 " + text + "#";
    } else if (text.find("[BOOT]") != std::string::npos) {
        text = "#00ffff " + text + "#";
    }

    lv_textarea_add_text(bootTerminalArea, text.c_str());
    lv_textarea_add_text(bootTerminalArea, "\n");
}

void create_standby_screen() {
    standbyScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(standbyScreen, lv_color_hex(0x121212), 0);

    labelTime = lv_label_create(standbyScreen);
    lv_obj_set_style_text_font(labelTime, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(labelTime, lv_color_hex(0x00e5ff), 0);
    lv_label_set_text(labelTime, currentTimeStr);
    lv_obj_align(labelTime, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t* btnPower = lv_btn_create(standbyScreen);
    lv_obj_set_size(btnPower, 140, 50);
    lv_obj_align(btnPower, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btnPower, lv_color_hex(0x00c853), 0);

    lv_obj_t* labelBtn = lv_label_create(btnPower);
    lv_label_set_text(labelBtn, LV_SYMBOL_POWER " POWER ON");
    lv_obj_center(labelBtn);

    lv_obj_add_event_cb(btnPower, [](lv_event_t* e) {
        send_uart_command("{\"cmd\": \"power_on\"}");
        panel_ui_switch_screen(ScreenState::SELF_TEST);
    }, LV_EVENT_CLICKED, NULL);
}

void create_boot_terminal_screen() {
    bootTerminalScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(bootTerminalScreen, lv_color_hex(0x000000), 0);

    lv_obj_t* title = lv_label_create(bootTerminalScreen);
    lv_label_set_text(title, "HARDWARE SELF-TEST DIAGNOSTICS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);

    bootTerminalArea = lv_textarea_create(bootTerminalScreen);
    lv_obj_set_size(bootTerminalArea, 300, 160);
    lv_obj_align(bootTerminalArea, LV_ALIGN_TOP_LEFT, 10, 32);
    lv_obj_set_style_bg_color(bootTerminalArea, lv_color_hex(0x050505), 0);
    lv_obj_set_style_text_color(bootTerminalArea, lv_color_hex(0x00ff00), 0);
    lv_label_set_recolor(bootTerminalArea, true);
    lv_textarea_set_text(bootTerminalArea, "[BOOT] Initializing Hardware Diagnostic Terminal...\n");

    btnSilenceAlarm = lv_btn_create(bootTerminalScreen);
    lv_obj_set_size(btnSilenceAlarm, 180, 36);
    lv_obj_align(btnSilenceAlarm, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
    lv_obj_set_style_bg_color(btnSilenceAlarm, lv_color_hex(0xd50000), 0);
    lv_obj_add_flag(btnSilenceAlarm, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* lblSilence = lv_label_create(btnSilenceAlarm);
    lv_label_set_text(lblSilence, "SILENCE & RETURN");
    lv_obj_center(lblSilence);

    lv_obj_add_event_cb(btnSilenceAlarm, [](lv_event_t* e) {
        send_uart_command("{\"cmd\": \"power_off\"}");
        panel_ui_switch_screen(ScreenState::STANDBY);
    }, LV_EVENT_CLICKED, NULL);
}
