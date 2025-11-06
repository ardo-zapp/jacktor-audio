#include "power.h"
#include "config.h"
#include "state.h"
#include "sensors.h"
#include "comms.h"

#ifndef LOGF
#define LOGF(...) do {} while (0)
#endif

#include <driver/ledc.h>

// -------------------- Static state --------------------
static bool   sRelayOn = false;
static bool   sRelayRequested = false;

static bool   sSpkBig  = false;
static bool   sSpkPwr  = false;

static bool   sBtEn    = false;
static bool   sBtHwOn  = false;
static bool   sBtMode  = false;  // true=BT, false=AUX

static bool   sOta     = false;
static bool   safeModeActive = false;

static bool   sSpkProtectOk = true;     // LED ON = OK (active-high)
static uint32_t protectLastChangeMs = 0;
static bool   protectFaultLatched = false;
static bool   protectFaultLogged = false;

static uint32_t btLastEnteredBtMs = 0;  // reset timer auto-off ketika masuk BT
static uint32_t btLastAuxMs       = 0;  // melacak lama berada di AUX

// AUX→BT LOW stabil >= 3s; jika SUDAH BT lalu HIGH → segera AUX
static uint32_t btLowSinceMs      = 0;

// PC detect
static bool     pcOn = false;
static bool     pcRaw = false;
static uint32_t pcLastRawMs = 0;
static uint32_t pcGraceUntilMs = 0;
static uint32_t pcOffSchedAt = 0;       // jadwal OFF (now+delay) saat PC OFF

// Fan
static bool     fanBootTestDone = false;

// SMPS undervolt fault latch
static bool     smpsFaultLatched = false;
static bool     smpsCutActive    = false;

static uint32_t spkProtectArmUntilMs = 0;

static PowerStateListener powerListener = nullptr;

static void notifyPowerChange(bool prevOn, bool nowOn, PowerChangeReason reason) {
  if (!powerListener || prevOn == nowOn) return;
  PowerState prev = prevOn ? PowerState::On : PowerState::Standby;
  PowerState now  = nowOn ? PowerState::On : PowerState::Standby;
  powerListener(prev, now, reason);
}

// SMPS soft-start window (proteksi di-suspend sampai waktu ini)
static uint32_t smpsSoftstartUntilMs = 0;

// -------------------- Helpers --------------------
static inline void _writeRelay(bool on) {
#if RELAY_MAIN_ACTIVE_HIGH
  digitalWrite(RELAY_MAIN_PIN, on ? HIGH : LOW);
#else
  digitalWrite(RELAY_MAIN_PIN, on ? LOW : HIGH);
#endif
  sRelayOn = on;
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

static inline uint32_t ms() { return millis(); }

// Fan duty 0..1023
static inline void fanWriteDuty(uint16_t duty) {
  if (duty > 1023) duty = 1023;
  ledcWrite(FAN_PWM_CH, duty);
}

// Linear map T → duty (t1..t2..t3)
static uint16_t fanCurveAuto(float tC) {
  if (isnan(tC)) return (FAN_AUTO_D1 + FAN_AUTO_D2) / 2;
  if (tC <= FAN_AUTO_T1_C) return FAN_AUTO_D1;
  if (tC >= FAN_AUTO_T3_C) return FAN_AUTO_D3;

  if (tC <= FAN_AUTO_T2_C) {
    float f = (tC - FAN_AUTO_T1_C) / (FAN_AUTO_T2_C - FAN_AUTO_T1_C);
    return (uint16_t)(FAN_AUTO_D1 + f * (FAN_AUTO_D2 - FAN_AUTO_D1));
  } else {
    float f = (tC - FAN_AUTO_T2_C) / (FAN_AUTO_T3_C - FAN_AUTO_T2_C);
    return (uint16_t)(FAN_AUTO_D2 + f * (FAN_AUTO_D3 - FAN_AUTO_D2));
  }
}

static void fanTick() {
  FanMode m = stateGetFanMode();
  uint16_t duty = 0;

  switch (m) {
    case FanMode::AUTO: {
      float t = getHeatsinkC();
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

static void applyBtHardware() {
  bool shouldOn = sBtEn && sRelayOn && !safeModeActive;
  if (shouldOn != sBtHwOn) {
    digitalWrite(BT_ENABLE_PIN, shouldOn ? HIGH : LOW);
    sBtHwOn = shouldOn;
  }
}

static void smpsProtectTick() {
  if (!FEAT_SMPS_PROTECT_ENABLE) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    if (sRelayOn != sRelayRequested) applyRelay(sRelayRequested);
    return;
  }

  // Tunda proteksi selama soft-start agar voltase/ADC sempat stabil
  if (millis() < smpsSoftstartUntilMs) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    if (sRelayOn != sRelayRequested) applyRelay(sRelayRequested);
    return;
  }

  if (stateSmpsBypass()) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    if (sRelayOn != sRelayRequested) applyRelay(sRelayRequested);
    return;
  }

  if (!sRelayRequested) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    if (sRelayOn) applyRelay(false);
    return;
  }

  float v = getVoltageInstant();
  float cutoff = stateSmpsCutoffV();
  float recover = stateSmpsRecoveryV();

  if (!smpsCutActive && sRelayOn && v > 0.0f && v < cutoff) {
    smpsCutActive = true;
    smpsFaultLatched = true;
    applyRelay(false);
  }

  if (smpsCutActive && v >= recover) {
    smpsCutActive = false;
    smpsFaultLatched = false;
    if (sRelayRequested) applyRelay(true);
  }
}

// -------------------- Public API --------------------
void powerInit() {
  safeModeActive = stateSafeModeSoft();

  const uint32_t now = millis();

  // ---------- SMPS soft-start ----------
  // Selalu beri jendela soft-start di cold boot
  powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
  smpsCutActive     = false;
  smpsFaultLatched  = false;

  // ---------- Relay utama ----------
  pinMode(RELAY_MAIN_PIN, OUTPUT);
  applyRelay(false);                // default OFF saat boot
  sRelayRequested   = false;

  // ---------- Speaker control ----------
  pinMode(SPEAKER_POWER_SWITCH_PIN, OUTPUT);
  pinMode(SPEAKER_SELECTOR_PIN,     OUTPUT);
  sSpkBig = stateSpeakerIsBig();
  sSpkPwr = safeModeActive ? false : stateSpeakerPowerOn();
  digitalWrite(SPEAKER_SELECTOR_PIN,     sSpkBig ? HIGH : LOW);
  digitalWrite(SPEAKER_POWER_SWITCH_PIN, (safeModeActive ? false : sSpkPwr) ? HIGH : LOW);

  // ---------- Bluetooth ----------
  pinMode(BT_ENABLE_PIN, OUTPUT);
  pinMode(BT_STATUS_PIN, INPUT);
  sBtEn  = stateBtEnabled();
  sBtHwOn = false;
  sBtMode = false;                  // default AUX, autoswitch akan mengubah saat sBtHwOn aktif
  btLastEnteredBtMs = 0;
  btLastAuxMs       = now;
  btLowSinceMs      = 0;

  // ---------- PC Detect ----------
  pinMode(PC_DETECT_PIN, PC_DETECT_INPUT_PULL);
  pcRaw  = _readPcDetectActiveLow();
  pcOn   = pcRaw;
  pcLastRawMs     = now;
  pcGraceUntilMs  = now + PC_DETECT_GRACE_MS;
  pcOffSchedAt    = 0;

  // ---------- Speaker protector input ----------
  pinMode(SPK_PROTECT_LED_PIN, INPUT);               // GPIO34..39: input-only, no internal pull
  sSpkProtectOk        = _readSpkProtectLedActiveHigh();
  protectLastChangeMs  = now;
  protectFaultLatched  = false;
  protectFaultLogged   = false;

  // ---------- Fan PWM ----------
  ledcSetup(FAN_PWM_CH,  FAN_PWM_FREQ, FAN_PWM_RES_BITS);
  ledcAttachPin(FAN_PWM_PIN, FAN_PWM_CH);
  if (FEAT_FAN_BOOT_TEST) {
    fanWriteDuty(FAN_BOOT_TEST_DUTY);
    delay(FAN_BOOT_TEST_MS);
  }
  fanBootTestDone = true;
  fanTick();

  // ---------- Apply BT HW & safe-mode ----------
  applyBtHardware();
  if (safeModeActive) {
    fanWriteDuty(0);
    digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW);
    sSpkPwr = false;
#if LOG_ENABLE
    LOGF("[SAFE] Jacktor Audio safe-mode active\n");
#endif
    commsLog("warn", "safe_mode");
  }

  // ---------- Arming window speaker protector ----------
  // Mulai cek protector hanya setelah: soft-start selesai + tambahan arming modul
  spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
}

void powerTick(uint32_t now) {
  // ---------------- Fan control ----------------
  fanTick();
  if (safeModeActive) {
    fanWriteDuty(0);
  }

  // ---------------- SMPS protect (update status) ----------------
  smpsProtectTick();

  // ---------------- Speaker protector monitor ----------------
#if FEAT_SPK_PROTECT_ENABLE
  // Boleh cek HANYA jika: power ON, soft-start selesai,
  // - bila FEAT_SMPS_PROTECT_ENABLE=1 → SMPS sehat (tidak cut/latched)
  // - bila FEAT_SMPS_PROTECT_ENABLE=0 → lewati syarat itu tapi tetap tunggu soft-start
  // DAN sudah melewati arming window modul protector.
  const bool smpsPassed =
      sRelayOn &&
      !powerSmpsSoftstartActive() &&
      (FEAT_SMPS_PROTECT_ENABLE ? (!smpsCutActive && !smpsFaultLatched) : true);

  const bool protectArmed = (now >= spkProtectArmUntilMs);

  if (smpsPassed && protectArmed) {
    // Speaker protector LED → latch fault jika OFF (sesuai logika active-high/low) stabil ≥ SPK_PROTECT_FAULT_MS
    bool ok = _readSpkProtectLedActiveHigh();
    if (ok != sSpkProtectOk) {
      sSpkProtectOk = ok;
      protectLastChangeMs = now;
    } else {
      if (!sSpkProtectOk && !protectFaultLatched) {
        if (now - protectLastChangeMs >= SPK_PROTECT_FAULT_MS) {
          protectFaultLatched = true;
        }
      }
      if (sSpkProtectOk && protectFaultLatched) {
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
    // Suspend monitor sebelum siap → cegah false-positive saat boot/soft-start
    protectFaultLatched = false;
    sSpkProtectOk = true;
  }
#else
  // Feature OFF: jangan cek/latch sama sekali
  protectFaultLatched = false;
  sSpkProtectOk = true;
#endif

  // ---------------- BT autoswitch & auto-off ----------------
  if (FEAT_BT_AUTOSWITCH_AUX && sBtHwOn) {
    bool lowNow = _readBtStatusActiveLow();
    if (lowNow) {
      if (!sBtMode) {
        if (btLowSinceMs == 0) btLowSinceMs = now;
        if ((now - btLowSinceMs) >= AUX_TO_BT_LOW_MS) {
          sBtMode = true;
          btLastEnteredBtMs = now;
          btLastAuxMs = 0;
        }
      } else {
        if (btLastEnteredBtMs == 0) btLastEnteredBtMs = now;
        btLastAuxMs = 0;
      }
    } else {
      btLowSinceMs = 0;
      if (sBtMode) {
        sBtMode = false;
        btLastAuxMs = now;
      } else if (btLastAuxMs == 0) {
        btLastAuxMs = now;
      }
    }
  }

  // Auto-off modul BT bila terlalu lama di AUX
  if (sBtEn && sBtHwOn) {
    uint32_t idleMs = stateBtAutoOffMs();
    if (idleMs > 0 && !sBtMode && btLastAuxMs != 0) {
      if ((now - btLastAuxMs) >= idleMs) {
        powerSetBtEnabled(false);
      }
    }
  }

  // ---------------- Auto power via PC detect ----------------
  if (FEAT_PC_DETECT_ENABLE && !sOta && !safeModeActive) {
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

  // ---------------- Apply BT hardware state ----------------
  applyBtHardware();
}


// ---------------- Relay ----------------
void powerSetMainRelay(bool on, PowerChangeReason reason) {
  const bool prevOn = sRelayOn;

  sRelayRequested = on;
  if (safeModeActive) {
    on = false;
  }

  if (!on) {
    // Saat mematikan: bersihkan status SMPS & protector
    smpsCutActive = false;
    smpsFaultLatched = false;
    protectFaultLatched = false;
    sSpkProtectOk = true;
  }

  applyRelay(on);
  const bool nowOn = sRelayOn;

  if (on && !prevOn) {
    // Set ulang soft-start & arming protector setiap kali power ON
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    smpsCutActive     = false;
    smpsFaultLatched  = false;
    spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;

#if FEAT_PC_DETECT_ENABLE
    pcGraceUntilMs = millis() + PC_DETECT_GRACE_MS;
#endif
  }

  applyBtHardware();
  notifyPowerChange(prevOn, nowOn, reason);
}

bool powerMainRelay() { return sRelayOn; }

void powerRegisterStateListener(PowerStateListener listener) { powerListener = listener; }

PowerState powerCurrentState() { return sRelayOn ? PowerState::On : PowerState::Standby; }

// ---------------- Speaker ----------------
void powerSetSpeakerSelect(bool big) {
  sSpkBig = big;
  digitalWrite(SPEAKER_SELECTOR_PIN, big ? HIGH : LOW);
  stateSetSpeakerIsBig(big);
}
bool powerGetSpeakerSelectBig() { return sSpkBig; }

void powerSetSpeakerPower(bool on) {
  sSpkPwr = on;
  bool hw = safeModeActive ? false : on;
  digitalWrite(SPEAKER_POWER_SWITCH_PIN, hw ? HIGH : LOW);
  stateSetSpeakerPowerOn(on);
}
bool powerGetSpeakerPower() { return sSpkPwr; }

// ---------------- Bluetooth ----------------
void powerSetBtEnabled(bool en) {
  sBtEn = en;
  stateSetBtEnabled(en);
  uint32_t now = ms();
  applyBtHardware();
  if (en && sBtHwOn) {
    sBtMode = _readBtStatusActiveLow();
    btLowSinceMs = sBtMode ? now : 0;
    btLastEnteredBtMs = sBtMode ? now : 0;
    btLastAuxMs = sBtMode ? 0 : now;
  }
  if (!en) {
    btLowSinceMs = 0;
    btLastEnteredBtMs = 0;
    btLastAuxMs = now;
  }
}
bool powerBtEnabled() { return sBtEn; }
bool powerBtMode()    { return sBtMode; } // true=BT, false=AUX

// ---------------- OTA guard ----------------
void powerSetOtaActive(bool on) {
  sOta = on;
  // Saat OTA aktif → auto power via PC detect diabaikan (dikelola di tick)
}

// ---------------- Protector Fault ----------------
bool powerSpkProtectFault() {
  // Matikan total dari config
#if !FEAT_SPK_PROTECT_ENABLE
  return false;
#endif

  // Jangan pernah melaporkan fault sebelum semua syarat lolos
  if (!sRelayOn) {
    return false;
  }
  if (powerSmpsSoftstartActive()) {
    return false;
  }
#if FEAT_SMPS_PROTECT_ENABLE
  if (smpsCutActive || smpsFaultLatched) {
    return false;
  }
#endif
  // Wajib tunggu arming window modul protector
  if (millis() < spkProtectArmUntilMs) {
    return false;
  }

  return protectFaultLatched;
}

// ---------------- Input mode string ----------------
const char* powerInputModeStr() { return sBtMode ? "bt" : "aux"; }

bool powerPcDetectLevelActive() { return pcRaw; }
bool powerPcDetectArmed()       { return pcOn; }
uint32_t powerPcDetectLastChangeMs() { return pcLastRawMs; }

bool powerSmpsTripLatched() { return smpsFaultLatched; }

void powerSmpsStartSoftstart(uint32_t msDelay) { smpsSoftstartUntilMs = millis() + msDelay; }
bool powerSmpsSoftstartActive() { return millis() < smpsSoftstartUntilMs; }