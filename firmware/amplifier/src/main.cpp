#include <HardwareSerial.h>
HardwareSerial espSerial(2);
#include "main.h"
#include "config.h"

#include <Wire.h>
#include <cstring>
#include <cmath>

#include "state.h"    // NVS, rev/hash, default speaker BIG
#include "sensors.h"  // RTC, ADS1115, DS18B20, Analyzer (I2S)
#include "power.h"    // Relay, speaker power/selector, fan PWM, auto PC
#include "comms.h"    // UART link + telemetry + command handler (incl. buzz JSON)
#include "buzzer.h"   // non-blocking scheduler
#include "ui.h"       // OLED kecil (standby clock, status+VU saat ON)
#include "ota.h"      // OTA over UART (verifikasi .bin, reboot)

#if LOG_ENABLE
  #define LOGF(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define LOGF(...)  do {} while (0)
#endif

static bool gPowerInitDone = false;

// Grace window supaya nada SHUTDOWN tidak dipotong saat masuk standby
static uint32_t gStandbyBuzzAllowUntilMs = 0;

static void onPowerStateChanged(PowerState prev, PowerState now, PowerChangeReason reason);

static uint8_t relayOffLevel() { return RELAY_MAIN_ACTIVE_HIGH ? LOW : HIGH; }

static void ensureMainRelayOffRaw() {
  pinMode(RELAY_MAIN_PIN, OUTPUT);
  digitalWrite(RELAY_MAIN_PIN, relayOffLevel());
}
static inline void ensureSpeakerPinsOffRaw() {
  pinMode(SPEAKER_POWER_SWITCH_PIN, OUTPUT); digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW);
  pinMode(SPEAKER_SELECTOR_PIN,      OUTPUT); digitalWrite(SPEAKER_SELECTOR_PIN,      LOW);
}

static const char *powerReasonToStr(PowerChangeReason reason) {
  switch (reason) {
    case PowerChangeReason::Button: return "button";
    case PowerChangeReason::Command: return "command";
    case PowerChangeReason::PcDetect: return "pc_detect";
    case PowerChangeReason::FactoryReset: return "factory_reset";
    default: return "unknown";
  }
}

static void onPowerStateChanged(PowerState prev, PowerState now, PowerChangeReason reason) {
  if (prev == now) return;
#if LOG_ENABLE
  const char *prevStr = prev == PowerState::On ? "on" : "standby";
  const char *nowStr  = now == PowerState::On ? "on" : "standby";
  LOGF("[POWER] %s -> %s (%s)\n", prevStr, nowStr, powerReasonToStr(reason));
#endif

  if (now == PowerState::On) {
    // Pa stikan nada BOOT selalu keluar (preempt pattern lama)
    uiShowBoot(UI_BOOT_HOLD_MS);
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);   // soft-start SMPS tiap ON
    buzzStop();                                   // hentikan pattern lama (ERROR/PROTECT_LONG)
    buzzPattern(BuzzPatternId::BOOT);
  } else {
    // Turun ke Standby (termasuk akibat proteksi): paksa nada SHUTDOWN
    buzzStop();                                   // preempt supaya shutdown terdengar
    buzzPattern(BuzzPatternId::SHUTDOWN);
    gStandbyBuzzAllowUntilMs = millis() + 450;    // beri waktu nada selesai
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
  if (gPowerInitDone) powerSetMainRelay(false, PowerChangeReason::FactoryReset);
  else ensureMainRelayOffRaw();
  commsLogFactoryReset(src);
  delay(1500);
  ESP.restart();
  while (true) { delay(100); }
}

static bool isPowerButtonPressed() {
  const int val = digitalRead(BTN_POWER_PIN);
  if (BTN_POWER_ACTIVE_LOW) return val == LOW;
  return val == HIGH;
}

static void checkManualFactoryResetCombo() {
#if FEAT_FACTORY_RESET_COMBO
  delay(50);
  if (digitalRead(BTN_BOOT_PIN) == LOW && isPowerButtonPressed()) {
    const uint32_t start = millis();
    while (millis() - start < BTN_FACTORY_RESET_HOLD_MS) {
      if (digitalRead(BTN_BOOT_PIN) != LOW || !isPowerButtonPressed()) return;
      delay(10);
    }
    appPerformFactoryReset("FACTORY RESET", "manual");
  }
#endif
}

// ---- Init -------------------------------------------------------------------
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
  pinMode(BTN_BOOT_PIN, INPUT_PULLUP);
  pinMode(BTN_POWER_PIN, BTN_POWER_INPUT_MODE);
#endif

  // Subsystems
  buzzerInit();
  stateInit();
  powerRegisterStateListener(onPowerStateChanged);
  commsInit();
  uiInit();
  checkManualFactoryResetCombo();

  sensorsInit();     // ADS1115, DS18B20, RTC, Analyzer (I2S)
  powerInit();       // relay default OFF; fan PWM ready
  gPowerInitDone = true;

  // Cold boot → langsung standby (tanpa splash)
  uiShowStandby();

#if OTA_ENABLE
  otaInit();
#endif

  LOGF("[INIT] done.\n");
}

// ---- Loop -------------------------------------------------------------------
void appTick() {
  static uint32_t lastUi = 0;
  static bool lastAnalyzerEnabled = true;
  static bool lastBtMode = false;

  // Fault flags & timers
  static bool lastSpkFault      = false;
  static bool lastSmpsFault     = false;
  static uint32_t lastFatalBuzzMs  = 0;
  static uint32_t lastWarnBuzzMs   = 0;
  static uint32_t smpsFaultSinceMs = 0;

  // Debounce tombol
  static bool btnInit = false, btnStable = false, btnReported = false;
  static uint32_t btnLastChange = 0;

  const uint32_t now = millis();

  // 1) Service
  sensorsTick(now);
  powerTick(now);

  bool powerOn = powerIsOn();

  // Debounce tombol power fisik
  const bool rawBtn = isPowerButtonPressed();
  if (!btnInit) { btnStable = rawBtn; btnReported = rawBtn; btnLastChange = now; btnInit = true; }
  if (rawBtn != btnStable) { btnStable = rawBtn; btnLastChange = now; }
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

  // Analyzer ON hanya saat power ON
  const bool analyzerShouldRun = powerOn;
  if (analyzerShouldRun != lastAnalyzerEnabled) {
    sensorsSetAnalyzerEnabled(analyzerShouldRun);
    lastAnalyzerEnabled = analyzerShouldRun;
  }

  // === Fault evaluation ===
  const bool protectFault = powerSpkProtectFault();
  const bool smpsBypass   = stateSmpsBypass();
  const float voltage     = getVoltageInstant();
  const bool inSoftstart  = powerSmpsSoftstartActive();

  const uint32_t ERROR_RETRIGGER_MS = 2000;
  const uint32_t WARN_RETRIGGER_MS  = 1500;

  if (powerOn) {
    const bool smpsNoPower = (!smpsBypass && voltage == 0.0f);
    const bool smpsLowVolt = (!smpsBypass && voltage > 0.0f && voltage < stateSmpsCutoffV());
    const bool smpsFault   = (!inSoftstart) && (smpsNoPower || smpsLowVolt);

    const bool warnNow     = isnan(getHeatsinkC());
    const uint32_t nowMs   = now;

    // Speaker Protector FAIL → nada panjang looping
    if (protectFault && !lastSpkFault) {
      uiShowError("SPEAKER PROTECT");
      buzzStop();
      buzzPattern(BuzzPatternId::PROTECT_LONG);
      lastSpkFault = true;
    }
    if (!protectFault && lastSpkFault) {
      buzzStop();
      lastSpkFault = false;
    }

    // SMPS soft-start: jangan panik dulu
    if (inSoftstart) {
      if (lastSmpsFault) { buzzStop(); lastSmpsFault = false; }
      smpsFaultSinceMs = 0;
    } else if (smpsFault && !lastSmpsFault) {
      // SMPS PROTECT → ERROR singkat + auto-shutdown 10s
      smpsFaultSinceMs = nowMs;
      uiShowError("SMPS PROTECT");
      if (nowMs - lastFatalBuzzMs >= ERROR_RETRIGGER_MS) {
        buzzStop();
        buzzPattern(BuzzPatternId::ERROR);
        lastFatalBuzzMs = nowMs;
      }
      lastSmpsFault = true;
    } else if (smpsFault && lastSmpsFault) {
      if (nowMs - smpsFaultSinceMs > 1000) buzzStop(); // jangan jadi panjang
      if (nowMs - smpsFaultSinceMs >= 10000) powerSetMainRelay(false, PowerChangeReason::Command);
    }
    if (!smpsFault && lastSmpsFault && !inSoftstart) {
      buzzStop(); lastSmpsFault = false; smpsFaultSinceMs = 0;
    }

    // Notice ringan
    if (!protectFault && !smpsFault && warnNow) {
      if (nowMs - lastWarnBuzzMs >= WARN_RETRIGGER_MS) {
        buzzPattern(BuzzPatternId::WARNING);
        lastWarnBuzzMs = nowMs;
      }
    }
  } else {
    // STANDBY: beri grace untuk nada shutdown, lalu diam & reset
    if (now >= gStandbyBuzzAllowUntilMs) {
      buzzStop();
      lastSpkFault = false;
      lastSmpsFault = false;
      smpsFaultSinceMs = 0;
    }
  }

  // 2) Telemetry & command link
  const bool sqw = sensorsSqwConsumeTick();
  commsTick(now, sqw);

#if OTA_ENABLE
  otaTick(now);
#endif

  // Update UI context info + nada ganti input
  uiSetInputStatus(powerBtMode(), powerGetSpeakerSelectBig());
  if (powerOn && !(lastSpkFault || lastSmpsFault)) {
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
    if (sensorsGetTimeISO(iso, sizeof(iso)) && strlen(iso) >= 19) {
      uiSetClock(&iso[11]);   // HH:MM:SS
      uiSetDate(iso);         // yyyy-mm-dd
    }
  }

  // 3) UI kecil
  if (now - lastUi >= 33) { lastUi = now; uiTick(now); }

  // 4) Buzzer & NVS
  buzzTick(now);
  stateTick();
}

void appSafeReboot() {
  LOGF("[SYS] reboot...\n");
  delay(50);
  ESP.restart();
}

// ---- Arduino entry ----------------------------------------------------------
void setup()  { appInit(); }
void loop()   { appTick(); }