#ifndef PANEL_HARDWARE_H
#define PANEL_HARDWARE_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize Panel Hardware peripherals (SPI Bus, ILI9341 LCD, XPT2046 Touch, Backlight PWM).
 */
void panel_hardware_init(void);

/**
 * @brief Set LCD backlight brightness level (0 - 255).
 * @param duty_255 PWM duty cycle
 */
void backlight_set(uint8_t duty_255);

/**
 * @brief Directly push raw RGB565 frame pixels to ILI9341 display via SPI DMA (bypassing LVGL).
 */
void ili9341_draw_direct_frame(const uint16_t* pixels, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/**
 * @brief Get timestamp of last screen touch in milliseconds.
 */
uint32_t panel_hardware_get_last_touch_ms(void);

/**
 * @brief Get current touch position and state.
 */
bool panel_hardware_get_touch_pos(int16_t* x, int16_t* y);

/**
 * @brief Get SD Card storage capacity and free space in bytes.
 */
bool panel_hardware_get_sd_info(uint64_t* total_bytes, uint64_t* free_bytes);

/**
 * @brief Format SD Card to FAT32 filesystem.
 */
bool panel_hardware_format_sd(void);

/**
 * @brief Switch USB PHY mode dynamically (USB Host / OTG vs USB Device DAC slave).
 * @param is_host_mode true for USB Host/OTG mode (default), false for USB DAC device mode
 */
void panel_hardware_set_usb_host_mode(bool is_host_mode);
bool panel_hardware_is_usb_host_mode(void);

#ifdef __cplusplus
}
#endif

#endif // PANEL_HARDWARE_H
