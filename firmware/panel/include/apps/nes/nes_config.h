#ifndef NES_CONFIG_H
#define NES_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

struct NesControllerConfig {
    uint32_t map_btn_a;      // DualShock 2 raw mask for NES A
    uint32_t map_btn_b;      // DualShock 2 raw mask for NES B
    uint32_t map_btn_select; // DualShock 2 raw mask for NES SELECT
    uint32_t map_btn_start;  // DualShock 2 raw mask for NES START
    uint32_t map_btn_up;     // DualShock 2 raw mask for NES UP
    uint32_t map_btn_down;   // DualShock 2 raw mask for NES DOWN
    uint32_t map_btn_left;   // DualShock 2 raw mask for NES LEFT
    uint32_t map_btn_right;  // DualShock 2 raw mask for NES RIGHT
};

#ifdef __cplusplus
extern "C" {
#endif

void nes_config_init(void);
NesControllerConfig nes_config_get(void);
void nes_config_save(const NesControllerConfig* cfg);
void nes_config_reset_defaults(void);
uint32_t nes_config_map_gamepad_state(uint32_t raw_gamepad_state);

#ifdef __cplusplus
}
#endif

#endif // NES_CONFIG_H
