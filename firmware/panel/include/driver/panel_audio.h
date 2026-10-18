#ifndef PANEL_AUDIO_H
#define PANEL_AUDIO_H

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#include <atomic>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#endif
#include "esp_err.h"

#ifdef __cplusplus
/**
 * @brief Global Atomic Audio Position Counter (in Milliseconds)
 * Updated continuously on Core 0 by I2S DMA driver.
 * Read asynchronously on Core 1 by UI & Lyric Rendering tasks.
 */
extern std::atomic<uint32_t> g_audio_position_ms;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
#define PANEL_AUDIO_INIT_DEFAULT_RATE = 384000
#else
#define PANEL_AUDIO_INIT_DEFAULT_RATE
#endif

/**
 * @brief Initialize Panel I2S Audio HAL Driver for GY-PCM5102 32-Bit DAC and Core 0 Pipeline.
 * @param sample_rate Audio sample rate in Hz (default 384000 Hz / 384 kHz)
 * @return true if initialized successfully, false otherwise.
 */
bool panel_audio_init(uint32_t sample_rate PANEL_AUDIO_INIT_DEFAULT_RATE);

/**
 * @brief Dynamically reconfigure I2S sample rate (e.g. 384000 Hz Hi-Fi mode or 30000 Hz NES APU).
 * @param sample_rate New sample rate in Hz.
 */
void panel_audio_set_rate(uint32_t sample_rate);

/**
 * @brief Write audio samples to GY-PCM5102 DAC via I2S DMA on Core 0.
 * Updates g_audio_position_ms atomically based on written PCM sample counts.
 * @param src Pointer to PCM sample buffer (32-bit / 16-bit PCM)
 * @param size Buffer size in bytes
 * @param timeout_ms Maximum write wait time in milliseconds
 * @return Number of bytes actually written
 */
size_t panel_audio_write(const void *src, size_t size, uint32_t timeout_ms);

/**
 * @brief Reset or set audio position in milliseconds.
 * @param position_ms Position in milliseconds.
 */
void panel_audio_set_position_ms(uint32_t position_ms);

/**
 * @brief Get current atomic audio position in milliseconds.
 * @return Position in milliseconds.
 */
uint32_t panel_audio_get_position_ms(void);

/**
 * @brief Set global playback output volume (0 - 100%).
 * @param volume_percent Volume level in percentage
 */
void panel_audio_set_volume(uint8_t volume_percent);

/**
 * @brief Get current playback volume level.
 * @return Volume percentage (0 - 100)
 */
uint8_t panel_audio_get_volume(void);

/**
 * @brief Zero out I2S DMA buffer, pause audio output, and reset position counter.
 */
void panel_audio_stop(void);

/**
 * @brief Start High-Priority Audio Pipeline on Core 0 (SMB Stream Task & FLAC Decoder Task).
 */
void panel_audio_start_core0_pipeline(void);

#ifdef __cplusplus
}
#endif

#endif // PANEL_AUDIO_H
