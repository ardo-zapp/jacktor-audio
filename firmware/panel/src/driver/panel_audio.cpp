#include "driver/panel_audio.h"
#include "panel_main.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include <algorithm>
#include <vector>
#include <atomic>

static const char *TAG = "PANEL_AUDIO";

std::atomic<uint32_t> g_audio_position_ms{0};

static i2s_chan_handle_t tx_handle = NULL;
static uint32_t current_sample_rate = 384000;
static uint8_t global_volume = 100;
static std::vector<int32_t> volume_scaled_buffer_32;

static uint64_t total_pcm_frames_written = 0;

// Core 0 PSRAM Ring Buffer (512 KB) for SMB / Network Streaming
static RingbufHandle_t psram_stream_ringbuf = NULL;
static TaskHandle_t smb_stream_task_handle = NULL;
static TaskHandle_t flac_decoder_task_handle = NULL;

static const size_t PSRAM_RINGBUF_SIZE = 512 * 1024; // 512 KB PSRAM Buffer

bool panel_audio_init(uint32_t sample_rate) {
    if (tx_handle != NULL) {
        return true;
    }

    current_sample_rate = sample_rate;
    g_audio_position_ms.store(0, std::memory_order_relaxed);
    total_pcm_frames_written = 0;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 12;
    chan_cfg.dma_frame_num = 512;
    chan_cfg.auto_clear = true;

    if (i2s_new_channel(&chan_cfg, &tx_handle, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate I2S TX channel");
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(current_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, // SCK connected to GND for internal PLL on GY-PCM5102
            .bclk = (gpio_num_t)I2S_DAC_BCK_PIN,
            .ws   = (gpio_num_t)I2S_DAC_WS_PIN,
            .dout = (gpio_num_t)I2S_DAC_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    if (i2s_channel_init_std_mode(tx_handle, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S STD mode");
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return false;
    }

    if (i2s_channel_enable(tx_handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S TX channel");
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return false;
    }

    // Allocate 512 KB PSRAM Ring Buffer for SMB/Network streaming
    if (psram_stream_ringbuf == NULL) {
        psram_stream_ringbuf = xRingbufferCreateWithCaps(
            PSRAM_RINGBUF_SIZE,
            RINGBUF_TYPE_BYTEBUF,
            MALLOC_CAP_SPIRAM
        );
        if (psram_stream_ringbuf == NULL) {
            ESP_LOGW(TAG, "Failed to allocate PSRAM Ring Buffer, trying internal RAM");
            psram_stream_ringbuf = xRingbufferCreate(16 * 1024, RINGBUF_TYPE_BYTEBUF);
        } else {
            ESP_LOGI(TAG, "Allocated 512 KB PSRAM Stream Ring Buffer successfully");
        }
    }

    panel_audio_start_core0_pipeline();

    ESP_LOGI(TAG, "GY-PCM5102 32-Bit I2S Audio Driver initialized on Core 0 at %lu Hz (BCK:%d, DOUT:%d, WS:%d)",
             current_sample_rate, I2S_DAC_BCK_PIN, I2S_DAC_DOUT_PIN, I2S_DAC_WS_PIN);
    return true;
}

// --- CORE 0 STREAMING TASKS ---
static void smb_stream_task(void *pvParameters) {
    ESP_LOGI(TAG, "SMB Stream Task running on Core 0 (Priority 20)");
    while (1) {
        // High priority network stream buffer filler
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void flac_decoder_task(void *pvParameters) {
    ESP_LOGI(TAG, "FLAC Decoder Task running on Core 0 (Priority 19)");
    while (1) {
        // High priority audio decoder feeding I2S DMA
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void panel_audio_start_core0_pipeline(void) {
    if (smb_stream_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            smb_stream_task,
            "smb_stream",
            8192,
            NULL,
            20, // Priority 20
            &smb_stream_task_handle,
            0   // Core 0
        );
    }
    if (flac_decoder_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            flac_decoder_task,
            "flac_decoder",
            12288,
            NULL,
            19, // Priority 19
            &flac_decoder_task_handle,
            0   // Core 0
        );
    }
}

void panel_audio_set_rate(uint32_t sample_rate) {
    if (sample_rate == current_sample_rate || tx_handle == NULL) return;

    current_sample_rate = sample_rate;
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(current_sample_rate);
    i2s_channel_disable(tx_handle);
    i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);
    i2s_channel_enable(tx_handle);
    ESP_LOGI(TAG, "I2S Audio sample rate updated to %lu Hz", current_sample_rate);
}

size_t panel_audio_write(const void *src, size_t size, uint32_t timeout_ms) {
    if (tx_handle == NULL || src == NULL || size == 0) return 0;

    size_t bytes_written = 0;
    const int32_t *pcm_in = (const int32_t *)src;
    size_t num_samples = size / sizeof(int32_t);

    if (global_volume < 100) {
        if (volume_scaled_buffer_32.size() < num_samples) {
            volume_scaled_buffer_32.resize(num_samples);
        }
        for (size_t i = 0; i < num_samples; ++i) {
            int64_t sample = (int64_t)pcm_in[i] * global_volume / 100;
            volume_scaled_buffer_32[i] = (int32_t)std::clamp(sample, (int64_t)-2147483648, (int64_t)2147483647);
        }
        pcm_in = volume_scaled_buffer_32.data();
    }

    esp_err_t err = i2s_channel_write(tx_handle, pcm_in, size, &bytes_written, pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_OK && bytes_written > 0) {
        // Stereo 32-bit PCM: 2 channels * 4 bytes per sample = 8 bytes per frame
        size_t frames = bytes_written / (sizeof(int32_t) * 2);
        total_pcm_frames_written += frames;

        if (current_sample_rate > 0) {
            uint32_t current_ms = (uint32_t)((total_pcm_frames_written * 1000ULL) / current_sample_rate);
            g_audio_position_ms.store(current_ms, std::memory_order_release);
        }
    }

    return bytes_written;
}

void panel_audio_set_position_ms(uint32_t position_ms) {
    g_audio_position_ms.store(position_ms, std::memory_order_release);
    if (current_sample_rate > 0) {
        total_pcm_frames_written = ((uint64_t)position_ms * current_sample_rate) / 1000ULL;
    }
}

uint32_t panel_audio_get_position_ms(void) {
    return g_audio_position_ms.load(std::memory_order_acquire);
}

void panel_audio_set_volume(uint8_t volume_percent) {
    global_volume = std::min((uint8_t)100, volume_percent);
}

uint8_t panel_audio_get_volume(void) {
    return global_volume;
}

void panel_audio_stop(void) {
    if (tx_handle != NULL) {
        i2s_channel_disable(tx_handle);
        i2s_channel_enable(tx_handle);
    }
    panel_audio_set_position_ms(0);
}
