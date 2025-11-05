#pragma once
#include <Arduino.h>

// Identitas pola standar (dipakai lintas modul untuk event buzzer)
enum class BuzzPatternId : uint8_t {
  NONE = 0,
  BOOT,
  SHUTDOWN,
  ENTER_BT,
  ENTER_AUX,
  CLICK,
  WARNING,
  ERROR,
  COUNT,
  PROTECT_LONG,
};

// Init LEDC untuk buzzer dan load konfigurasi NVS (enabled/volume/quiet hours)
void buzzerInit();

// Aktif/nonaktifkan buzzer global (ketika off → output 0 dan pattern diabaikan)
void buzzSetEnabled(bool enabled, bool persist = true);
bool buzzerEnabled();

// Volume 0..100% (diskalakan terhadap duty pada pattern)
void buzzerSetVolume(uint8_t percent, bool persist = true);
uint8_t buzzerGetVolume();

// Quiet hours (jam start/end 0..23). Ketika enabled, nada non-kritis dibungkam.
void buzzerSetQuietHours(bool enabled, uint8_t startHour, uint8_t endHour, bool persist = true);
void buzzerGetQuietHours(bool &enabled, uint8_t &startHour, uint8_t &endHour);
bool buzzerQuietHoursActive();

// Informasi runtime
const char *buzzerLastTone();
uint32_t buzzerLastToneAt();

// Mainkan pattern preset (lihat BuzzPatternId). buzzPattern(BuzzPatternId::NONE) mematikan pattern aktif.
void buzzPattern(BuzzPatternId pattern);

// Nada click pendek standar untuk aksi UI (panel/tombol)
void buzzerClick();

// Panggil rutin di loop
void buzzTick(uint32_t now);

// Penghenti paksa (mematikan output buzzer)
void buzzStop();

// Nada kustom dari panel (freq Hz, duty 0..1023, durasi ms).
// Duty diabaikan jika > resolusi; akan diklip ke 0..1023.
void buzzerCustom(uint32_t freqHz, uint16_t duty, uint16_t ms);

// Status
bool buzzerIsActive();

// Factory reset (hapus konfigurasi buzzer di NVS)
void buzzerFactoryReset();
