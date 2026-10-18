#ifndef PANEL_UI_H
#define PANEL_UI_H

#include <lvgl.h>
#include "panel_main.h"

#ifdef __cplusplus
extern "C" {
#endif

void create_standby_screen(void);
void create_boot_terminal_screen(void);
void create_operational_screen(void);
void create_nes_menu_screen(void);
void create_nes_settings_screen(void);
void create_settings_app_screen(void);
void refresh_nes_rom_list(void);

extern lv_obj_t* standbyScreen;
extern lv_obj_t* labelTime;
extern lv_obj_t* labelVoltages;
extern lv_obj_t* labelTemps;
extern lv_obj_t* bootTerminalScreen;
extern lv_obj_t* bootTerminalArea;
extern lv_obj_t* operationalScreen;
extern lv_obj_t* nesLauncherScreen;
extern lv_obj_t* nesRomListContainer;
extern lv_obj_t* nesSettingsScreen;
extern lv_obj_t* settingsAppScreen;

#ifdef __cplusplus
}
#endif

#endif // PANEL_UI_H
