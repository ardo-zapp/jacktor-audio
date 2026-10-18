#ifndef USB_DAC_DRIVER_H
#define USB_DAC_DRIVER_H

#include "panel_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB DAC audio driver and USB OTG PHY.
 */
void usb_dac_driver_init(void);

/**
 * @brief Returns true if USB Audio host (PC / Mobile) is connected.
 */
bool usb_dac_is_connected(void);
void usb_dac_set_connected(bool connected);

/**
 * @brief Returns active PCM sample rate in Hz (e.g. 384000 Hz).
 */
uint32_t usb_dac_get_sample_rate(void);

/**
 * @brief Returns active PCM bit depth (16, 24, or 32 bit).
 */
uint8_t usb_dac_get_bit_depth(void);

/**
 * @brief Returns active channel count (2 for Stereo L/R).
 */
uint8_t usb_dac_get_channels(void);

/**
 * @brief Returns USB product string name ("Jacktor Audio").
 */
const char* usb_dac_get_device_name(void);

/**
 * @brief Ticks USB connection state, handles auto-open/auto-close events, and routes audio.
 */
void usb_dac_driver_tick(void);

#ifdef __cplusplus
}
#endif

#endif // USB_DAC_DRIVER_H
