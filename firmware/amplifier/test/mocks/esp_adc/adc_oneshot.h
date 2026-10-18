#pragma once
#include <cstdint>

typedef void* adc_oneshot_unit_handle_t;
typedef enum { ADC_UNIT_1 = 1 } adc_unit_t;
typedef enum { ADC_CHANNEL_0 = 0, ADC_CHANNEL_3 = 3 } adc_channel_t;
typedef enum { ADC_ATTEN_DB_12 = 3 } adc_atten_t;
typedef enum { ADC_BITWIDTH_12 = 12 } adc_bitwidth_t;
typedef enum { ADC_RTC_CLK_SRC_DEFAULT = 0 } adc_clk_src_t;
typedef enum { ADC_ULP_MODE_DISABLE = 0 } adc_ulp_mode_t;
typedef int esp_err_t;
#define ESP_OK 0

struct adc_oneshot_unit_init_cfg_t {
    adc_unit_t unit_id;
    adc_clk_src_t clk_src;
    adc_ulp_mode_t ulp_mode;
};

struct adc_oneshot_chan_cfg_t {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
};

inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t* h) { *h = (void*)1; return ESP_OK; }
inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*) { return ESP_OK; }
inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int* val) { *val = 2048; return ESP_OK; }
inline esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t) { return ESP_OK; }
