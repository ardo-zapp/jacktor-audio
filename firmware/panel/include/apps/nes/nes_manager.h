#ifndef NES_MANAGER_H
#define NES_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#include <vector>
#include <string>
#endif

#ifdef __cplusplus
extern "C" {
#endif

void nes_manager_init(void);
bool nes_manager_is_sd_mounted(void);
#ifdef __cplusplus
std::vector<std::string> nes_manager_scan_roms(void);
#endif
bool nes_manager_start_game(const char* rom_path);
void nes_manager_stop_game(void);
bool nes_manager_is_running(void);
void nes_manager_update_input(uint32_t pad_state);
void nes_manager_update_player_input(int player, uint32_t raw_pad_state);
void panel_ui_request_exit_game(void);
bool nes_manager_check_pending_exit(void);
void nes_manager_set_pending_exit(bool pending);

#ifdef __cplusplus
}
#endif

#endif // NES_MANAGER_H
