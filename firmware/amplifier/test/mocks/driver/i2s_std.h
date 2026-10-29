#pragma once

#include <cstdint>
#include <cstddef>

typedef void* i2s_chan_handle_t;
typedef int esp_err_t;
typedef int gpio_num_t;

#define ESP_OK 0
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_GPIO_UNUSED (-1)
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_MONO 1
#define pdMS_TO_TICKS(ms) (ms)

struct i2s_chan_config_t {
    int id;
    int role;
    int dma_desc_num;
    int dma_frame_num;
    bool auto_clear;
};

struct i2s_std_clk_config_t {};
#define I2S_STD_CLK_DEFAULT_CONFIG(freq) i2s_std_clk_config_t{}

struct i2s_std_slot_config_t {};
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) i2s_std_slot_config_t{}

struct i2s_gpio_config_t {
    gpio_num_t mclk;
    gpio_num_t bclk;
    gpio_num_t ws;
    gpio_num_t dout;
    gpio_num_t din;
    struct {
        bool mclk_inv;
        bool bclk_inv;
        bool ws_inv;
    } invert_flags;
};

struct i2s_std_config_t {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_gpio_config_t gpio_cfg;
};

inline esp_err_t i2s_new_channel(const i2s_chan_config_t*, i2s_chan_handle_t*, i2s_chan_handle_t* rx_handle) {
    if (rx_handle) *rx_handle = (void*)0x1234;
    return ESP_OK;
}

inline esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t*) {
    return ESP_OK;
}

inline esp_err_t i2s_channel_enable(i2s_chan_handle_t) {
    return ESP_OK;
}

inline esp_err_t i2s_channel_disable(i2s_chan_handle_t) {
    return ESP_OK;
}

inline esp_err_t i2s_del_channel(i2s_chan_handle_t) {
    return ESP_OK;
}

inline esp_err_t i2s_channel_read(i2s_chan_handle_t, void* dest, size_t size, size_t* bytes_read, uint32_t) {
    if (dest && size > 0) {
        std::memset(dest, 0, size);
        if (bytes_read) *bytes_read = size;
    }
    return ESP_OK;
}
