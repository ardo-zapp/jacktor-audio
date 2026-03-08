#pragma once
#include <Arduino.h>

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
void buzzStop();
void buzzerCustom(uint32_t freqHz, uint16_t duty, uint16_t ms);
bool buzzerIsActive();
void buzzerFactoryReset();
void buzzLiftMute();