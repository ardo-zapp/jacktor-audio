#include "nes_manager.h"
#include "nes_config.h"
#include "panel_main.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nofrendo/nofrendo.h"
#include "nofrendo/osd.h"
#include "nofrendo/nesinput.h"
#include "driver/panel_hardware.h"
#include "driver/panel_audio.h"

static const char* TAG = "NES_MGR";
static bool s_is_running = false;
static TaskHandle_t s_emu_task_handle = nullptr;
static std::string s_current_rom_path = "";

void nes_manager_init(void) {
    struct stat st;
    if (stat("/sdcard", &st) == 0) {
        if (stat("/sdcard/NES", &st) != 0) {
            mkdir("/sdcard/NES", 0775);
            ESP_LOGI(TAG, "Created /sdcard/NES directory");
        }
    }
}

bool nes_manager_is_sd_mounted(void) {
    struct stat st;
    return (stat("/sdcard", &st) == 0);
}

std::vector<std::string> nes_manager_scan_roms(void) {
    std::vector<std::string> roms;
    if (!nes_manager_is_sd_mounted()) {
        return roms;
    }

    struct stat st;
    if (stat("/sdcard/NES", &st) != 0) {
        mkdir("/sdcard/NES", 0775);
    }

    DIR* dir = opendir("/sdcard/NES");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard/NES");
        return roms;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue; // Skip hidden files
        size_t len = strlen(entry->d_name);
        if (len > 4) {
            const char* ext = entry->d_name + len - 4;
            if (strcasecmp(ext, ".nes") == 0) {
                roms.push_back(std::string(entry->d_name));
            }
        }
    }
    closedir(dir);

    std::sort(roms.begin(), roms.end());
    return roms;
}

static void emu_task_func(void* arg) {
    ESP_LOGI(TAG, "Starting emulation task for ROM: %s", s_current_rom_path.c_str());
    s_is_running = true;

    vid_preload_rom(s_current_rom_path.c_str());
    main_loop(s_current_rom_path.c_str(), system_nes);

    ESP_LOGI(TAG, "Emulation task finished");
    s_is_running = false;
    s_emu_task_handle = nullptr;
    vTaskDelete(NULL);
}

bool nes_manager_start_game(const char* rom_path) {
    if (!rom_path || s_is_running) return false;

    s_current_rom_path = rom_path;
    send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"audio_relay\":true}}");
    xTaskCreatePinnedToCore(emu_task_func, "nes_emu_task", 8192, NULL, 5, &s_emu_task_handle, 1);
    return true;
}

void nes_manager_stop_game(void) {
    if (s_is_running) {
        main_quit();
        s_is_running = false;
        send_uart_command("{\"type\":\"cmd\",\"cmd\":{\"audio_relay\":false}}");
    }
}

bool nes_manager_is_running(void) {
    return s_is_running;
}

static uint32_t s_hotkey_start_ms = 0;
static volatile bool s_pending_exit = false;

bool nes_manager_check_pending_exit(void) {
    return s_pending_exit;
}

void nes_manager_set_pending_exit(bool pending) {
    s_pending_exit = pending;
}

void nes_manager_update_player_input(int player, uint32_t raw_pad_state) {
    uint32_t mapped_state = nes_config_map_gamepad_state(raw_pad_state);
    if (player == 2) {
        nes_set_gamepad2_state(mapped_state);
    } else {
        nes_set_gamepad_state(mapped_state);
    }

    // Check SELECT + START hotkey (INP_PAD_SELECT = 0x04, INP_PAD_START = 0x08)
    const uint32_t hotkey_mask = 0x04 | 0x08;
    if ((mapped_state & hotkey_mask) == hotkey_mask) {
        uint32_t now = millis();
        if (s_hotkey_start_ms == 0) {
            s_hotkey_start_ms = now;
        } else if (now - s_hotkey_start_ms >= 1000) { // 1 second hold
            ESP_LOGI(TAG, "Hotkey SELECT+START detected on P%d, exiting game...", player);
            s_hotkey_start_ms = 0;
            s_pending_exit = true;
        }
    } else if (player == 1) {
        s_hotkey_start_ms = 0;
    }
}

void nes_manager_update_input(uint32_t pad_state) {
    nes_manager_update_player_input(1, pad_state);
}
