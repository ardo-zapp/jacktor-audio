#include "power_fault.h"
#include "main.h"
#include "config.h"
#include "cJSON.h"
#include "comms_internal.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "state.h"
#include "sensors.h"
#include "power.h"
#include "comms.h"
#include "buzzer.h"
#include "ota.h"
#include "gaia.h"
#include "tm1637.h"
#include "esp_rom_sys.h"

void sendDebugLog(const char* msg);
#if LOG_ENABLE
  #define LOGF(...)  do { \
    char _log_buf[256]; \
    int _n = snprintf(_log_buf, sizeof(_log_buf), __VA_ARGS__); \
    while (_n > 0 && (_log_buf[_n - 1] == '\n' || _log_buf[_n - 1] == '\r')) { \
      _log_buf[--_n] = '\0'; \
    } \
    if (_n > 0) { \
      sendDebugLog(_log_buf); \
    } \
  } while (0)
#else
  #define LOGF(...)  do {} while (0)
#endif

static inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static bool powerInitDone = false;
static uint32_t standbyBuzzAllowUntil = 0;
static bool bootTonePlayed = false;
static bool initLogPrinted = false;
static bool standbyBuzzStopped = false;

static bool btnLogicalState = false;
static uint32_t btnStableSince = 0;
static uint32_t btnLockoutUntil = 0;

static bool isPowerButtonPressed() {
  return gpio_get_level((gpio_num_t)BTN_POWER_PIN) == 0;
}

static void onPowerStateChanged(PowerState prev, PowerState now, PowerChangeReason reason);

static void ensureMainRelayOffRaw() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << RELAY_SMPS_PIN) | (1ULL << RELAY_AUDIO_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  gpio_set_level((gpio_num_t)RELAY_SMPS_PIN, RELAY_SMPS_ACTIVE_HIGH ? 0 : 1);
  gpio_set_level((gpio_num_t)RELAY_AUDIO_PIN, RELAY_AUDIO_ACTIVE_HIGH ? 0 : 1);
}

static inline void ensureSpeakerPinsOffRaw() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << SPEAKER_POWER_SWITCH_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  gpio_set_level((gpio_num_t)SPEAKER_POWER_SWITCH_PIN, 0);
}

static inline void ensureBtPinsOffRaw() {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << BT_ENABLE_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  gpio_set_level((gpio_num_t)BT_ENABLE_PIN, 0);
}

static const char* powerReasonToStr(PowerChangeReason reason) {
  switch (reason) {
    case PowerChangeReason::Button:       return "button";
    case PowerChangeReason::Command:      return "command";
    case PowerChangeReason::SmpsFaultDetect:     return "smps_fault";
    case PowerChangeReason::FactoryReset: return "factory_reset";
    default:                              return "unknown";
  }
}

static void onPowerStateChanged(PowerState prev, PowerState now, PowerChangeReason reason) {
  if (prev == now) return;

#if LOG_ENABLE
  const char* prevStr = (prev == PowerState::On) ? "on" : "standby";
  const char* nowStr  = (now  == PowerState::On) ? "on" : "standby";
  LOGF("[POWER] %s -> %s (%s)\n", prevStr, nowStr, powerReasonToStr(reason));
#endif

  if (reason != PowerChangeReason::Button) {
     btnLogicalState = isPowerButtonPressed();
     btnStableSince = 0;
     btnLockoutUntil = millis() + 500;
  }

  if (now == PowerState::On) {
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    bootTonePlayed = false;
    standbyBuzzStopped = false;

    tm1637ShowText("on", 3000);
    buzzStop();
  } else {
    tm1637ShowText("off", 2000);
    if (!powerSmpsHwFaultLatched() && !powerSmpsTripLatched()) {
      LOGF("[Terminal] Force standby from callback\n");
      buzzStop();
      buzzPattern(BuzzPatternId::SHUTDOWN);
      standbyBuzzAllowUntil = millis() + 450;
    } else {
      LOGF("[Terminal] Skip standby Terminal override due to latched error\n");
      standbyBuzzAllowUntil = 0;
    }

    bootTonePlayed = false;
    standbyBuzzStopped = false;

    ensureMainRelayOffRaw();
    ensureSpeakerPinsOffRaw();
    ensureBtPinsOffRaw();

    stateSaveRuntimeStats();
  }
}

static void playFactoryResetTone() {
  for (int i = 0; i < 2; ++i) {
    buzzerCustom(1200, BUZZER_DUTY_STRONG, 140);
    delay(200);
    buzzTick(millis());
  }
  buzzStop();
}

void appPerformFactoryReset(const char* subtitle, const char* src) {
  (void)subtitle;
  playFactoryResetTone();
  stateFactoryReset();
  buzzerFactoryReset();

  if (powerInitDone) {
    powerSetMainRelay(false, PowerChangeReason::FactoryReset);
  } else {
    ensureMainRelayOffRaw();
  }

  commsLogFactoryReset(src);

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "factory_reset");
  char *out = cJSON_PrintUnformatted(root);
  if (out) {
    linkSerial.println(out);
    free(out);
  }
  cJSON_Delete(root);

  delay(1500);
  esp_restart();
}

volatile int g_appTickStage = 0;
volatile uint32_t g_lastTickActiveMs = 0;

void appInit() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector to prevent false resets from dynamic audio rail dips
  ensureMainRelayOffRaw();
  ensureSpeakerPinsOffRaw();
  ensureBtPinsOffRaw();
  commsInit();

  esp_reset_reason_t rr = esp_reset_reason();
#if LOG_ENABLE
  delay(20);
  LOGF("\n[%s] %s v%s | ResetReason: %d\n", "BOOT", FW_NAME, FW_VERSION, (int)rr);
#endif

  gpio_config_t btn_conf = {};
  btn_conf.intr_type = GPIO_INTR_DISABLE;
  btn_conf.mode = GPIO_MODE_INPUT;
  btn_conf.pin_bit_mask = (1ULL << BTN_POWER_PIN);
#if FEAT_FACTORY_RESET_COMBO
  btn_conf.pin_bit_mask |= (1ULL << BTN_BOOT_PIN);
#endif
  btn_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  btn_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&btn_conf);

  buzzerInit();
  stateInit();
  powerRegisterStateListener(onPowerStateChanged);
  gaiaInit();
  sensorsInit();
  powerInit();
  tm1637Init();

  btnLogicalState = isPowerButtonPressed();
  btnStableSince = 0;
  btnLockoutUntil = 0;

  powerInitDone = true;
  bootTonePlayed = false;
  standbyBuzzStopped = false;

#if OTA_ENABLE
  otaInit();
#endif

  if (!initLogPrinted) {
    LOGF("[INIT] done.\n");
    LOGF("[STATS] Flight Time: %lu mins, Peak Temp: %.1fC, Last Fault: %s\n",
         (unsigned long)stateGetTotalMinutes(), stateGetPeakTemp(), stateGetLastFault());
    initLogPrinted = true;
  }

}

void appTick() {
  static bool lastAnalyzerEnabled = false;
  static bool lastBtMode = false;
  static bool lastStandbyState = false;

  const uint32_t now = millis();
  g_lastTickActiveMs = now;

  g_appTickStage = 1;
  sensorsTick(now);
  g_appTickStage = 2;
  powerTick(now);

  bool powerOn = powerIsOn();
  bool powerStandby = powerIsStandby();
  bool smpsValid = powerSmpsIsValid();

  bool currentBtnRaw = isPowerButtonPressed();

  if (stateGetBtnLock()) {
    btnLogicalState = currentBtnRaw;
    btnStableSince = 0;
  }

  if (currentBtnRaw != btnLogicalState) {
    if (btnStableSince == 0) {
      btnStableSince = now;
    } else if (now - btnStableSince >= BTN_POWER_DEBOUNCE_MS) {
      btnLogicalState = currentBtnRaw;
      btnStableSince = 0;

      if (btnLogicalState && now >= btnLockoutUntil) {
        buzzerClick();

        bool realPowerOn = powerIsOn();
        if (powerSmpsHwFaultLatched() || powerSmpsTripLatched()) {
           LOGF("[MAIN] Button pressed: Clearing latched error state to Standby.\n");
           powerSetMainRelay(false, PowerChangeReason::Button);
        } else {
           LOGF("[MAIN] Button pressed: Toggling power from %s to %s.\n",
                realPowerOn ? "ON" : "OFF", !realPowerOn ? "ON" : "OFF");
           powerSetMainRelay(!realPowerOn, PowerChangeReason::Button);
        }
        btnLockoutUntil = now + 800;

        powerOn = powerIsOn();
        powerStandby = powerIsStandby();
      }
    }
  } else {
    btnStableSince = 0;
  }

  const bool analyzerShouldRun = powerOn && (stateSelfTestGetStatus() != SelfTestStatus::RUNNING);
  if (analyzerShouldRun != lastAnalyzerEnabled) {
    sensorsSetAnalyzerEnabled(analyzerShouldRun);
    lastAnalyzerEnabled = analyzerShouldRun;
  }

  const bool protectFault = powerSpkProtectFault();
  const bool qccFault = powerQccProtectFault();
  const bool silenced = powerAlarmsSilenced();

  if (powerOn) {
    static uint32_t tempNanStartMs = 0;
    bool tempNan = false;

    if (sensorsHasDs18b20() && std::isnan(getHeatsinkC()) && (now > 10000) && !powerIsInitializing()) {
        if (tempNanStartMs == 0) tempNanStartMs = now;
        if (now - tempNanStartMs >= 5000) {
            tempNan = true;
        }
    } else {
        tempNanStartMs = 0;
    }

    const bool smpsFault = powerSmpsHwFaultLatched() || powerSmpsTripLatched();
    static uint32_t lastErrorLogMs = 0;

    const bool isSelftest = (stateSelfTestGetStatus() == SelfTestStatus::RUNNING);

    if (!isSelftest && (protectFault || qccFault || smpsFault || tempNan)) {
        if (!silenced) {
            if (!buzzerIsActive()) {
                if (protectFault || qccFault) buzzPattern(BuzzPatternId::PROTECT_LONG);
                else if (smpsFault) buzzPattern(BuzzPatternId::SMPS_ERROR);
                else if (tempNan) {
                    buzzPattern(BuzzPatternId::PROTECT_LONG);
                    if (std::strcmp(stateGetLastFault(), "temp_sensor_nan") != 0) {
                        stateSetLastFault("temp_sensor_nan");
                    }
                }
            }
        } else {
            buzzStop();
        }

        if (now - lastErrorLogMs >= 2000) {
            lastErrorLogMs = now;
            if (!silenced) {
                if (protectFault) commsLog("error", "speaker_protector_fault_gpio34_low");
                if (qccFault) commsLog("error", "qcc_power_detect_fault_gpio5_low");
                if (smpsFault) commsLog("error", "smps_fault_detected");
                if (tempNan) commsLog("error", "temp_sensor_nan_disconnect");
            }
        }
    }

    if (smpsValid && !isSelftest) {
      if (!bootTonePlayed && !powerSmpsHwFaultLatched() && !powerSmpsTripLatched()) {
        buzzPattern(BuzzPatternId::BOOT);
        bootTonePlayed = true;
        lastBtMode = powerBtMode();
        LOGF("[BOOT] SMPS valid, playing boot tone\n");
      }
    }
  } else {
    if (now >= standbyBuzzAllowUntil && !standbyBuzzStopped) {
      if (powerSmpsHwFaultLatched() || powerSmpsTripLatched()) {
        standbyBuzzStopped = true;
      } else {
        if (std::strcmp(buzzerLastTone(), "custom") != 0) {
          buzzStop();
        }
        standbyBuzzStopped = true;
        LOGF("[BUZZ] Standby buzz stopped after grace period\n");
      }
    }
  }

  if (powerStandby) {
    if (!lastStandbyState) {
      ensureMainRelayOffRaw();
      ensureSpeakerPinsOffRaw();
      if (powerSmpsHwFaultLatched() || powerSmpsTripLatched()) {
         LOGF("[MAIN] powerStandby=true, but Terminal standby is shielded due to latched error\n");
      } else {
         LOGF("[MAIN] powerStandby=true, forcing Terminal standby\n");
      }
      lastStandbyState = true;
    }
  } else {
    lastStandbyState = false;
  }

  const bool sqw = sensorsSqwConsumeTick();
  g_appTickStage = 3;
  commsTick(now, sqw);

#if OTA_ENABLE
  g_appTickStage = 4;
  otaTick(now);
#endif

  g_appTickStage = 5;
  gaiaTick(now);

  if (powerOn && !powerSmpsHwFaultLatched()) {
    const bool btMode = powerBtMode();
    if (btMode != lastBtMode) {
      if (stateSelfTestGetStatus() != SelfTestStatus::RUNNING) {
        buzzPattern(btMode ? BuzzPatternId::ENTER_BT : BuzzPatternId::ENTER_AUX);
      }
      lastBtMode = btMode;
    }
  } else if (!powerOn) {
    lastBtMode = false;
  }

  g_appTickStage = 6;
  buzzTick(now);
  g_appTickStage = 7;
  stateTick();
  g_appTickStage = 8;
  tm1637Tick();
  g_appTickStage = 9;
}

void appSafeReboot() {
  LOGF("[SYS] reboot...\n");
  delay(50);
  esp_restart();
}

extern "C" void app_main(void) {
  appInit();
  for (;;) {
    appTick();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
