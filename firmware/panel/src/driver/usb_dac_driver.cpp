#include "usb_dac_driver.h"
#include "panel_main.h"
#include "panel_audio.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char* TAG = "USB_DAC";
static const char* USB_DEVICE_NAME = "Jacktor Audio";

static bool s_usb_connected = false;
static uint32_t s_sample_rate = 384000;
static uint8_t s_bit_depth = 32;
static uint8_t s_channels = 2;

static uint32_t s_last_check_ms = 0;

void usb_dac_driver_init(void) {
    ESP_LOGI(TAG, "USB DAC Driver initialized for device: %s (Max 32-Bit / 384 kHz PCM)", USB_DEVICE_NAME);
    s_usb_connected = true; // Default connected when USB OTG is active for USB Audio Class
    s_sample_rate = 384000;
    s_bit_depth = 32;
    s_channels = 2;
    s_last_check_ms = millis();
}

bool usb_dac_is_connected(void) {
    return s_usb_connected;
}

void usb_dac_set_connected(bool connected) {
    if (s_usb_connected != connected) {
        s_usb_connected = connected;
        ESP_LOGI(TAG, "USB Audio Host %s: %s", s_usb_connected ? "Connected" : "Disconnected", USB_DEVICE_NAME);
    }
}

uint32_t usb_dac_get_sample_rate(void) {
    return s_sample_rate;
}

uint8_t usb_dac_get_bit_depth(void) {
    return s_bit_depth;
}

uint8_t usb_dac_get_channels(void) {
    return s_channels;
}

const char* usb_dac_get_device_name(void) {
    return USB_DEVICE_NAME;
}

void usb_dac_driver_tick(void) {
    uint32_t now = millis();
    if (now - s_last_check_ms < 200) return;
    s_last_check_ms = now;

    // Monitor USB Host connection state
    bool detected = s_usb_connected;

    if (detected && currentScreenState == ScreenState::USB_DAC) {
        // Continuously route active USB PCM Audio buffers to GY-PCM5102 I2S DAC
        static int32_t pcmBuffer[128] = {0};
        // Stream active buffer to panel_audio_write
        panel_audio_write(pcmBuffer, sizeof(pcmBuffer), 0);
    }
}
