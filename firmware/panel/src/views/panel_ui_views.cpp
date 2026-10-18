#include "panel_ui.h"
#include "panel_main.h"
#include "nes_manager.h"
#include "nes_bgm.h"
#include "nes_config.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

ScreenState currentScreenState = ScreenState::OPERATIONAL;
bool isIndependentBypassMode = true;
bool isLcdSleeping = false;
uint32_t lastTouchActivityMs = 0;
uint32_t sleepTimeoutMs = 0; // Disabled auto-sleep to keep LCD permanently active

float stbyVoltage = 0.0f;
float vddVoltage = 0.0f;
float espVoltage = 0.0f;
float heatsinkTemp = 0.0f;
float rtcTemp = 0.0f;
uint8_t telemetryVuL = 0;
uint8_t telemetryVuR = 0;
uint8_t telemetryBands[64] = {0};
uint8_t telemetryBandsLen = 0;
bool isBtEnabledState = true;
bool isBtInputActive = false;
bool amplifierPowerOn = false;
bool isInitializingState = false;
bool isBtFailedPerm = false;
lv_obj_t* mboxInitializing = nullptr;
lv_obj_t* mboxError = nullptr;
char currentTimeStr[24] = "1970-01-01 00:00:00";

lv_obj_t* labelNoLinkIndicator = nullptr;

bool isSelftestActive = false;
bool isSelftestFailed = false;
lv_obj_t* btnSilenceAlarm = nullptr;
bool selftestReportReceived = false;
bool selftestStepOk = false;
std::string selftestErrorDetail = "";
float selftestSmpsVolt = 0.0f;
float selftestStbyVolt = 0.0f;

void panel_ui_request_exit_game(void) {
    nes_manager_stop_game();
    panel_ui_switch_screen(ScreenState::NES_MENU);
}

void panel_ui_switch_screen(ScreenState state) {
    if (currentScreenState == ScreenState::USB_DAC && state != ScreenState::USB_DAC) {
        panel_audio_stop();
        panel_hardware_set_usb_host_mode(true);
        send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"audio_relay\":false,\"display_text\":\"\"}}");
        if (usbDacScreen) {
            lv_obj_del(usbDacScreen);
            usbDacScreen = nullptr;
        }
    }

    if (currentScreenState == ScreenState::USB_MSC && state != ScreenState::USB_MSC) {
        // Leaving USB MSC: Disconnect MSC slave session, destroy UI, and restore default USB Host / OTG mode
        panel_hardware_set_usb_host_mode(true);
        if (usbMscScreen) {
            lv_obj_del(usbMscScreen);
            usbMscScreen = nullptr;
        }
    }

    if (currentScreenState == ScreenState::SETTINGS_APP && state != ScreenState::SETTINGS_APP) {
        if (settingsAppScreen) {
            lv_obj_del(settingsAppScreen);
            settingsAppScreen = nullptr;
        }
    }

    currentScreenState = state;
    switch (state) {
        case ScreenState::STANDBY:
            nes_bgm_stop();
            if (standbyScreen) {
                lv_scr_load(standbyScreen);
                backlight_set(255);
            } else {
                panel_ui_enter_deep_sleep();
            }
            break;
        case ScreenState::SELF_TEST:
            nes_bgm_stop();
            if (bootTerminalScreen) lv_scr_load(bootTerminalScreen);
            break;
        case ScreenState::OPERATIONAL:
            nes_bgm_stop();
            if (operationalScreen) lv_scr_load(operationalScreen);
            break;
        case ScreenState::NES_MENU:
            if (!nesLauncherScreen) create_nes_menu_screen();
            if (nesLauncherScreen) {
                refresh_nes_rom_list();
                lv_scr_load(nesLauncherScreen);
                nes_bgm_start();
                nes_bgm_resume();
            }
            break;
        case ScreenState::NES_GAME:
            // Dynamic USB Mode Switch: Disconnect MSC + USB DAC when opening USB Host / OTG gamepad emulator
            panel_audio_stop();
            nes_bgm_pause();
            break;
        case ScreenState::NES_SETTINGS:
            nes_bgm_pause();
            if (!nesSettingsScreen) create_nes_settings_screen();
            if (nesSettingsScreen) lv_scr_load(nesSettingsScreen);
            break;
        case ScreenState::SETTINGS_APP:
            nes_bgm_stop();
            if (!settingsAppScreen) create_settings_app_screen();
            if (settingsAppScreen) lv_scr_load(settingsAppScreen);
            break;
        case ScreenState::USB_DAC:
            nes_bgm_stop();
            // Entering USB DAC: Switch USB PHY to Device mode (USB DAC slave)
            panel_hardware_set_usb_host_mode(false);
            send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"audio_relay\":true,\"display_text\":\"usb\"}}");
            if (!usbDacScreen) create_usb_dac_screen();
            if (usbDacScreen) lv_scr_load(usbDacScreen);
            break;
        case ScreenState::MUSIC_PLAYER:
            nes_bgm_stop();
            if (!musicPlayerScreen) create_music_player_screen();
            if (musicPlayerScreen) lv_scr_load(musicPlayerScreen);
            break;
        case ScreenState::USB_MSC:
            nes_bgm_stop();
            // Entering USB MSC: Switch USB PHY to Device mode for PC file transfer
            panel_hardware_set_usb_host_mode(false);
            if (!usbMscScreen) create_usb_msc_screen();
            if (usbMscScreen) lv_scr_load(usbMscScreen);
            break;
    }
}

void panel_ui_enter_deep_sleep() {
    isLcdSleeping = true;
    backlight_set(0);
    panel_audio_stop();
    nes_bgm_stop();
    send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"power\":false}}");
}

void panel_ui_init() {
    gpio_config_t boot_conf = {};
    boot_conf.intr_type = GPIO_INTR_DISABLE;
    boot_conf.mode = GPIO_MODE_INPUT;
    boot_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    boot_conf.pin_bit_mask = (1ULL << GPIO_NUM_0);
    gpio_config(&boot_conf);

    // Instant non-blocking check of BOOT button state on power-up
    if (gpio_get_level(GPIO_NUM_0) == 0) {
        isIndependentBypassMode = true;
    } else {
        isIndependentBypassMode = true; // Ensure UI boots up immediately when powered
    }

    nes_config_init();
    nes_bgm_init();
    create_standby_screen();
    create_boot_terminal_screen();
    create_operational_screen();

    isLcdSleeping = false;
    gpio_set_level((gpio_num_t)TFT_BL_PIN, 1);
    backlight_set(255);

    panel_ui_switch_screen(ScreenState::OPERATIONAL);
}
