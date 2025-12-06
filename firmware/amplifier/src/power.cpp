#include "power.h"
#include "config.h"
#include "state.h"
#include "sensors.h"
#include "comms.h"

#ifndef LOGF
#define LOGF(...) do {} while (0)
#endif

#include <driver/ledc.h>

// -------------------- Fan PWM --------------------
static constexpr ledc_mode_t      FAN_LEDC_MODE   = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t     FAN_LEDC_TIMER  = LEDC_TIMER_0;
static constexpr ledc_channel_t   FAN_LEDC_CH     = (ledc_channel_t)FAN_PWM_CH;
static constexpr ledc_timer_bit_t FAN_LEDC_RES    = (ledc_timer_bit_t)FAN_PWM_RES_BITS;
// ===================================================


// -------------------- Static state --------------------
static bool   sRelayOn = false;
static bool   sRelayRequested = false;

static bool   sSpkBig  = false;
static bool   sSpkPwr  = false;

static bool   sBtEn    = false;
static bool   sBtHwOn  = false;
static bool   sBtMode  = false;  // true=BT, false=AUX

static bool   sSpkHwOn = false;

static bool   sOta     = false;
static bool   safeModeActive = false;

static bool   sSpkProtectOk = true;     // LED ON = OK (active-high)
static uint32_t protectLastChangeMs = 0;
static bool   protectFaultLatched = false;
static bool   protectFaultLogged = false;

static uint32_t btLastEnteredBtMs = 0;  // reset timer auto-off ketika masuk BT
static uint32_t btLastAuxMs       = 0;  // melacak lama berada di AUX
static uint32_t btLowSinceMs      = 0;  // AUX -> BT hold
static uint32_t btLossSinceMs     = 0;  // BT -> AUX loss hold

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

static uint32_t ms() { return millis(); }

// Fan duty 0..1023
static void fanWriteDuty(uint16_t duty) {
  if (duty > 1023) duty = 1023;

  const uint32_t val = duty;

  ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CH, val);
  ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CH);
}

// Linear map suhu → duty:
// - t <= tLow  : kipas jalan pelan (dutyMin)
// - tLow..tHigh: linear dutyMin → dutyMax
// - t >= tHigh : dutyMax
static uint16_t fanCurveAuto(float tC) {
  constexpr uint16_t dutyMin = 250;
  constexpr uint16_t dutyMax = 1023;

  if (isnan(tC)) {
    return dutyMin;
  }

  constexpr float tLow  = 40.0f;
  constexpr float tHigh = 80.0f;

  if (tC <= tLow) {
    return dutyMin;
  }

  if (tC >= tHigh) {
    return dutyMax;
  }

  const float f = (tC - tLow) / (tHigh - tLow);   // 0..1
  return static_cast<uint16_t>(
    dutyMin + f * (dutyMax - dutyMin)
  );
}

// Fan control
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
  bool shouldOn = sBtEn
                  && sRelayOn
                  && !safeModeActive
                  && !powerSmpsSoftstartActive();

  if (shouldOn) {
    const uint32_t idleMs = stateBtAutoOffMs();
    if (idleMs > 0) {
      if (!sBtMode && btLastAuxMs != 0 && (now - btLastAuxMs) >= idleMs) {
        shouldOn = false;
      }
    }
  }

  if (shouldOn != sBtHwOn) {
    digitalWrite(BT_ENABLE_PIN, shouldOn ? HIGH : LOW);
    sBtHwOn = shouldOn;
  }
}

static void applySpeakerPower() {
  bool shouldOn = sSpkPwr
                  && sRelayOn
                  && !safeModeActive
                  && !powerSmpsSoftstartActive();

  if (FEAT_SMPS_PROTECT_ENABLE) {
    shouldOn = shouldOn && !smpsFaultLatched && !smpsCutActive;
  }

  if (shouldOn != sSpkHwOn) {
    digitalWrite(SPEAKER_POWER_SWITCH_PIN, shouldOn ? HIGH : LOW);
    sSpkHwOn = shouldOn;
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
  sSpkPwr = stateSpeakerPowerOn(); // Hanya BACA nilai NVS ke memori
  digitalWrite(SPEAKER_SELECTOR_PIN,     sSpkBig ? HIGH : LOW);
  digitalWrite(SPEAKER_POWER_SWITCH_PIN, LOW); // PAKSA MATI saat boot
  sSpkHwOn = false; // Pastikan pelacak status sinkron

  // ---------- Bluetooth ----------
  pinMode(BT_ENABLE_PIN, OUTPUT);
  pinMode(BT_STATUS_PIN, INPUT);
  sBtEn   = stateBtEnabled();
  sBtHwOn = false;
  sBtMode = false;
  btLastEnteredBtMs = 0;
  btLastAuxMs       = now;
  btLowSinceMs      = 0;

#if FEAT_BT_BUTTONS_ENABLE
  pinMode(BT_BTN_PLAY_PIN, OUTPUT);
  pinMode(BT_BTN_PREV_PIN, OUTPUT);
  pinMode(BT_BTN_NEXT_PIN, OUTPUT);

  digitalWrite(BT_BTN_PLAY_PIN, LOW);
  digitalWrite(BT_BTN_PREV_PIN, LOW);
  digitalWrite(BT_BTN_NEXT_PIN, LOW);
#endif

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
  {
    ledc_timer_config_t fanTimerCfg = {
      .speed_mode      = FAN_LEDC_MODE,
      .duty_resolution = FAN_LEDC_RES,
      .timer_num       = FAN_LEDC_TIMER,
      .freq_hz         = FAN_PWM_FREQ,
      .clk_cfg         = LEDC_AUTO_CLK
    };
    ledc_timer_config(&fanTimerCfg);

    ledc_channel_config_t fanChCfg = {
      .gpio_num   = FAN_PWM_PIN,
      .speed_mode = FAN_LEDC_MODE,
      .channel    = FAN_LEDC_CH,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = FAN_LEDC_TIMER,
      .duty       = 0,
      .hpoint     = 0
    };
    ledc_channel_config(&fanChCfg);

    // Boot test seperti dulu: kipas "ngeroll" sebentar
    if (FEAT_FAN_BOOT_TEST) {
      fanWriteDuty(FAN_BOOT_TEST_DUTY);
      delay(FAN_BOOT_TEST_MS);
    }
    fanBootTestDone = true;
    fanWriteDuty(0);   // mulai dari OFF
  }

  // ---------- Apply BT HW & safe-mode ----------
  applyBtHardware(now);
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

void powerTick(const uint32_t now) {
  // ---------------- Fan control ----------------
  if (powerIsOn()) {
    fanTick();

    if (safeModeActive) {
      fanWriteDuty(0);
    }
  } else {
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
    // Secara logika: true = BT connected/aktif (nama lowNow cuma sejarah)
    const bool btActive = _readBtStatusActiveLow();

    if (btActive) {
      // Status "connected" stabil → reset loss timer
      btLossSinceMs = 0;

      if (!sBtMode) {
        // Lagi di AUX → butuh hold dulu sebelum pindah ke BT
        if (btLowSinceMs == 0) btLowSinceMs = now;
        if ((now - btLowSinceMs) >= AUX_TO_BT_LOW_MS) {
          sBtMode          = true;
          btLastEnteredBtMs = now;
          btLastAuxMs       = 0;
        }
      } else {
        // Sudah di BT, cuma update waktu terakhir di BT
        if (btLastEnteredBtMs == 0) btLastEnteredBtMs = now;
        btLastAuxMs = 0;
      }
    } else {
      // BT status kelihatan "putus"
      btLowSinceMs = 0;

      if (sBtMode) {
        // Hanya kalau sekarang memang di BT → mulai hitung loss
        if (btLossSinceMs == 0) btLossSinceMs = now;
        if ((now - btLossSinceMs) >= BT_TO_AUX_LOSS_MS) {
          sBtMode    = false;
          btLastAuxMs = now;
        }
      } else {
        // Sudah di AUX, cukup catat kapan terakhir AUX
        btLossSinceMs = 0;
        if (btLastAuxMs == 0) {
          btLastAuxMs = now;
        }
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
  applyBtHardware(now);

  // ---------------- Apply Speaker Power state ----------------
  applySpeakerPower();
}


// ---------------- Relay ----------------
void powerSetMainRelay(bool on, PowerChangeReason reason) {
  const bool prevOn = sRelayOn;

  sRelayRequested = on;
  if (safeModeActive) {
    on = false;
  }

  // Transisi ON -> Standby: reset semua timer idle BT
  if (!on && prevOn) {
    btLastAuxMs   = 0;
    btLowSinceMs  = 0;
    btLossSinceMs = 0;    // kalau kamu sudah pakai delay BT->AUX loss
  }

  if (!on) {
    // Saat mematikan: bersihkan status SMPS & protector
    smpsCutActive      = false;
    smpsFaultLatched   = false;
    protectFaultLatched = false;
    sSpkProtectOk      = true;
  }

  applyRelay(on);
  const bool nowOn = sRelayOn;

  if (on && !prevOn) {
    // Set ulang soft-start & arming protector setiap kali power ON
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    smpsCutActive        = false;
    smpsFaultLatched     = false;
    spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
#if FEAT_PC_DETECT_ENABLE
    pcGraceUntilMs = millis() + PC_DETECT_GRACE_MS;
#endif
  }

  applyBtHardware(millis());
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
  stateSetSpeakerPowerOn(on);
}

bool powerGetSpeakerPower() { return sSpkPwr; }

// ---------------- Bluetooth ----------------
void powerSetBtEnabled(bool en) {
  sBtEn = en;
  stateSetBtEnabled(en);
  uint32_t now = ms();
  applyBtHardware(now);
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