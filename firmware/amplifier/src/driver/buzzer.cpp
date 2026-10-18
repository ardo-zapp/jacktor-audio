#include "buzzer.h"
#include "config.h"
#include "sensors.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <driver/ledc.h>
#include <ctime>
#include <cstdint>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef BUZZER_ACTIVE_LOW
#define BUZZER_ACTIVE_LOW 1
#endif

struct BuzzPatternDef {
  const BuzzStep *steps;
  size_t count;
  uint32_t repeatIntervalMs;
  const char *toneName;
  bool bypassQuiet;
  bool fatal;
};

static constexpr uint16_t POWER_DUTY = 307;

// Classic PC Motherboard BIOS POST beep: crisp, solid single beep at 1000 Hz for 200 ms
static const BuzzStep PATTERN_BOOT[] = {
  {1000, 200, BUZZER_DUTY_STRONG},
};

static const BuzzStep PATTERN_SHUTDOWN[] = {
  {800, 160, BUZZER_DUTY_DEFAULT}, {0, 40, 0},
  {500, 240, BUZZER_DUTY_DEFAULT},
};

static const BuzzStep PATTERN_BT[] = {
  {1318, 140, BUZZER_DUTY_DEFAULT}, {0, 50, 0},
  {1760, 220, BUZZER_DUTY_DEFAULT}
};

static const BuzzStep PATTERN_AUX[] = {
  {1046, 220, BUZZER_DUTY_DEFAULT}
};

// Normal UI button click feedback: clear, crisp, audible 65 ms tone at 2200 Hz
static const BuzzStep PATTERN_CLICK[] = {
  {2200, 65, BUZZER_DUTY_DEFAULT}
};

static const BuzzStep PATTERN_WARNING[] = {
  {1200, 150, BUZZER_DUTY_STRONG}, {0, 80, 0},
  {1200, 150, BUZZER_DUTY_STRONG}
};

static const BuzzStep PATTERN_ERROR[] = {
  {600, 220, BUZZER_DUTY_STRONG}, {0, 100, 0},
  {600, 250, BUZZER_DUTY_STRONG}
};

static const BuzzStep PATTERN_PROTECT_LONG[] = {
  {650, 350, BUZZER_DUTY_STRONG}, {0, 150, 0}
};

static const BuzzStep PATTERN_SMPS_ERROR[] = {
  {700, 250, BUZZER_DUTY_STRONG}, {0, 120, 0},
  {700, 250, BUZZER_DUTY_STRONG}, {0, 120, 0},
  {700, 400, BUZZER_DUTY_STRONG}
};

static const BuzzStep PATTERN_WARNING_LOW_VOLT[] = {
  {1200, 120, BUZZER_DUTY_STRONG}, {0, 70, 0},
  {1200, 120, BUZZER_DUTY_STRONG}, {0, 70, 0},
  {1200, 120, BUZZER_DUTY_STRONG}
};

static const BuzzStep PATTERN_ERROR_CONTINUOUS[] = {
  {800, 1000, POWER_DUTY}, {0, 100, 0}
};

static const BuzzPatternDef PATTERNS[] = {
  {nullptr, 0, 0, "none", true, false},
  {PATTERN_BOOT, sizeof(PATTERN_BOOT) / sizeof(BuzzStep), 0, "boot", true, true},
  {PATTERN_SHUTDOWN, sizeof(PATTERN_SHUTDOWN) / sizeof(BuzzStep), 0, "shutdown", true, true},
  {PATTERN_BT, sizeof(PATTERN_BT) / sizeof(BuzzStep), 0, "bt", false, false},
  {PATTERN_AUX, sizeof(PATTERN_AUX) / sizeof(BuzzStep), 0, "aux", false, false},
  {PATTERN_CLICK, sizeof(PATTERN_CLICK) / sizeof(BuzzStep), 0, "click", false, false},
  {PATTERN_WARNING, sizeof(PATTERN_WARNING) / sizeof(BuzzStep), 0, "warn", false, false},
  {PATTERN_ERROR, sizeof(PATTERN_ERROR) / sizeof(BuzzStep), 0, "error", true, true},
  {PATTERN_PROTECT_LONG, sizeof(PATTERN_PROTECT_LONG) / sizeof(BuzzStep), 0, "protect_long", true, true},
  {PATTERN_SMPS_ERROR, sizeof(PATTERN_SMPS_ERROR) / sizeof(BuzzStep), 0, "smps_error", true, true},
  {PATTERN_WARNING_LOW_VOLT, sizeof(PATTERN_WARNING_LOW_VOLT) / sizeof(BuzzStep), 0, "warn_low_volt", true, true},
  {PATTERN_ERROR_CONTINUOUS, sizeof(PATTERN_ERROR_CONTINUOUS) / sizeof(BuzzStep), 0, "err_cont", true, true},
};

static bool prefsReady = false;

struct BuzzerConfig {
  bool enabled;
  uint8_t volume;
  bool quietEnabled;
  uint8_t quietStart, quietEnd;
};

static BuzzerConfig config = {true, 100, false, 0, 0};
static const BuzzPatternDef *current = nullptr;
static size_t stepIndex = 0;
static uint32_t stepEndMs = 0;
static bool customActive = false;
static const char *lastTone = "none";
static uint32_t lastToneMs = 0;
static uint32_t mutedUntilMs = 0;

static constexpr uint32_t MIN_TONE_INTERVAL_MS = 80;
static constexpr uint32_t QUIET_PACK_ENABLE_BIT = 1u << 16;
static constexpr uint32_t LEDC_MAX = (1u << BUZZER_PWM_RES_BITS) - 1u;

static inline uint32_t ms() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void ensurePrefsLoaded() {
  if (prefsReady) return;
  nvs_handle_t handle;
  if (nvs_open("dev_bz", NVS_READWRITE, &handle) == ESP_OK) {
      uint8_t en = 1;
      nvs_get_u8(handle, "enabled", &en);
      config.enabled = (en != 0);

      uint8_t vol = 100;
      nvs_get_u8(handle, "volume", &vol);
      config.volume = vol;

      uint32_t quietRaw = 0;
      nvs_get_u32(handle, "quiet", &quietRaw);
      config.quietEnabled = (quietRaw & QUIET_PACK_ENABLE_BIT) != 0;
      config.quietStart = (quietRaw >> 8) & 0xFF;
      config.quietEnd = quietRaw & 0xFF;

      nvs_close(handle);
  }
  if (config.quietStart >= 24 || config.quietEnd >= 24) {
    config.quietEnabled = false;
    config.quietStart = 0;
    config.quietEnd = 0;
  }
  if (config.volume > 100) config.volume = 100;
  prefsReady = true;
}

static void persistEnabled() {
  if (!prefsReady) return;
  nvs_handle_t handle;
  if (nvs_open("dev_bz", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_set_u8(handle, "enabled", config.enabled ? 1 : 0);
      nvs_commit(handle);
      nvs_close(handle);
  }
}

static void persistVolume() {
  if (!prefsReady) return;
  nvs_handle_t handle;
  if (nvs_open("dev_bz", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_set_u8(handle, "volume", config.volume);
      nvs_commit(handle);
      nvs_close(handle);
  }
}

static void persistQuiet() {
  if (!prefsReady) return;
  uint32_t raw = (config.quietEnabled ? QUIET_PACK_ENABLE_BIT : 0u)
               | ((uint32_t)config.quietStart << 8)
               | (uint32_t)config.quietEnd;
  nvs_handle_t handle;
  if (nvs_open("dev_bz", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_set_u32(handle, "quiet", raw);
      nvs_commit(handle);
      nvs_close(handle);
  }
}

static bool quietHoursActiveNow() {
  if (!config.quietEnabled) return false;
  if (config.quietStart == config.quietEnd) return false;
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
  return hour >= config.quietStart || hour < config.quietEnd;
}

static uint16_t applyVolume(uint16_t duty) {
  if (duty == 0) return 0;
  if (config.volume >= 100) return duty;
  uint32_t scaled = (uint32_t)duty * (uint32_t)config.volume / 100u;
  if (scaled > LEDC_MAX) scaled = LEDC_MAX;
  return (uint16_t)scaled;
}

static esp_timer_handle_t buzzerStepTimer = NULL;

static void buzzerOff();
static void startStep();

static void IRAM_ATTR buzzerStepTimerCallback(void *arg) {
  if (!config.enabled || !current) {
    buzzerOff();
    current = nullptr;
    return;
  }
  stepIndex++;
  if (stepIndex < current->count) {
    startStep();
  } else {
    buzzerOff();
    current = nullptr;
  }
}

static void buzzerOff() {
  if (buzzerStepTimer) {
    esp_timer_stop(buzzerStepTimer);
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static bool toneAllowed(const BuzzPatternDef *pat, uint32_t now) {
  if (!pat || !pat->steps || pat->count == 0) return false;
  if (!config.enabled) return false;
  if (!pat->bypassQuiet && quietHoursActiveNow()) return false;
  if (!pat->fatal && (now - lastToneMs) < MIN_TONE_INTERVAL_MS) return false;
  if (now < mutedUntilMs) return false;
  return true;
}

static void setBuzzerTone(uint32_t freqHz, uint16_t duty) {
  if (freqHz == 0 || duty == 0) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
    return;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_1, freqHz);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

static void startStep() {
  if (!config.enabled || !current || stepIndex >= current->count) {
    buzzerOff();
    current = nullptr;
    return;
  }
  const BuzzStep &step = current->steps[stepIndex];
  stepEndMs = ms() + step.durationMs;

  if (step.freqHz == 0 || step.durationMs == 0 || step.duty == 0) {
    setBuzzerTone(0, 0);
  } else {
    uint16_t duty = applyVolume(step.duty);
    setBuzzerTone(step.freqHz, duty);
  }

  if (buzzerStepTimer && step.durationMs > 0) {
    esp_timer_stop(buzzerStepTimer);
    esp_timer_start_once(buzzerStepTimer, (uint64_t)step.durationMs * 1000ULL);
  }
}

void buzzerInit() {
  ensurePrefsLoaded();

  if (!buzzerStepTimer) {
    esp_timer_create_args_t timer_args = {
      .callback = &buzzerStepTimerCallback,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "buzzer_step",
      .skip_unhandled_events = true
    };
    esp_timer_create(&timer_args, &buzzerStepTimer);
  }

  // Configure LEDC Timer for Buzzer
  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_timer.duty_resolution = LEDC_TIMER_10_BIT;
  ledc_timer.timer_num = LEDC_TIMER_1;
  ledc_timer.freq_hz = BUZZER_PWM_BASE_FREQ;
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&ledc_timer);

  // Configure LEDC Channel for Buzzer
  ledc_channel_config_t ledc_channel = {};
  ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_channel.channel = LEDC_CHANNEL_1;
  ledc_channel.timer_sel = LEDC_TIMER_1;
  ledc_channel.gpio_num = BUZZER_PIN;
  ledc_channel.duty = 0;
  ledc_channel.hpoint = 0;
  ledc_channel_config(&ledc_channel);

  buzzerOff();
  current = nullptr;
  customActive = false;
  mutedUntilMs = 0;
}

void buzzSetEnabled(bool enabled, bool persist) {
  ensurePrefsLoaded();
  config.enabled = enabled;
  if (!config.enabled) {
    current = nullptr;
    customActive = false;
    buzzerOff();
  }
  if (persist) persistEnabled();
}

bool buzzerEnabled() {
  ensurePrefsLoaded();
  return config.enabled;
}

void buzzerSetVolume(uint8_t percent, bool persist) {
  ensurePrefsLoaded();
  if (percent > 100) percent = 100;
  config.volume = percent;
  if (persist) persistVolume();
}

uint8_t buzzerGetVolume() {
  ensurePrefsLoaded();
  return config.volume;
}

void buzzerSetQuietHours(bool enabled, uint8_t startHour, uint8_t endHour, bool persist) {
  ensurePrefsLoaded();
  if (startHour >= 24 || endHour >= 24) {
    enabled = false;
    startHour = 0;
    endHour = 0;
  }
  config.quietEnabled = enabled;
  config.quietStart = startHour;
  config.quietEnd = endHour;
  if (persist) persistQuiet();
}

void buzzerGetQuietHours(bool &enabled, uint8_t &startHour, uint8_t &endHour) {
  ensurePrefsLoaded();
  enabled = config.quietEnabled;
  startHour = config.quietStart;
  endHour = config.quietEnd;
}

bool buzzerQuietHoursActive() { return quietHoursActiveNow(); }

void buzzLiftMute() { mutedUntilMs = 0; }

void buzzStop() {
  current = nullptr;
  customActive = false;
  buzzerOff();
}

void buzzPattern(BuzzPatternId pattern) {
  ensurePrefsLoaded();
  size_t index = (size_t)pattern;
  if (index >= (size_t)BuzzPatternId::COUNT) return;
  const BuzzPatternDef *pat = &PATTERNS[index];
  uint32_t now = ms();
  if (!toneAllowed(pat, now)) return;

  buzzStop();

  current = pat;
  stepIndex = 0;
  customActive = false;
  lastTone = pat->toneName;
  lastToneMs = now;
  startStep();
}

void buzzerClick() { buzzPattern(BuzzPatternId::CLICK); }

static BuzzStep customPatternSteps[24];
static BuzzPatternDef customPatternDef = {
  customPatternSteps, 0, 0, "custom", true, true
};

void buzzerCustomSequence(const BuzzStep *steps, size_t count) {
  ensurePrefsLoaded();
  if (!config.enabled || !steps || count == 0) {
    buzzStop();
    return;
  }
  uint32_t now = ms();
  buzzStop();

  size_t n = count > 24 ? 24 : count;
  for (size_t i = 0; i < n; ++i) {
    customPatternSteps[i] = steps[i];
  }
  customPatternDef.count = n;
  customPatternDef.steps = customPatternSteps;
  customPatternDef.toneName = "custom";

  current = &customPatternDef;
  stepIndex = 0;
  customActive = false;
  lastTone = "custom";
  lastToneMs = now;
  startStep();
}

void buzzerCustom(uint32_t freqHz, uint16_t duty, uint16_t msDur) {
  BuzzStep step = {(uint16_t)freqHz, (uint16_t)msDur, (uint16_t)duty};
  buzzerCustomSequence(&step, 1);
}

void buzzTick(uint32_t now) {
  if (!config.enabled) {
    buzzerOff();
    return;
  }

  if (!current) return;

  if (stepIndex >= current->count) {
    buzzerOff();
    current = nullptr;
  }
}

bool buzzerIsActive() {
  if (!config.enabled) return false;
  if (customActive) return true;
  return current && (stepIndex < current->count);
}

const char* buzzerLastTone() { return lastTone; }
uint32_t buzzerLastToneAt() { return lastToneMs; }

void buzzerFactoryReset() {
  ensurePrefsLoaded();
  nvs_handle_t handle;
  if (nvs_open("dev_bz", NVS_READWRITE, &handle) == ESP_OK) {
      nvs_erase_all(handle);
      nvs_commit(handle);
      nvs_close(handle);
  }
  config.enabled = true;
  config.volume = 100;
  config.quietEnabled = false;
  config.quietStart = 0;
  config.quietEnd = 0;
  persistEnabled();
  persistVolume();
  persistQuiet();
}