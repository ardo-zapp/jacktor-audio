#include "nes_config.h"
#include <cstring>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "nofrendo/noftypes.h"
#include "nofrendo/nesinput.h"

static const char* TAG = "NES_CFG";
static const char* NVS_NAMESPACE = "nes_config";
static const char* NVS_KEY = "ctrl_cfg";

static NesControllerConfig s_config;

static const NesControllerConfig s_default_config = {
    .map_btn_a      = (1ULL << 0), // Cross
    .map_btn_b      = (1ULL << 1), // Circle
    .map_btn_select = (1ULL << 4), // Select
    .map_btn_start  = (1ULL << 5), // Start
    .map_btn_up     = (1ULL << 6), // D-Pad Up
    .map_btn_down   = (1ULL << 7), // D-Pad Down
    .map_btn_left   = (1ULL << 8), // D-Pad Left
    .map_btn_right  = (1ULL << 9)  // D-Pad Right
};

void nes_config_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }

    nvs_handle_t nvs_h;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h);
    if (err == ESP_OK) {
        size_t required_size = sizeof(NesControllerConfig);
        err = nvs_get_blob(nvs_h, NVS_KEY, &s_config, &required_size);
        if (err != ESP_OK || required_size != sizeof(NesControllerConfig)) {
            ESP_LOGI(TAG, "No NVS config found, writing defaults");
            s_config = s_default_config;
            nvs_set_blob(nvs_h, NVS_KEY, &s_config, sizeof(NesControllerConfig));
            nvs_commit(nvs_h);
        } else {
            ESP_LOGI(TAG, "Loaded custom NES controller config from NVS");
        }
        nvs_close(nvs_h);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS namespace");
        s_config = s_default_config;
    }
}

NesControllerConfig nes_config_get(void) {
    return s_config;
}

void nes_config_save(const NesControllerConfig* cfg) {
    if (!cfg) return;
    s_config = *cfg;

    nvs_handle_t nvs_h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_h) == ESP_OK) {
        nvs_set_blob(nvs_h, NVS_KEY, &s_config, sizeof(NesControllerConfig));
        nvs_commit(nvs_h);
        nvs_close(nvs_h);
        ESP_LOGI(TAG, "Saved NES controller config to NVS");
    }
}

void nes_config_reset_defaults(void) {
    s_config = s_default_config;
    nes_config_save(&s_config);
}

uint32_t nes_config_map_gamepad_state(uint32_t raw) {
    uint32_t nes_pad = 0;

    if (raw & s_config.map_btn_a)      nes_pad |= INP_PAD_A;
    if (raw & s_config.map_btn_b)      nes_pad |= INP_PAD_B;
    if (raw & s_config.map_btn_select) nes_pad |= INP_PAD_SELECT;
    if (raw & s_config.map_btn_start)  nes_pad |= INP_PAD_START;
    if (raw & s_config.map_btn_up)     nes_pad |= INP_PAD_UP;
    if (raw & s_config.map_btn_down)   nes_pad |= INP_PAD_DOWN;
    if (raw & s_config.map_btn_left)   nes_pad |= INP_PAD_LEFT;
    if (raw & s_config.map_btn_right)  nes_pad |= INP_PAD_RIGHT;

    return nes_pad;
}
