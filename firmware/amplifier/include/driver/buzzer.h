#pragma once
#include <cstdint>

enum class BuzzPatternId : uint8_t {
  NONE = 0,
  BOOT,
  SHUTDOWN,
  ENTER_BT,
  ENTER_AUX,
  CLICK,
  WARNING,
  ERROR,
  PROTECT_LONG,
  SMPS_ERROR,
  WARNING_LOW_VOLT,
  ERROR_CONTINUOUS,
  COUNT
};

void buzzerInit();

void buzzSetEnabled(bool enabled, bool persist = true);
bool buzzerEnabled();

void buzzerSetVolume(uint8_t percent, bool persist = true);
uint8_t buzzerGetVolume();

void buzzerSetQuietHours(bool enabled, uint8_t startHour, uint8_t endHour, bool persist = true);
void buzzerGetQuietHours(bool &enabled, uint8_t &startHour, uint8_t &endHour);
bool buzzerQuietHoursActive();

const char *buzzerLastTone();
uint32_t buzzerLastToneAt();

void buzzPattern(BuzzPatternId pattern);
void buzzerClick();
void buzzTick(uint32_t now);
struct BuzzStep {
  uint16_t freqHz;
  uint16_t durationMs;
  uint16_t duty;
};

void buzzStop();
void buzzerCustom(uint32_t freqHz, uint16_t duty, uint16_t ms);
void buzzerCustomSequence(const BuzzStep *steps, size_t count);
bool buzzerIsActive();
void buzzerFactoryReset();
void buzzLiftMute();