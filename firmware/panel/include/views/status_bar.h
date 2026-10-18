#ifndef GLOBAL_STATUS_BAR_H
#define GLOBAL_STATUS_BAR_H

#include "panel_main.h"

extern lv_obj_t* globalStatusBar;

void create_global_status_bar(lv_obj_t* parentScreen);
void update_global_status_bar(void);
void trigger_status_bar_rx_pulse(void);
void trigger_status_bar_tx_pulse(void);

#endif // GLOBAL_STATUS_BAR_H
