#pragma once
#include <cstdint>
#include <cstddef>
#include "config.h"

// ---- Voltage & Temperature ----
void  sensorsInit();
void  sensorsTick(uint32_t now);

float getVoltageInstant();   // 65V SMPS Voltage (ADS1115 A0, without smoothing)
float sensorsReadSmpsDirect(); // Direct read from ADS1115 channel 0
float getVoltageStandby();   // Standby Power Voltage (ADS1115 A1)
float getVoltageVdd();       // 3.3V VDD Voltage (ADS1115 A2)
float getVoltageEsp32();     // ESP32 5V Power Voltage (ADS1115 A3)
float getHeatsinkC();        // °C (DS18B20) or NAN if invalid
float sensorsGetRtcTempC();  // °C DS3231 internal RTC or NAN

// ---- Analyzer (FFT 8/16/32/64 band) ----
void  analyzerGetBytes(uint8_t* out, size_t n);  // 0..255 per band
void  analyzerGetVu(uint8_t& outVu);             // 0..255 mono VU
void  sensorsSetAnalyzerEnabled(bool en);        // disable in standby mode

// ---- Voltmeter & RTC Probe ----
bool  sensorsRecheckAds1115();                   // Probe & re-init ADS1115 I2C
bool  sensorsRecheckRtc();                       // Re-probe & re-init DS3231 I2C
bool  sensorsGetTimeISO(char* out, size_t n);    // "YYYY-MM-DDTHH:MM:SSZ"
bool  sensorsSqwConsumeTick();                   // true if 1 Hz pulse received
uint32_t sensorsSqwGetTickCount();               // 1 Hz tick counter (HW SQW or SW fallback)
bool  sensorsGetColonState();                    // Standard clock colon: true during 0..499 ms (ON), false during 500..999 ms (OFF)
bool  sensorsGetRtcTime(uint8_t& hours, uint8_t& minutes);
bool  sensorsGetUnixTime(uint32_t& epochOut);    // epoch seconds (UTC)
bool  sensorsSetUnixTime(uint32_t epoch);        // set RTC to epoch UTC
bool  sensorsIsRtcReady();                       // true if DS3231 detected on I2C bus
bool  sensorsIsRtcColdBootDetected();            // true if DS3231 detected during cold boot
bool  sensorsHasDs18b20();                       // true if DS18B20 sensor detected at boot