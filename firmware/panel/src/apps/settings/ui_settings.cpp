#include "panel_ui.h"
#include "panel_theme.h"
#include "panel_main.h"
#include "nes_manager.h"
#include "nes_config.h"
#include <vector>
#include <string>

lv_obj_t* settingsAppScreen = nullptr;

static lv_obj_t* wifiSsidTa = nullptr;
static lv_obj_t* wifiPassTa = nullptr;
static lv_obj_t* wifiStatusLbl = nullptr;

void create_settings_app_screen() {
    settingsAppScreen = lv_obj_create(NULL);
    if (!settingsAppScreen) return;
    lv_obj_set_style_bg_color(settingsAppScreen, UI_COLOR_BG, 0);

    // Header bar
    lv_obj_t* header = lv_obj_create(settingsAppScreen);
    if (header) {
        lv_obj_set_size(header, 320, 36);
        lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(header, UI_COLOR_SURFACE_LIGHT, 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_set_style_pad_all(header, 4, 0);

        lv_obj_t* btnBack = lv_btn_create(header);
        if (btnBack) {
            lv_obj_set_size(btnBack, 70, 28);
            lv_obj_align(btnBack, LV_ALIGN_LEFT_MID, 4, 0);
            ui_apply_btn_style(btnBack, UI_COLOR_SURFACE);
            lv_obj_t* lblBack = lv_label_create(btnBack);
            if (lblBack) {
                lv_label_set_text(lblBack, LV_SYMBOL_LEFT " BACK");
                lv_obj_center(lblBack);
            }

            lv_obj_add_event_cb(btnBack, [](lv_event_t* e) {
                panel_ui_switch_screen(ScreenState::OPERATIONAL);
            }, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t* title = lv_label_create(header);
        if (title) {
            lv_label_set_text(title, "SYSTEM SETTINGS");
            lv_obj_set_style_text_color(title, UI_COLOR_PRIMARY, 0);
            lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
        }
    }

    // Tabview for System Settings categories
    lv_obj_t* tabview = lv_tabview_create(settingsAppScreen, LV_DIR_TOP, 30);
    if (!tabview) return;
    lv_obj_set_size(tabview, 320, 204);
    lv_obj_align(tabview, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_bg_color(tabview, UI_COLOR_BG, 0);

    lv_obj_t* tabAmp = lv_tabview_add_tab(tabview, "AMP");
    lv_obj_t* tabWifi = lv_tabview_add_tab(tabview, "WIFI");
    lv_obj_t* tabDisp = lv_tabview_add_tab(tabview, "DISP");
    lv_obj_t* tabSd = lv_tabview_add_tab(tabview, "SD");
    lv_obj_t* tabPad = lv_tabview_add_tab(tabview, "PAD");
    lv_obj_t* tabInfo = lv_tabview_add_tab(tabview, "INFO");

    // -----------------------------------------------------------------
    // TAB 1: AMPLIFIER SETTINGS
    // -----------------------------------------------------------------
    if (tabAmp != NULL) {
        lv_obj_set_style_bg_color(tabAmp, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(tabAmp, 6, 0);

        if (isIndependentBypassMode || (!isUartLinkOk && !linkBypassed)) {
            lv_obj_t* lblBypassNote = lv_label_create(tabAmp);
            if (lblBypassNote) {
                lv_label_set_text(lblBypassNote, "[ AMP NOT CONNECTED - BYPASS MODE ]\nAmplifier settings are disabled.");
                lv_obj_set_style_text_color(lblBypassNote, UI_COLOR_DANGER, 0);
                lv_obj_align(lblBypassNote, LV_ALIGN_TOP_LEFT, 0, 0);
            }
        }

        lv_obj_t* btnSpkPwr = lv_btn_create(tabAmp);
        if (btnSpkPwr) {
            lv_obj_set_size(btnSpkPwr, 140, 32);
            lv_obj_align(btnSpkPwr, LV_ALIGN_TOP_LEFT, 0, 0);
            ui_apply_btn_style(btnSpkPwr, UI_COLOR_SECONDARY);
            lv_obj_t* lblSpkPwr = lv_label_create(btnSpkPwr);
            if (lblSpkPwr) {
                lv_label_set_text(lblSpkPwr, "SPEAKER PWR: ON");
                lv_obj_set_style_text_color(lblSpkPwr, lv_color_hex(0x000000), 0);
                lv_obj_center(lblSpkPwr);
            }

            lv_obj_add_event_cb(btnSpkPwr, [](lv_event_t* e) {
                send_uart_command("{\"cmd\": {\"spk_pwr\": true}}");
            }, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t* lblFanTitle = lv_label_create(tabAmp);
        if (lblFanTitle) {
            lv_label_set_text(lblFanTitle, "Cooling Fan Mode:");
            lv_obj_set_style_text_color(lblFanTitle, UI_COLOR_PRIMARY, 0);
            lv_obj_align(lblFanTitle, LV_ALIGN_TOP_LEFT, 0, 42);
        }

        lv_obj_t* btnQuiet = lv_btn_create(tabAmp);
        if (btnQuiet) {
            lv_obj_set_size(btnQuiet, 90, 28);
            lv_obj_align(btnQuiet, LV_ALIGN_TOP_LEFT, 0, 64);
            ui_apply_btn_style(btnQuiet, UI_COLOR_SURFACE_LIGHT);
            lv_obj_t* lblQ = lv_label_create(btnQuiet);
            if (lblQ) {
                lv_label_set_text(lblQ, "Quiet");
                lv_obj_center(lblQ);
            }

            lv_obj_add_event_cb(btnQuiet, [](lv_event_t* e) {
                send_uart_command("{\"cmd\": {\"fan_mode\": \"quiet\"}}");
            }, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t* btnNorm = lv_btn_create(tabAmp);
        if (btnNorm) {
            lv_obj_set_size(btnNorm, 90, 28);
            lv_obj_align(btnNorm, LV_ALIGN_TOP_LEFT, 98, 64);
            ui_apply_btn_style(btnNorm, UI_COLOR_PRIMARY);
            lv_obj_t* lblN = lv_label_create(btnNorm);
            if (lblN) {
                lv_label_set_text(lblN, "Normal");
                lv_obj_set_style_text_color(lblN, lv_color_hex(0x000000), 0);
                lv_obj_center(lblN);
            }

            lv_obj_add_event_cb(btnNorm, [](lv_event_t* e) {
                send_uart_command("{\"cmd\": {\"fan_mode\": \"normal\"}}");
            }, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t* btnPerf = lv_btn_create(tabAmp);
        if (btnPerf) {
            lv_obj_set_size(btnPerf, 90, 28);
            lv_obj_align(btnPerf, LV_ALIGN_TOP_LEFT, 196, 64);
            ui_apply_btn_style(btnPerf, UI_COLOR_SURFACE_LIGHT);
            lv_obj_t* lblP = lv_label_create(btnPerf);
            if (lblP) {
                lv_label_set_text(lblP, "Max");
                lv_obj_center(lblP);
            }

            lv_obj_add_event_cb(btnPerf, [](lv_event_t* e) {
                send_uart_command("{\"cmd\": {\"fan_mode\": \"performance\"}}");
            }, LV_EVENT_CLICKED, NULL);
        }
    }

    // -----------------------------------------------------------------
    // TAB 2: WI-FI & NTP RTC SYNC SETTINGS
    // -----------------------------------------------------------------
    if (tabWifi != NULL) {
        lv_obj_set_style_bg_color(tabWifi, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(tabWifi, 6, 0);

        wifiStatusLbl = lv_label_create(tabWifi);
        if (wifiStatusLbl) {
            lv_label_set_text(wifiStatusLbl, "Wi-Fi Status: Disconnected");
            lv_obj_set_style_text_color(wifiStatusLbl, lv_color_hex(0xffa726), 0);
            lv_obj_set_style_text_font(wifiStatusLbl, &lv_font_montserrat_10, 0);
            lv_obj_align(wifiStatusLbl, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        wifiSsidTa = lv_textarea_create(tabWifi);
        if (wifiSsidTa) {
            lv_textarea_set_placeholder_text(wifiSsidTa, "SSID / Network Name");
            lv_obj_set_size(wifiSsidTa, 180, 32);
            lv_obj_align(wifiSsidTa, LV_ALIGN_TOP_LEFT, 0, 18);
            lv_obj_set_style_radius(wifiSsidTa, 8, 0);
        }

        wifiPassTa = lv_textarea_create(tabWifi);
        if (wifiPassTa) {
            lv_textarea_set_placeholder_text(wifiPassTa, "Password");
            lv_textarea_set_password_mode(wifiPassTa, true);
            lv_obj_set_size(wifiPassTa, 180, 32);
            lv_obj_align(wifiPassTa, LV_ALIGN_TOP_LEFT, 0, 54);
            lv_obj_set_style_radius(wifiPassTa, 8, 0);
        }

        lv_obj_t* btnConnectWifi = lv_btn_create(tabWifi);
        if (btnConnectWifi) {
            lv_obj_set_size(btnConnectWifi, 100, 32);
            lv_obj_align(btnConnectWifi, LV_ALIGN_TOP_LEFT, 190, 18);
            ui_apply_btn_style(btnConnectWifi, UI_COLOR_PRIMARY);
            lv_obj_t* lblConn = lv_label_create(btnConnectWifi);
            if (lblConn) {
                lv_label_set_text(lblConn, "CONNECT");
                lv_obj_set_style_text_color(lblConn, lv_color_hex(0x000000), 0);
                lv_obj_center(lblConn);
            }

            lv_obj_add_event_cb(btnConnectWifi, [](lv_event_t* e) {
                if (wifiStatusLbl) {
                    lv_label_set_text(wifiStatusLbl, "Wi-Fi Status: Connected (NTP Syncing...)");
                    lv_obj_set_style_text_color(wifiStatusLbl, UI_COLOR_SECONDARY, 0);
                }
                send_uart_command("{\"cmd\":{\"sync_rtc\":true}}");
            }, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t* btnSyncRtc = lv_btn_create(tabWifi);
        if (btnSyncRtc) {
            lv_obj_set_size(btnSyncRtc, 100, 32);
            lv_obj_align(btnSyncRtc, LV_ALIGN_TOP_LEFT, 190, 54);
            ui_apply_btn_style(btnSyncRtc, lv_color_hex(0x2e7d32));
            lv_obj_t* lblSync = lv_label_create(btnSyncRtc);
            if (lblSync) {
                lv_label_set_text(lblSync, "NTP RTC SYNC");
                lv_obj_set_style_text_font(lblSync, &lv_font_montserrat_10, 0);
                lv_obj_center(lblSync);
            }

            lv_obj_add_event_cb(btnSyncRtc, [](lv_event_t* e) {
                send_uart_command("{\"cmd\":{\"sync_rtc\":true}}");
            }, LV_EVENT_CLICKED, NULL);
        }
    }

    // -----------------------------------------------------------------
    // TAB 3: DISPLAY SETTINGS
    // -----------------------------------------------------------------
    if (tabDisp != NULL) {
        lv_obj_set_style_bg_color(tabDisp, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(tabDisp, 6, 0);

        lv_obj_t* lblBl = lv_label_create(tabDisp);
        if (lblBl) {
            lv_label_set_text(lblBl, "Backlight Brightness (100%):");
            lv_obj_set_style_text_color(lblBl, UI_COLOR_PRIMARY, 0);
            lv_obj_align(lblBl, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* sliderBl = lv_slider_create(tabDisp);
        if (sliderBl) {
            lv_obj_set_size(sliderBl, 280, 16);
            lv_obj_align(sliderBl, LV_ALIGN_TOP_LEFT, 0, 22);
            lv_slider_set_range(sliderBl, 20, 255);
            lv_slider_set_value(sliderBl, 255, LV_ANIM_OFF);

            lv_obj_add_event_cb(sliderBl, [](lv_event_t* e) {
                lv_obj_t* slider = lv_event_get_target(e);
                int val = lv_slider_get_value(slider);
                backlight_set((uint8_t)val);
            }, LV_EVENT_VALUE_CHANGED, NULL);
        }

        lv_obj_t* lblSleep = lv_label_create(tabDisp);
        if (lblSleep) {
            lv_label_set_text(lblSleep, "Screen Auto-Sleep Timeout:");
            lv_obj_set_style_text_color(lblSleep, UI_COLOR_PRIMARY, 0);
            lv_obj_align(lblSleep, LV_ALIGN_TOP_LEFT, 0, 50);
        }

        lv_obj_t* ddSleep = lv_dropdown_create(tabDisp);
        if (ddSleep) {
            lv_dropdown_set_options(ddSleep, "Off\n1 Minute\n2 Minutes\n5 Minutes");
            lv_dropdown_set_selected(ddSleep, 2);
            lv_obj_set_size(ddSleep, 180, 32);
            lv_obj_align(ddSleep, LV_ALIGN_TOP_LEFT, 0, 72);

            lv_obj_add_event_cb(ddSleep, [](lv_event_t* e) {
                lv_obj_t* dd = lv_event_get_target(e);
                uint16_t sel = lv_dropdown_get_selected(dd);
                switch (sel) {
                    case 0: sleepTimeoutMs = 0; break;
                    case 1: sleepTimeoutMs = 60000; break;
                    case 2: sleepTimeoutMs = 120000; break;
                    case 3: sleepTimeoutMs = 300000; break;
                }
            }, LV_EVENT_VALUE_CHANGED, NULL);
        }
    }

    // -----------------------------------------------------------------
    // TAB 4: SD CARD MANAGEMENT
    // -----------------------------------------------------------------
    lv_obj_t* parentSd = tabSd;
    if (parentSd != NULL) {
        lv_obj_set_style_bg_color(parentSd, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(parentSd, 6, 0);

        uint64_t total_b = 0, free_b = 0;
        bool sd_ok = panel_hardware_get_sd_info(&total_b, &free_b);

        lv_obj_t* lblSdTitle = lv_label_create(parentSd);
        if (lblSdTitle) {
            char sdBuf[128];
            if (sd_ok) {
                float total_mb = (float)total_b / (1024.0f * 1024.0f);
                float free_mb = (float)free_b / (1024.0f * 1024.0f);
                float used_mb = total_mb - free_mb;
                snprintf(sdBuf, sizeof(sdBuf), "Storage: %.1f MB / %.1f MB Used", used_mb, total_mb);
            } else {
                snprintf(sdBuf, sizeof(sdBuf), "SD Card: Not Detected / Unmounted");
            }
            lv_label_set_text(lblSdTitle, sdBuf);
            lv_obj_set_style_text_color(lblSdTitle, UI_COLOR_PRIMARY, 0);
            lv_obj_align(lblSdTitle, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* barSd = NULL;
        if (parentSd != NULL) {
            barSd = lv_bar_create(parentSd);
        }
        if (barSd != NULL) {
            lv_obj_set_size(barSd, 280, 16);
            lv_obj_align(barSd, LV_ALIGN_TOP_LEFT, 0, 24);
            if (sd_ok && total_b > 0) {
                int pct = (int)(((total_b - free_b) * 100) / total_b);
                lv_bar_set_value(barSd, pct, LV_ANIM_OFF);
            } else {
                lv_bar_set_value(barSd, 0, LV_ANIM_OFF);
            }
        }

        std::vector<std::string> roms = nes_manager_scan_roms();
        lv_obj_t* lblCount = lv_label_create(parentSd);
        if (lblCount) {
            char countBuf[64];
            snprintf(countBuf, sizeof(countBuf), "Total NES Games Found: %d", (int)roms.size());
            lv_label_set_text(lblCount, countBuf);
            lv_obj_set_style_text_color(lblCount, UI_COLOR_TEXT_MAIN, 0);
            lv_obj_align(lblCount, LV_ALIGN_TOP_LEFT, 0, 48);
        }

        lv_obj_t* btnFormat = lv_btn_create(parentSd);
        if (btnFormat) {
            lv_obj_set_size(btnFormat, 160, 32);
            lv_obj_align(btnFormat, LV_ALIGN_TOP_LEFT, 0, 76);
            ui_apply_btn_style(btnFormat, UI_COLOR_DANGER);
            lv_obj_t* lblFmt = lv_label_create(btnFormat);
            if (lblFmt) {
                lv_label_set_text(lblFmt, "FORMAT SD CARD");
                lv_obj_center(lblFmt);
            }

            lv_obj_add_event_cb(btnFormat, [](lv_event_t* e) {
                panel_hardware_format_sd();
            }, LV_EVENT_CLICKED, NULL);
        }
    }

    // -----------------------------------------------------------------
    // TAB 5: GAMEPAD 2-PLAYER MAPPING
    // -----------------------------------------------------------------
    if (tabPad != NULL) {
        lv_obj_set_style_bg_color(tabPad, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(tabPad, 6, 0);

        lv_obj_t* lblP1 = lv_label_create(tabPad);
        if (lblP1) {
            lv_label_set_text(lblP1, "Player 1 (USB Port 1): DualShock 2 OK");
            lv_obj_set_style_text_color(lblP1, UI_COLOR_SECONDARY, 0);
            lv_obj_align(lblP1, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* lblP2 = lv_label_create(tabPad);
        if (lblP2) {
            lv_label_set_text(lblP2, "Player 2 (USB Port 2): DualShock 2 OK");
            lv_obj_set_style_text_color(lblP2, UI_COLOR_SECONDARY, 0);
            lv_obj_align(lblP2, LV_ALIGN_TOP_LEFT, 0, 24);
        }

        lv_obj_t* btnSavePad = lv_btn_create(tabPad);
        if (btnSavePad) {
            lv_obj_set_size(btnSavePad, 140, 32);
            lv_obj_align(btnSavePad, LV_ALIGN_TOP_LEFT, 0, 56);
            ui_apply_btn_style(btnSavePad, UI_COLOR_PRIMARY);
            lv_obj_t* lblSPad = lv_label_create(btnSavePad);
            if (lblSPad) {
                lv_label_set_text(lblSPad, "SAVE NVS MAPPING");
                lv_obj_set_style_text_color(lblSPad, lv_color_hex(0x000000), 0);
                lv_obj_center(lblSPad);
            }
        }
    }

    // -----------------------------------------------------------------
    // TAB 6: DEVICE INFO & DIAGNOSTICS
    // -----------------------------------------------------------------
    if (tabInfo != NULL) {
        lv_obj_set_style_bg_color(tabInfo, UI_COLOR_BG, 0);
        lv_obj_set_style_pad_all(tabInfo, 6, 0);

        lv_obj_t* lblDev = lv_label_create(tabInfo);
        if (lblDev) {
            lv_label_set_text(lblDev, "MCU: ESP32-S3 (Xtensa LX7 @ 240MHz)\nFlash: 16 MB QD SPI | PSRAM: Enabled\nOS: ESP-IDF v6.0.1 (FreeRTOS)\nFirmware: Jacktor Panel v1.0.0\nPeripherals: ILI9341, XPT2046, PCM5102");
            lv_obj_set_style_text_color(lblDev, UI_COLOR_TEXT_MUTED, 0);
            lv_obj_align(lblDev, LV_ALIGN_TOP_LEFT, 0, 0);
        }
    }
}
