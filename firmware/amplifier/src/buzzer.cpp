#include "buzzer.h"
#include "config.h"
#include "sensors.h"

#include <Preferences.h>
#include <driver/ledc.h>
#include <ctime>
#include <Arduino.h>

// === Hardware polarity =======================================================
// Jika buzzer pasif dengan transistor ke GND (aktif-LOW), set 1
#ifndef BUZZER_ACTIVE_LOW
#define BUZZER_ACTIVE_LOW 1
#endif

// ============================================================================

struct BuzzStep {
  uint16_t freqHz;
  uint16_t durationMs;
  uint16_t duty;
};

struct BuzzPatternDef {
  const BuzzStep *steps;
  size_t          count;
  uint32_t        repeatIntervalMs;
  const char     *toneName;
  bool            bypassQuiet;
  bool            fatal;
};

// ------------------ Patterns -------------------------------------------------
static const BuzzStep PATTERN_BOOT[] = {
  {880,  90, BUZZER_DUTY_DEFAULT},
  {0,    30, 0},
  {1175, 90, BUZZER_DUTY_DEFAULT},
  {0,    30, 0},
  {1568, 120, BUZZER_DUTY_DEFAULT},
};

static const BuzzStep PATTERN_SHUTDOWN[] = {
  {1568, 90, BUZZER_DUTY_DEFAULT},
  {0,    30, 0},
  {1175, 90, BUZZER_DUTY_DEFAULT},
  {0,    30, 0},
  {880,  120, BUZZER_DUTY_DEFAULT},
};

static const BuzzStep PATTERN_BT[]      = { {1568, 60, BUZZER_DUTY_DEFAULT}, {0, 40, 0}, {2093, 80, BUZZER_DUTY_DEFAULT} };
static const BuzzStep PATTERN_AUX[]     = { {1175, 60, BUZZER_DUTY_DEFAULT} };
static const BuzzStep PATTERN_CLICK[]   = { {3000, 25, BUZZER_DUTY_STRONG} };
static const BuzzStep PATTERN_WARNING[] = { {1175, 70, BUZZER_DUTY_DEFAULT} };
static const BuzzStep PATTERN_ERROR[]   = { {880,  70, BUZZER_DUTY_STRONG}, {0, 100, 0}, {880, 120, BUZZER_DUTY_STRONG} };
// Beep 220ms, jeda 180ms, repeat setiap 800ms
static const BuzzStep PATTERN_PROTECT_LONG[] = { {750, 220, BUZZER_DUTY_STRONG}, {0, 180, 0} };

static const BuzzPatternDef PATTERNS[] = {
  {nullptr, 0, 0, "none",      true,  false},
  {PATTERN_BOOT,      sizeof(PATTERN_BOOT)      / sizeof(BuzzStep), 0,   "boot",         true,  true },
  {PATTERN_SHUTDOWN,  sizeof(PATTERN_SHUTDOWN)  / sizeof(BuzzStep), 0,   "shutdown",     true,  true },
  {PATTERN_BT,        sizeof(PATTERN_BT)        / sizeof(BuzzStep), 0,   "bt",           false, false},
  {PATTERN_AUX,       sizeof(PATTERN_AUX)       / sizeof(BuzzStep), 0,   "aux",          false, false},
  {PATTERN_CLICK,     sizeof(PATTERN_CLICK)     / sizeof(BuzzStep), 0,   "click",        false, false},
  {PATTERN_WARNING,   sizeof(PATTERN_WARNING)   / sizeof(BuzzStep), 0,   "warn",         false, false},
  {PATTERN_ERROR,     sizeof(PATTERN_ERROR)     / sizeof(BuzzStep), 0,   "error",        true,  true },
  {PATTERN_PROTECT_LONG, sizeof(PATTERN_PROTECT_LONG)/sizeof(BuzzStep), 800, "protect_long", true, true},
};

// ------------------ State ----------------------------------------------------
static Preferences prefs;
static bool prefsReady = false;

struct BuzzerConfig {
  bool    enabled;
  uint8_t volume;      // 0..100 %
  bool    quietEnabled;
  uint8_t quietStart;  // 0..23
  uint8_t quietEnd;    // 0..23
};
static BuzzerConfig config = {true, 100, false, 0, 0};

static const BuzzPatternDef *gCurrent        = nullptr;
static size_t                 gStepIndex      = 0;
static uint32_t               gStepEndMs      = 0;

static bool                   gCustomActive   = false;
static uint32_t               gCustomEndMs    = 0;

static const char            *gLastTone       = "none";
static uint32_t               gLastToneMs     = 0;
static uint32_t               gMutedUntilMs   = 0;

static constexpr uint32_t MIN_TONE_INTERVAL_MS   = 150;
static constexpr uint32_t QUIET_PACK_ENABLE_BIT  = 1u << 16;
static constexpr uint32_t LEDC_MAX               = (1u << BUZZER_PWM_RES_BITS) - 1u;

static inline uint32_t ms() { return millis(); }

// ------------------ Prefs ----------------------------------------------------
static void ensurePrefsLoaded() {
  if (prefsReady) return;
  prefs.begin("dev/bz", false);
  config.enabled = prefs.getBool("enabled", true);
  config.volume  = prefs.getUChar("volume", 100);
  uint32_t quietRaw = prefs.getUInt("quiet", 0);
  config.quietEnabled = (quietRaw & QUIET_PACK_ENABLE_BIT) != 0;
  config.quietStart   = (quietRaw >> 8) & 0xFF;
  config.quietEnd     = quietRaw & 0xFF;
  if (config.quietStart >= 24 || config.quietEnd >= 24) {
    config.quietEnabled = false;
    config.quietStart = 0; config.quietEnd = 0;
  }
  if (config.volume > 100) config.volume = 100;
  prefsReady = true;
}

static void persistEnabled() { if (prefsReady) prefs.putBool("enabled", config.enabled); }
static void persistVolume()  { if (prefsReady) prefs.putUChar("volume", config.volume); }
static void persistQuiet()   {
  if (!prefsReady) return;
  uint32_t raw = (config.quietEnabled ? QUIET_PACK_ENABLE_BIT : 0u)
               | ((uint32_t)config.quietStart << 8)
               | (uint32_t)config.quietEnd;
  prefs.putUInt("quiet", raw);
}

// ------------------ Helpers --------------------------------------------------
static bool quietHoursActiveNow() {
  if (!config.quietEnabled) return false;
  if (config.quietStart == config.quietEnd) return false; // 0 span
  uint32_t epoch;
  if (!sensorsGetUnixTime(epoch)) return false;
  time_t t = (time_t)epoch;
  struct tm tmBuf;
  struct tm *tmPtr = gmtime_r(&t, &tmBuf);
  if (!tmPtr) return false;
  uint8_t hour = (uint8_t)(tmPtr->tm_hour % 24);
  if (config.quietStart < config.quietEnd) {
    return hour >= config.quietStart && hour < config.quietEnd;
  }
  return hour >= config.quietStart || hour < config.quietEnd; // melewati tengah malam
}

static uint16_t applyVolume(uint16_t duty) {
  if (duty == 0) return 0;
  if (config.volume >= 100) return duty;
  uint32_t scaled = (uint32_t)duty * (uint32_t)config.volume / 100u;
  if (scaled > LEDC_MAX) scaled = LEDC_MAX;
  return (uint16_t)scaled;
}

// OFF harus lewat LEDC duty, jangan digitalWrite (pin sudah diambil alih LEDC)
static void buzzerOff() {
#if BUZZER_ACTIVE_LOW
  // Aktif-LOW: OFF = HIGH konstan → duty 100%
  ledcWrite(BUZZER_PWM_CH, LEDC_MAX);
#else
  // Aktif-HIGH: OFF = LOW konstan → duty 0%
  ledcWrite(BUZZER_PWM_CH, 0);
#endif
}

static bool toneAllowed(const BuzzPatternDef *pat, uint32_t now) {
  if (!pat || !pat->steps || pat->count == 0) return false;
  if (!config.enabled) return false;
  if (!pat->bypassQuiet && quietHoursActiveNow()) return false;
  if (!pat->fatal && (now - gLastToneMs) < MIN_TONE_INTERVAL_MS) return false;
  if (now < gMutedUntilMs) return false;
  return true;
}

static void startStep(uint32_t now) {
  if (!gCurrent || gStepIndex >= gCurrent->count) {
    buzzerOff(); gCurrent = nullptr; return;
  }
  const BuzzStep &step = gCurrent->steps[gStepIndex];
  gStepEndMs = now + step.durationMs;

  // Silent step / disabled
  if (!config.enabled || step.freqHz == 0 || step.durationMs == 0 || step.duty == 0) {
    buzzerOff(); return;
  }

  uint16_t duty = applyVolume(step.duty);
  if (duty == 0) { buzzerOff(); return; }

  ledcSetup(BUZZER_PWM_CH, step.freqHz, BUZZER_PWM_RES_BITS);

#if BUZZER_ACTIVE_LOW
  // Aktif-LOW: LOW = bunyi. Duty harus dibalik: 0 → OFF, makin besar → makin lama LOW.
  uint32_t invDuty = (duty > LEDC_MAX) ? 0 : (LEDC_MAX - duty);
  ledcWrite(BUZZER_PWM_CH, invDuty);
#else
  ledcWrite(BUZZER_PWM_CH, duty);
#endif
}

// ------------------ Public API ----------------------------------------------
void buzzerInit() {
  ensurePrefsLoaded();
  pinMode(BUZZER_PIN, OUTPUT);
  ledcSetup(BUZZER_PWM_CH, BUZZER_PWM_BASE_FREQ, BUZZER_PWM_RES_BITS);
  ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CH);
  // Pastikan idle benar: aktif-LOW → HIGH 100%, aktif-HIGH → LOW 0%
  buzzerOff();
  gCurrent = nullptr;
  gCustomActive = false;
  gMutedUntilMs = 0;
}

void buzzSetEnabled(bool enabled, bool persist) {
  ensurePrefsLoaded();
  config.enabled = enabled;
  if (!config.enabled) { gCurrent = nullptr; gCustomActive = false; buzzerOff(); }
  if (persist) persistEnabled();
}

bool buzzerEnabled() {
  ensurePrefsLoaded(); return config.enabled;
}

void buzzerSetVolume(uint8_t percent, bool persist) {
  ensurePrefsLoaded(); if (percent > 100) percent = 100;
  config.volume = percent; if (persist) persistVolume();
}

uint8_t buzzerGetVolume() {
  ensurePrefsLoaded(); return config.volume;
}

void buzzerSetQuietHours(bool enabled, uint8_t startHour, uint8_t endHour, bool persist) {
  ensurePrefsLoaded();
  if (startHour >= 24 || endHour >= 24) { enabled = false; startHour = 0; endHour = 0; }
  config.quietEnabled = enabled; config.quietStart = startHour; config.quietEnd = endHour;
  if (persist) persistQuiet();
}

void buzzerGetQuietHours(bool &enabled, uint8_t &startHour, uint8_t &endHour) {
  ensurePrefsLoaded(); enabled = config.quietEnabled; startHour = config.quietStart; endHour = config.quietEnd;
}

bool buzzerQuietHoursActive() { return quietHoursActiveNow(); }

void buzzLiftMute() { gMutedUntilMs = 0; }

void buzzStop() {
  gCurrent = nullptr; gCustomActive = false; buzzerOff();
}

void buzzPattern(BuzzPatternId pattern) {
  ensurePrefsLoaded();
  size_t index = (size_t)pattern;
  if (index >= (size_t)BuzzPatternId::COUNT) return;
  const BuzzPatternDef *pat = &PATTERNS[index];
  uint32_t now = ms();
  if (!toneAllowed(pat, now)) return;
  gCurrent = pat; gStepIndex = 0; gCustomActive = false;
  gLastTone = pat->toneName; gLastToneMs = now;
  startStep(now);
}

void buzzerClick() { buzzPattern(BuzzPatternId::CLICK); }

void buzzerCustom(uint32_t freqHz, uint16_t duty, uint16_t msDur) {
  ensurePrefsLoaded();
  if (!config.enabled) return;
  if (freqHz == 0 || msDur == 0 || duty == 0) { buzzStop(); return; }

  uint32_t now = ms();
  gCurrent = nullptr;
  gCustomActive = true;
  gCustomEndMs = now + msDur;

  uint16_t scaledDuty = applyVolume(duty);
  if (scaledDuty == 0) { buzzerOff(); gCustomActive = false; return; }

  ledcSetup(BUZZER_PWM_CH, freqHz, BUZZER_PWM_RES_BITS);
#if BUZZER_ACTIVE_LOW
  uint32_t invDuty = (scaledDuty > LEDC_MAX) ? 0 : (LEDC_MAX - scaledDuty);
  ledcWrite(BUZZER_PWM_CH, invDuty);
#else
  ledcWrite(BUZZER_PWM_CH, scaledDuty);
#endif

  gLastTone = "custom"; gLastToneMs = now;
}

void buzzTick(uint32_t now) {
  if (!config.enabled) { buzzerOff(); return; }

  if (gCustomActive) {
    if (now >= gCustomEndMs) { gCustomActive = false; buzzerOff(); }
    return;
  }

  if (!gCurrent) return;

  if (gStepIndex >= gCurrent->count) {
    buzzerOff(); gCurrent = nullptr; return;
  }

  if (now >= gStepEndMs) {
    ++gStepIndex;
    if (gStepIndex < gCurrent->count) startStep(now);
    else { buzzerOff(); gCurrent = nullptr; }
  }
}

bool buzzerIsActive() {
  if (!config.enabled) return false;
  if (gCustomActive) return true;
  return gCurrent && (gStepIndex < gCurrent->count);
}

const char* buzzerLastTone() {
  return gLastTone;
}

uint32_t buzzerLastToneAt() {
  return gLastToneMs;
}

void buzzerFactoryReset() {
  ensurePrefsLoaded();
  prefs.clear();
  config.enabled = true;
  config.volume = 100;
  config.quietEnabled = false; config.quietStart = 0; config.quietEnd = 0;
  persistEnabled(); persistVolume(); persistQuiet();
}