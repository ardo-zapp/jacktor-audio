#pragma once
#include <cstdint>
#include <cstddef>
#include "config.h"

// ---- Voltage & Temperature ----
void  sensorsInit();
void  sensorsTick(uint32_t now);

// OneWire Temperature Sensor Roles (Class AB, Class D Subwoofer, SMPS)
enum TempSensorRole {
    TEMP_ROLE_CLASS_AB = 0,
    TEMP_ROLE_CLASS_D  = 1,
    TEMP_ROLE_SMPS     = 2,
    TEMP_ROLE_COUNT    = 3
};

struct TempSensorDeviceInfo {
    uint64_t rom;
    float tempC;
    int role; // -1 if unassigned, or TempSensorRole enum
    bool online;
};

float getVoltageInstant();   // 65V SMPS Voltage (ADS1115 A0, without smoothing)
float sensorsReadSmpsDirect(); // Direct read from ADS1115 channel 0
float getVoltageStandby();   // Standby Power Voltage (ADS1115 A1)
float getVoltageVdd();       // 3.3V VDD Voltage (ADS1115 A2)
float getVoltageEsp32();     // ESP32 5V Power Voltage (ADS1115 A3)
float getHeatsinkC();        // °C (Highest Temperature across all assigned sensors) or NAN if invalid
float sensorsGetTempClassAB(); // °C Class AB amplifier sensor
float sensorsGetTempClassD();  // °C Class D subwoofer amplifier sensor
float sensorsGetTempSmps();    // °C SMPS power supply sensor
float sensorsGetRtcTempC();  // °C DS3231 internal RTC or NAN

uint64_t sensorsGetRomClassAB();
uint64_t sensorsGetRomClassD();
uint64_t sensorsGetRomSmps();
bool sensorsAssignTempRom(TempSensorRole role, uint64_t rom);
bool sensorsSwapTempRoles(TempSensorRole roleA, TempSensorRole roleB);
bool sensorsIsTempOnline(TempSensorRole role);
size_t sensorsScan1WireBus(TempSensorDeviceInfo* outList, size_t maxCount);

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
bool  sensorsGetColonState();                    // Standard digital clock colon: true on even seconds (1s ON), false on odd seconds (1s OFF)
bool  sensorsGetRtcTime(uint8_t& hours, uint8_t& minutes);
bool  sensorsGetUnixTime(uint32_t& epochOut);    // epoch seconds (UTC)
bool  sensorsSetUnixTime(uint32_t epoch);        // set RTC to epoch UTC
bool  sensorsIsRtcReady();                       // true if DS3231 detected on I2C bus
bool  sensorsIsRtcColdBootDetected();            // true if DS3231 detected during cold boot
bool  sensorsHasDs18b20();                       // true if DS18B20 sensor detected at boot
bool  sensorsIsSqwHwValid();                     // true only if physical SQW 1 Hz pulse (~1000ms period) is active on GPIO 36
float sensorsGetSqwFrequency();                  // Measured pulse frequency in Hz (e.g. 1.00 Hz, or 0.0 Hz if disconnected)
const char* sensorsGetSqwStatusStr();            // "VALID_1HZ", "DISCONNECTED", or "INVALID_FREQ"