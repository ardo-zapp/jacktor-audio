#include <HardwareSerial.h>
HardwareSerial espSerial(2);

#include "main.h"
#include "config.h"
#include <Wire.h>
#include <cstring>
#include <cmath>

#include "state.h"
#include "sensors.h"
#include "power.h"
#include "comms.h"
#include "buzzer.h"
#include "ui.h"
#include "ota.h"

#if LOG_ENABLE
  #define LOGF(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define LOGF(...)  do {} while (0)
#endif

static bool powerInitDone = false;
static uint32_t standbyBuzzAllowUntil = 0;

static void onPowerStateChanged(PowerState prev, PowerState now, PowerChangeReason reason);

static uint8_t relayOffLevel() {
  return RELAY_MAIN_ACTIVE_HIGH ? LOW : HIGH;
}

static void ensureMainRelayOffRaw() {
  pinMode(RELAY_MAIN_PIN, OUTPUT);
  digitalWrite(RELAY_MAIN_PIN, relayOffLevel());
}

static inline void ensureSpeakerPinsOffRaw() {
  pinMode(SPEAKER_POWER_SWITCH_PIN, OUTPUT);
  digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW);
  pinMode(SPEAKER_SELECTOR_PIN, OUTPUT);
  digitalWrite(SPEAKER_SELECTOR_PIN, LOW);
}

static const char* powerReasonToStr(PowerChangeReason reason) {
  switch (reason) {
    case PowerChangeReason::Button:       return "button";
    case PowerChangeReason::Command:      return "command";
    case PowerChangeReason::PcDetect:     return "pc_detect";
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

  if (now == PowerState::On) {
    uiShowBoot(UI_BOOT_HOLD_MS);
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    buzzStop();
    buzzPattern(BuzzPatternId::BOOT);
  } else {
    uiShowStandby();
    buzzStop();
    buzzPattern(BuzzPatternId::SHUTDOWN);
    standbyBuzzAllowUntil = millis() + 450;
  }
}

static void playFactoryResetTone() {
  for (int i = 0; i < 2; ++i) {
    buzzerCustom(1175, BUZZER_DUTY_DEFAULT, 90);
    delay(150);
    buzzTick(millis());
  }
  buzzStop();
}

void appPerformFactoryReset(const char* subtitle, const char* src) {
  uiShowFactoryReset(subtitle, 0);
  playFactoryResetTone();
  stateFactoryReset();
  buzzerFactoryReset();

  if (powerInitDone) {
    powerSetMainRelay(false, PowerChangeReason::FactoryReset);
  } else {
    ensureMainRelayOffRaw();
  }

  commsLogFactoryReset(src);
  delay(1500);
  ESP.restart();
}

static bool isPowerButtonPressed() {
  return digitalRead(BTN_POWER_PIN) == LOW;
}

static bool isBootButtonPressed() {
  return digitalRead(BTN_BOOT_PIN) == LOW;
}

void appInit() {
#if LOG_ENABLE
  Serial.begin(LOG_BAUD);
  delay(20);
  LOGF("\n[%s] %s v%s\n", "BOOT", FW_NAME, FW_VERSION);
#endif

  Wire.begin(I2C_SDA, I2C_SCL);
  ensureMainRelayOffRaw();
  ensureSpeakerPinsOffRaw();

#if FEAT_FACTORY_RESET_COMBO
  pinMode(BTN_BOOT_PIN,  INPUT_PULLUP);
  pinMode(BTN_POWER_PIN, BTN_POWER_INPUT_MODE);
#endif

  buzzerInit();
  stateInit();
  powerRegisterStateListener(onPowerStateChanged);
  commsInit();
  uiInit();
  sensorsInit();
  powerInit();
  powerInitDone = true;
  uiShowStandby();

#if OTA_ENABLE
  otaInit();
#endif

  LOGF("[INIT] done.\n");
}

void appTick() {
  static uint32_t lastUi = 0;
  static bool lastAnalyzerEnabled = true;
  static bool lastBtMode = false;

  static bool lastSpkFault = false, lastSmpsFault = false;
  static uint32_t lastWarnBuzzMs = 0;
  static bool smpsErrorTonePlayed = false;
  static uint32_t smpsValidSince = 0;

  static bool btnInit = false, btnStable = false, btnReported = false;
  static uint32_t btnLastChange = 0;

  const uint32_t now = millis();

  sensorsTick(now);
  powerTick(now);

  bool powerOn = powerIsOn();

#if FEAT_FACTORY_RESET_COMBO
  static bool frDialogActive = false, frAwaitRepress = false;
  static uint32_t frHoldStart = 0;
  static bool frBootPrev = false;

  const bool bootNow = isBootButtonPressed();

  if (!powerOn) {
    if (!frDialogActive) {
      if (bootNow && !frBootPrev) frHoldStart = now;
      if (bootNow && (now - frHoldStart >= BTN_FACTORY_RESET_HOLD_MS)) {
        uiShowFactoryReset("Lepas & tekan BOOT lagi", 0);
        frDialogActive = true;
        frAwaitRepress = false;
      }
    } else {
      if (!frAwaitRepress) {
        if (!bootNow && frBootPrev) frAwaitRepress = true;
      } else {
        if (bootNow && !frBootPrev) {
          appPerformFactoryReset("FACTORY RESET", "boot_btn");
        }
      }
    }
  } else {
    frDialogActive = false;
    frAwaitRepress = false;
  }
  frBootPrev = bootNow;
#endif

  const bool rawBtn = isPowerButtonPressed();
  if (!btnInit) {
    btnStable = btnReported = rawBtn;
    btnLastChange = now;
    btnInit = true;
  }
  if (rawBtn != btnStable) {
    btnStable = rawBtn;
    btnLastChange = now;
  }
  if (now - btnLastChange >= BTN_POWER_DEBOUNCE_MS) {
    if (btnStable != btnReported) {
      btnReported = btnStable;
      if (btnStable) {
        powerSetMainRelay(!powerOn, PowerChangeReason::Button);
        buzzerClick();
        powerOn = powerIsOn();
      }
    }
  }

  const bool analyzerShouldRun = powerOn;
  if (analyzerShouldRun != lastAnalyzerEnabled) {
    sensorsSetAnalyzerEnabled(analyzerShouldRun);
    lastAnalyzerEnabled = analyzerShouldRun;
  }

  const bool protectFault = powerSpkProtectFault();
  const bool smpsBypass = stateSmpsBypass();
  const float voltage = getVoltageInstant();
  const bool inSoftstart = powerSmpsSoftstartActive();

  if (powerOn) {
    const bool smpsNoPower = (!smpsBypass && voltage == 0.0f);
    const bool smpsLowVolt = (!smpsBypass && voltage > 0.0f && voltage < stateSmpsCutoffV());
    const bool smpsFault = (!inSoftstart) && (smpsNoPower || smpsLowVolt);
    const bool warnNow = isnan(getHeatsinkC());

    if (protectFault && !lastSpkFault) {
      uiShowError("SPEAKER PROTECT");
      buzzStop();
      buzzPattern(BuzzPatternId::PROTECT_LONG);
      lastSpkFault = true;
    }
    if (!protectFault && lastSpkFault) {
      buzzStop();
      lastSpkFault = false;
      uiClearErrorToRun();
    }

    if (inSoftstart) {
      if (lastSmpsFault) buzzStop();
      lastSmpsFault = false;
      smpsErrorTonePlayed = false;
      smpsValidSince = 0;
    } else if (smpsFault && !lastSmpsFault) {
      uiShowError("SMPS PROTECT");
      buzzStop();
      buzzPattern(BuzzPatternId::SMPS_ERROR);
      smpsErrorTonePlayed = true;
      lastSmpsFault = true;
      smpsValidSince = 0;
    } else if (!smpsFault && !lastSmpsFault) {
      if (smpsValidSince == 0) smpsValidSince = now;
    }

    if (!smpsFault && lastSmpsFault && !inSoftstart) {
      buzzStop();
      lastSmpsFault = false;
      smpsErrorTonePlayed = false;
      uiClearErrorToRun();
      smpsValidSince = now;
    }

    if (!protectFault && !smpsFault && warnNow) {
      if (now - lastWarnBuzzMs >= 1500) {
        buzzPattern(BuzzPatternId::WARNING);
        lastWarnBuzzMs = now;
      }
    }
  } else {
    if (now >= standbyBuzzAllowUntil) {
      buzzStop();
      lastSpkFault = false;
      lastSmpsFault = false;
      smpsErrorTonePlayed = false;
      smpsValidSince = 0;
    }
  }

  const bool sqw = sensorsSqwConsumeTick();
  commsTick(now, sqw);

#if OTA_ENABLE
  otaTick(now);
#endif

  uiSetInputStatus(powerBtMode(), powerGetSpeakerSelectBig());
  if (powerOn && !(lastSpkFault || lastSmpsFault) && (smpsValidSince != 0 && (now - smpsValidSince >= 3000))) {
    const bool btMode = powerBtMode();
    if (btMode != lastBtMode) {
      buzzPattern(btMode ? BuzzPatternId::ENTER_BT : BuzzPatternId::ENTER_AUX);
      lastBtMode = btMode;
    }
  } else {
    lastBtMode = powerBtMode();
  }

  if (sqw) {
    char iso[20];
    if (sensorsGetTimeISO(iso, sizeof(iso)) && std::strlen(iso) >= 19) {
      uiSetClock(&iso[11]);
      uiSetDate(iso);
    }
  }

  if (now - lastUi >= 33) {
    lastUi = now;
    uiTick(now);
  }

  buzzTick(now);
  stateTick();
}

void appSafeReboot() {
  LOGF("[SYS] reboot...\n");
  delay(50);
  ESP.restart();
}

void setup() { appInit(); }
void loop()  { appTick(); }