#include "power.h"
#include "config.h"
#include "state.h"
#include "sensors.h"
#include "comms.h"

#ifndef LOGF
#define LOGF(...) do {} while (0)
#endif

#include <driver/ledc.h>

static constexpr ledc_mode_t FAN_LEDC_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t FAN_LEDC_TIMER = LEDC_TIMER_0;
static constexpr ledc_channel_t FAN_LEDC_CH = (ledc_channel_t)FAN_PWM_CH;
static constexpr ledc_timer_bit_t FAN_LEDC_RES = (ledc_timer_bit_t)FAN_PWM_RES_BITS;

static bool relayOn = false, relayRequested = false;
static bool spkBig = false, spkPwr = false;
static bool btEn = false, btHwOn = false, btMode = false;
static bool spkHwOn = false;
static bool otaActive = false, safeModeActive = false;
static bool spkProtectOk = true;
static uint32_t protectLastChangeMs = 0;
static bool protectFaultLatched = false, protectFaultLogged = false;

static uint32_t btLastEnteredBtMs = 0, btLastAuxMs = 0;
static uint32_t btLowSinceMs = 0, btLossSinceMs = 0;

static bool pcOn = false, pcRaw = false;
static uint32_t pcLastRawMs = 0, pcGraceUntilMs = 0, pcOffSchedAt = 0;

static bool fanBootTestDone = false;
static bool smpsFaultLatched = false, smpsCutActive = false;
static uint32_t smpsFaultGraceUntilMs = 0;
static uint32_t spkProtectArmUntilMs = 0;
static uint32_t smpsValidSince = 0;

static PowerStateListener powerListener = nullptr;

static void notifyPowerChange(bool prevOn, bool nowOn, PowerChangeReason reason) {
  if (!powerListener || prevOn == nowOn) return;
  PowerState prev = prevOn ? PowerState::On : PowerState::Standby;
  PowerState now = nowOn ? PowerState::On : PowerState::Standby;
  powerListener(prev, now, reason);
}

static uint32_t smpsSoftstartUntilMs = 0;

static inline void _writeRelay(bool on) {
#if RELAY_MAIN_ACTIVE_HIGH
  digitalWrite(RELAY_MAIN_PIN, on ? HIGH : LOW);
#else
  digitalWrite(RELAY_MAIN_PIN, on ? LOW : HIGH);
#endif
  relayOn = on;
}

static inline void applyRelay(bool on) {
  _writeRelay(on);
  powerSetOn(on);
}

static inline bool _readBtStatusActiveLow() {
  int val = digitalRead(BT_STATUS_PIN);
  if (BT_STATUS_ACTIVE_LOW) return val == LOW;
  return val == HIGH;
}

static inline bool _readSpkProtectLedActiveHigh() {
#if SPK_PROTECT_ACTIVE_HIGH
  return digitalRead(SPK_PROTECT_LED_PIN) == HIGH;
#else
  return digitalRead(SPK_PROTECT_LED_PIN) == LOW;
#endif
}

static inline bool _readPcDetectActiveLow() {
#if PC_DETECT_ACTIVE_LOW
  return digitalRead(PC_DETECT_PIN) == LOW;
#else
  return digitalRead(PC_DETECT_PIN) == HIGH;
#endif
}

static uint32_t ms() { return millis(); }

static void fanWriteDuty(uint16_t duty) {
  if (duty > 1023) duty = 1023;
  ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CH, duty);
  ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CH);
}

// 3-point piecewise linear fan curve using config.h values
static uint16_t fanCurveAuto(float tC) {
  if (isnan(tC)) {
    // Sensor error: use safe minimum duty
    return FAN_AUTO_D1;
  }

  // Below T1: constant duty D1
  if (tC <= FAN_AUTO_T1_C) {
    return FAN_AUTO_D1;
  }

  // Between T1 and T2: linear interpolation D1 -> D2
  if (tC <= FAN_AUTO_T2_C) {
    float f = (tC - FAN_AUTO_T1_C) / (FAN_AUTO_T2_C - FAN_AUTO_T1_C);
    return static_cast<uint16_t>(FAN_AUTO_D1 + f * (FAN_AUTO_D2 - FAN_AUTO_D1));
  }

  // Between T2 and T3: linear interpolation D2 -> D3
  if (tC <= FAN_AUTO_T3_C) {
    float f = (tC - FAN_AUTO_T2_C) / (FAN_AUTO_T3_C - FAN_AUTO_T2_C);
    return static_cast<uint16_t>(FAN_AUTO_D2 + f * (FAN_AUTO_D3 - FAN_AUTO_D2));
  }

  // Above T3: constant maximum duty
  return FAN_AUTO_D3;
}

static void fanTick() {
  const FanMode m = stateGetFanMode();
  uint16_t duty = 0;

  switch (m) {
    case FanMode::AUTO: {
      const float t = getHeatsinkC();
      duty = fanCurveAuto(t);
      break;
    }
    case FanMode::CUSTOM:
      duty = stateGetFanCustomDuty();
      break;
    case FanMode::FAILSAFE:
    default:
      duty = FAN_FALLBACK_DUTY;
      break;
  }

  fanWriteDuty(duty);
}

static void applyBtHardware(const uint32_t now) {
  bool shouldOn = btEn && relayOn && !safeModeActive && !powerSmpsSoftstartActive();

  if (shouldOn) {
    const uint32_t idleMs = stateBtAutoOffMs();
    if (idleMs > 0) {
      if (!btMode && btLastAuxMs != 0 && (now - btLastAuxMs) >= idleMs) {
        shouldOn = false;
      }
    }
  }

  if (shouldOn != btHwOn) {
    digitalWrite(BT_ENABLE_PIN, shouldOn ? HIGH : LOW);
    btHwOn = shouldOn;
  }
}

static void applySpeakerPower() {
  bool shouldOn = spkPwr && relayOn && !safeModeActive && !powerSmpsSoftstartActive();

  if (FEAT_SMPS_PROTECT_ENABLE) {
    shouldOn = shouldOn && !smpsFaultLatched && !smpsCutActive;
  }

  if (shouldOn != spkHwOn) {
    digitalWrite(SPEAKER_POWER_SWITCH_PIN, shouldOn ? HIGH : LOW);
    spkHwOn = shouldOn;
  }
}

static void smpsProtectTick() {
  if (!FEAT_SMPS_PROTECT_ENABLE) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    if (relayOn != relayRequested) applyRelay(relayRequested);
    return;
  }

  if (millis() < smpsSoftstartUntilMs) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    if (relayOn != relayRequested) applyRelay(relayRequested);
    return;
  }

  if (stateSmpsBypass()) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    if (relayOn != relayRequested) applyRelay(relayRequested);
    return;
  }

  if (!relayRequested) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    if (relayOn) applyRelay(false);
    return;
  }

  float v = getVoltageInstant();
  float cutoff = stateSmpsCutoffV();
  float recover = stateSmpsRecoveryV();

  if (!smpsCutActive && relayOn && v > 0.0f && v < cutoff) {
    smpsCutActive = true;
    smpsFaultLatched = true;
    smpsFaultGraceUntilMs = millis() + 10000;
  }

  if (smpsCutActive) {
    if (millis() >= smpsFaultGraceUntilMs) {
      applyRelay(false);
      smpsCutActive = false;
      smpsFaultLatched = false;
      smpsFaultGraceUntilMs = 0;
    }

    if (v >= recover) {
      smpsCutActive = false;
      smpsFaultLatched = false;
      smpsFaultGraceUntilMs = 0;
      if (relayRequested) applyRelay(true);
    }
  }
}

void powerInit() {
  safeModeActive = stateSafeModeSoft();
  const uint32_t now = millis();

  powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
  smpsCutActive = false;
  smpsFaultLatched = false;
  smpsFaultGraceUntilMs = 0;
  smpsValidSince = 0;

  pinMode(RELAY_MAIN_PIN, OUTPUT);
  applyRelay(false);
  relayRequested = false;

  pinMode(SPEAKER_POWER_SWITCH_PIN, OUTPUT);
  pinMode(SPEAKER_SELECTOR_PIN, OUTPUT);
  spkBig = stateSpeakerIsBig();
  spkPwr = stateSpeakerPowerOn();
  digitalWrite(SPEAKER_SELECTOR_PIN, spkBig ? HIGH : LOW);
  digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW);
  spkHwOn = false;

  pinMode(BT_ENABLE_PIN, OUTPUT);
  pinMode(BT_STATUS_PIN, INPUT);
  btEn = stateBtEnabled();
  btHwOn = false;
  btMode = false;
  btLastEnteredBtMs = 0;
  btLastAuxMs = now;
  btLowSinceMs = 0;

#if FEAT_BT_BUTTONS_ENABLE
  pinMode(BT_BTN_PLAY_PIN, OUTPUT);
  pinMode(BT_BTN_PREV_PIN, OUTPUT);
  pinMode(BT_BTN_NEXT_PIN, OUTPUT);
  digitalWrite(BT_BTN_PLAY_PIN, LOW);
  digitalWrite(BT_BTN_PREV_PIN, LOW);
  digitalWrite(BT_BTN_NEXT_PIN, LOW);
#endif

  pinMode(PC_DETECT_PIN, PC_DETECT_INPUT_PULL);
  pcRaw = _readPcDetectActiveLow();
  pcOn = pcRaw;
  pcLastRawMs = now;
  pcGraceUntilMs = now + PC_DETECT_GRACE_MS;
  pcOffSchedAt = 0;

  pinMode(SPK_PROTECT_LED_PIN, INPUT);
  spkProtectOk = _readSpkProtectLedActiveHigh();
  protectLastChangeMs = now;
  protectFaultLatched = false;
  protectFaultLogged = false;

  {
    ledc_timer_config_t fanTimerCfg = {
      .speed_mode = FAN_LEDC_MODE,
      .duty_resolution = FAN_LEDC_RES,
      .timer_num = FAN_LEDC_TIMER,
      .freq_hz = FAN_PWM_FREQ,
      .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&fanTimerCfg);

    ledc_channel_config_t fanChCfg = {
      .gpio_num = FAN_PWM_PIN,
      .speed_mode = FAN_LEDC_MODE,
      .channel = FAN_LEDC_CH,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = FAN_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0
    };
    ledc_channel_config(&fanChCfg);

    if (FEAT_FAN_BOOT_TEST) {
      LOGF("[FAN] Boot test: duty=%d for %dms\n", FAN_BOOT_TEST_DUTY, FAN_BOOT_TEST_MS);
      fanWriteDuty(FAN_BOOT_TEST_DUTY);
      delay(FAN_BOOT_TEST_MS);
      LOGF("[FAN] Boot test complete\n");
    }
    fanBootTestDone = true;
    fanWriteDuty(0);
  }

  applyBtHardware(now);
  if (safeModeActive) {
    fanWriteDuty(0);
    digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW);
    spkPwr = false;
#if LOG_ENABLE
    LOGF("[SAFE] safe-mode active\n");
#endif
    commsLog("warn", "safe_mode");
  }

  spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
}

void powerTick(const uint32_t now) {
  // FAN: Always run fanTick(), even in standby (allows manual control and proper auto mode)
  fanTick();

  // Override fan to 0 if safe mode active
  if (safeModeActive) {
    fanWriteDuty(0);
  }

  // SMPS valid tracking only when ON
  if (!powerIsOn()) {
    smpsValidSince = 0;
  }

  smpsProtectTick();

  if (relayOn && !powerSmpsSoftstartActive()) {
    const bool smpsBypass = stateSmpsBypass();
    const float voltage = getVoltageInstant();
    const bool smpsNoPower = (!smpsBypass && voltage == 0.0f);
    const bool smpsLowVolt = (!smpsBypass && voltage > 0.0f && voltage < stateSmpsCutoffV());
    const bool smpsFault = (smpsNoPower || smpsLowVolt);

    if (!smpsFault) {
      if (smpsValidSince == 0) smpsValidSince = now;
    } else {
      smpsValidSince = 0;
    }
  } else {
    smpsValidSince = 0;
  }

#if FEAT_SPK_PROTECT_ENABLE
  const bool smpsPassed = relayOn && !powerSmpsSoftstartActive() &&
      (FEAT_SMPS_PROTECT_ENABLE ? (!smpsCutActive && !smpsFaultLatched) : true);
  const bool protectArmed = (now >= spkProtectArmUntilMs);

  if (smpsPassed && protectArmed) {
    bool ok = _readSpkProtectLedActiveHigh();
    if (ok != spkProtectOk) {
      spkProtectOk = ok;
      protectLastChangeMs = now;
    } else {
      if (!spkProtectOk && !protectFaultLatched) {
        if (now - protectLastChangeMs >= SPK_PROTECT_FAULT_MS) {
          protectFaultLatched = true;
        }
      }
      if (spkProtectOk && protectFaultLatched) {
        protectFaultLatched = false;
      }
    }
    if (protectFaultLatched != protectFaultLogged) {
      protectFaultLogged = protectFaultLatched;
#if LOG_ENABLE
      LOGF(protectFaultLatched ? "[PROTECT] speaker_fail\n" : "[PROTECT] speaker_clear\n");
#endif
    }
  } else {
    protectFaultLatched = false;
    spkProtectOk = true;
  }
#else
  protectFaultLatched = false;
  spkProtectOk = true;
#endif

  if (FEAT_BT_AUTOSWITCH_AUX && btHwOn) {
    const bool btActive = _readBtStatusActiveLow();

    if (btActive) {
      btLossSinceMs = 0;
      if (!btMode) {
        if (btLowSinceMs == 0) btLowSinceMs = now;
        if ((now - btLowSinceMs) >= AUX_TO_BT_LOW_MS) {
          btMode = true;
          btLastEnteredBtMs = now;
          btLastAuxMs = 0;
        }
      } else {
        if (btLastEnteredBtMs == 0) btLastEnteredBtMs = now;
        btLastAuxMs = 0;
      }
    } else {
      btLowSinceMs = 0;
      if (btMode) {
        if (btLossSinceMs == 0) btLossSinceMs = now;
        if ((now - btLossSinceMs) >= BT_TO_AUX_LOSS_MS) {
          btMode = false;
          btLastAuxMs = now;
        }
      } else {
        btLossSinceMs = 0;
        if (btLastAuxMs == 0) btLastAuxMs = now;
      }
    }
  }

  if (FEAT_PC_DETECT_ENABLE && !otaActive && !safeModeActive) {
    bool raw = _readPcDetectActiveLow();
    if (raw != pcRaw) {
      pcRaw = raw;
      pcLastRawMs = now;
    }
    if ((now - pcLastRawMs) >= PC_DETECT_DEBOUNCE_MS) {
      if (raw != pcOn) {
        pcOn = raw;
        if (pcOn) {
          pcGraceUntilMs = now + PC_DETECT_GRACE_MS;
          powerSetMainRelay(true, PowerChangeReason::PcDetect);
        } else {
          pcOffSchedAt = now + PC_DETECT_GRACE_MS;
        }
      }
    }
    if (!pcOn && pcOffSchedAt != 0 && now >= pcOffSchedAt && now >= pcGraceUntilMs) {
      powerSetMainRelay(false, PowerChangeReason::PcDetect);
      pcOffSchedAt = 0;
    }
  } else {
    pcOffSchedAt = 0;
  }

  applyBtHardware(now);
  applySpeakerPower();
}

void powerSetMainRelay(bool on, PowerChangeReason reason) {
  const bool prevOn = relayOn;
  relayRequested = on;
  if (safeModeActive) on = false;

  if (!on && prevOn) {
    btLastAuxMs = 0;
    btLowSinceMs = 0;
    btLossSinceMs = 0;
  }

  if (!on) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    protectFaultLatched = false;
    spkProtectOk = true;
    smpsValidSince = 0;
  }

  applyRelay(on);
  const bool nowOn = relayOn;

  if (on && !prevOn) {
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    smpsCutActive = false;
    smpsFaultLatched = false;
    smpsFaultGraceUntilMs = 0;
    smpsValidSince = 0;
    spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
#if FEAT_PC_DETECT_ENABLE
    pcGraceUntilMs = millis() + PC_DETECT_GRACE_MS;
#endif
  }

  applyBtHardware(millis());
  notifyPowerChange(prevOn, nowOn, reason);
}

bool powerMainRelay() { return relayOn; }
void powerRegisterStateListener(PowerStateListener listener) { powerListener = listener; }
PowerState powerCurrentState() { return relayOn ? PowerState::On : PowerState::Standby; }

void powerSetSpeakerSelect(bool big) {
  spkBig = big;
  digitalWrite(SPEAKER_SELECTOR_PIN, big ? HIGH : LOW);
  stateSetSpeakerIsBig(big);
}

bool powerGetSpeakerSelectBig() { return spkBig; }

void powerSetSpeakerPower(bool on) {
  spkPwr = on;
  stateSetSpeakerPowerOn(on);
}

bool powerGetSpeakerPower() { return spkPwr; }

void powerSetBtEnabled(bool en) {
  btEn = en;
  stateSetBtEnabled(en);
  uint32_t now = ms();

  // Reset auto-off timer when manually enabling BT
  if (en) {
    btLastAuxMs = 0;  // Reset timer BEFORE applyBtHardware()
    btLowSinceMs = 0;
    btLastEnteredBtMs = 0;
  }

  applyBtHardware(now);

  // Update BT mode after hardware applied
  if (en && btHwOn) {
    btMode = _readBtStatusActiveLow();
    btLowSinceMs = btMode ? now : 0;
    btLastEnteredBtMs = btMode ? now : 0;
    btLastAuxMs = btMode ? 0 : now;
  }
  if (!en) {
    btLowSinceMs = 0;
    btLastEnteredBtMs = 0;
    btLastAuxMs = now;
  }
}

bool powerBtEnabled() { return btEn; }
bool powerBtMode() { return btMode; }

void powerSetOtaActive(bool on) { otaActive = on; }

bool powerSpkProtectFault() {
#if !FEAT_SPK_PROTECT_ENABLE
  return false;
#endif
  if (!relayOn) return false;
  if (powerSmpsSoftstartActive()) return false;
#if FEAT_SMPS_PROTECT_ENABLE
  if (smpsCutActive || smpsFaultLatched) return false;
#endif
  if (millis() < spkProtectArmUntilMs) return false;
  return protectFaultLatched;
}

const char* powerInputModeStr() { return btMode ? "bt" : "aux"; }
bool powerPcDetectLevelActive() { return pcRaw; }
bool powerPcDetectArmed() { return pcOn; }
uint32_t powerPcDetectLastChangeMs() { return pcLastRawMs; }
bool powerSmpsTripLatched() { return smpsFaultLatched; }

void powerSmpsStartSoftstart(uint32_t msDelay) { smpsSoftstartUntilMs = millis() + msDelay; }
bool powerSmpsSoftstartActive() { return millis() < smpsSoftstartUntilMs; }
bool powerSmpsIsValid() {
  if (!relayOn) return false;
  if (powerSmpsSoftstartActive()) return false;
  if (smpsValidSince == 0) return false;
  return (millis() - smpsValidSince) >= 3000;
}
