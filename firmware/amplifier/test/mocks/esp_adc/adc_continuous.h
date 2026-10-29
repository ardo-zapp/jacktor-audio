#ifndef ADC_CONTINUOUS_MOCK_H
#define ADC_CONTINUOUS_MOCK_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

typedef void* adc_continuous_handle_t;

typedef enum {
    ADC_UNIT_1 = 1,
} adc_unit_t;

typedef enum {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_3 = 3,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_12 = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_12 = 12,
} adc_bitwidth_t;

typedef enum {
    ADC_CONV_SINGLE_UNIT_1 = 1,
} adc_continuous_convert_mode_t;

typedef enum {
    ADC_DIGI_OUTPUT_FORMAT_TYPE1 = 0,
} adc_digi_output_format_t;

typedef struct {
    uint32_t max_store_buf_size;
    uint32_t conv_frame_size;
    struct {
        uint32_t flush_conv_frame_data: 1;
    } flags;
} adc_continuous_handle_cfg_t;

typedef struct {
    adc_atten_t atten;
    adc_channel_t channel;
    adc_unit_t unit;
    adc_bitwidth_t bit_width;
} adc_digi_pattern_config_t;

typedef struct {
    adc_digi_pattern_config_t *adc_pattern;
    size_t pattern_num;
    uint32_t sample_freq_hz;
    adc_continuous_convert_mode_t conv_mode;
    adc_digi_output_format_t format;
} adc_continuous_config_t;

typedef struct {
    union {
        struct {
            uint32_t data: 12;
            uint32_t channel: 4;
            uint32_t unit: 1;
            uint32_t reserved: 15;
        } type1;
        uint32_t val;
    };
} adc_digi_output_data_t;

inline esp_err_t adc_continuous_new_handle(const adc_continuous_handle_cfg_t *h_cfg, adc_continuous_handle_t *out_handle) {
    if (out_handle) *out_handle = (void*)1;
    return ESP_OK;
}

inline esp_err_t adc_continuous_config(adc_continuous_handle_t handle, const adc_continuous_config_t *config) {
    (void)handle; (void)config;
    return ESP_OK;
}

inline esp_err_t adc_continuous_start(adc_continuous_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

inline esp_err_t adc_continuous_stop(adc_continuous_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

inline esp_err_t adc_continuous_deinit(adc_continuous_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

inline esp_err_t adc_continuous_read(adc_continuous_handle_t handle, uint8_t *buf, uint32_t length, uint32_t *out_length, uint32_t timeout_ms) {
    (void)handle; (void)timeout_ms;
    if (out_length) *out_length = length;
    if (buf) {
        adc_digi_output_data_t *p = (adc_digi_output_data_t*)buf;
        size_t count = length / sizeof(adc_digi_output_data_t);
        for (size_t i = 0; i < count; i++) {
            p[i].type1.channel = (i % 2 == 0) ? ADC_CHANNEL_0 : ADC_CHANNEL_3;
            p[i].type1.data = 2048;
        }
    }
    return ESP_OK;
}

#endif // ADC_CONTINUOUS_MOCK_H
