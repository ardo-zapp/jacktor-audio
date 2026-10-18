#ifndef PANEL_MAIN_H
#define PANEL_MAIN_H

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#else
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#endif
#include <lvgl.h>
#include "cJSON.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "driver/panel_hardware.h"
#include "driver/panel_audio.h"
#include "driver/usb_dac_driver.h"
#include "apps/dac/ui_usb_dac.h"
#include "apps/player/ui_music_player.h"

static inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Legacy aliases mapped directly to config.h macros
#define S_SCLK           CONFIG_S_SCLK
#define S_MISO           CONFIG_S_MISO
#define S_MOSI           CONFIG_S_MOSI

#define TFT_CS_PIN       CONFIG_TFT_CS_PIN
#define TFT_DC_PIN       CONFIG_TFT_DC_PIN
#define TFT_RST_PIN      CONFIG_TFT_RST_PIN
#define TOUCH_CS_PIN     CONFIG_TOUCH_CS_PIN
#define SD_CS_PIN        CONFIG_SD_CS_PIN
#define TOUCH_IRQ_PIN   CONFIG_TOUCH_IRQ_PIN

#define TFT_BL_PIN      CONFIG_TFT_BL_PIN

#define I2S_DAC_WS_PIN   CONFIG_I2S_DAC_WS_PIN
#define I2S_DAC_DOUT_PIN CONFIG_I2S_DAC_DOUT_PIN
#define I2S_DAC_BCK_PIN  CONFIG_I2S_DAC_BCK_PIN

#define AMP_TX_PIN      CONFIG_AMP_TX_PIN
#define AMP_RX_PIN      CONFIG_AMP_RX_PIN
#define AMP_BAUD        CONFIG_AMP_BAUD_RATE

enum class ScreenState {
  SELF_TEST,
  STANDBY,
  OPERATIONAL,
  NES_MENU,
  NES_GAME,
  NES_SETTINGS,
  SETTINGS_APP,
  USB_DAC,
  MUSIC_PLAYER,
  USB_MSC
};

extern lv_obj_t* usbMscScreen;
void create_usb_msc_screen(void);
void update_usb_msc_screen(void);

extern ScreenState currentScreenState;
extern bool isLcdSleeping;
extern uint32_t lastTouchActivityMs;
extern uint32_t sleepTimeoutMs;

extern float stbyVoltage;
extern float vddVoltage;
extern float espVoltage;
extern float heatsinkTemp;
extern float rtcTemp;
extern uint8_t telemetryVuL;
extern uint8_t telemetryVuR;
extern uint8_t telemetryBands[64];
extern uint8_t telemetryBandsLen;
extern bool isBtEnabledState;
extern bool isBtInputActive;
extern bool amplifierPowerOn;
extern bool isInitializingState;
extern bool isBtFailedPerm;
extern lv_obj_t* mboxInitializing;
extern lv_obj_t* mboxError;
extern char currentTimeStr[24];

extern bool isIndependentBypassMode;
extern bool isUartLinkOk;
extern uint32_t lastTelemetryRxMs;
extern bool linkBypassed;
extern lv_obj_t* labelNoLinkIndicator;

extern bool isSelftestActive;
extern bool isSelftestFailed;
extern lv_obj_t* btnSilenceAlarm;
extern bool selftestReportReceived;
extern bool selftestStepOk;
extern std::string selftestErrorDetail;
extern float selftestSmpsVolt;
extern float selftestStbyVolt;

void panel_comms_init();
void panel_comms_tick();
void parse_json_message(const char* json_str);
void panel_send_commands_list();
void send_uart_command(const char* json_cmd);
void panel_send_log(const char* level, const char* msg);

void panel_ui_init();
void panel_ui_switch_screen(ScreenState state);
void panel_ui_enter_deep_sleep();
void panel_ui_update_telemetry();
void append_boot_log(const char* msg);

#endif // PANEL_MAIN_H
