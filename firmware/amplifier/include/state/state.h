#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
using String = std::string;
#include <vector>

// Fan modes persisted in NVS
enum class FanMode : uint8_t {
  AUTO     = 0,
  CUSTOM   = 1,
  FAILSAFE = 2
};

// Init & factory reset (erases all NVS keys)
void     stateInit();
void     stateFactoryReset();

// -------- Persisted settings (NVS) --------
bool     stateSpeakerPowerOn();
void     stateSetSpeakerPowerOn(bool on);

FanMode  stateGetFanMode();
void     stateSetFanMode(FanMode m);

uint16_t stateGetFanCustomDuty();            // 0..1023
void     stateSetFanCustomDuty(uint16_t d);

bool     stateSmpsBypass();
void     stateSetSmpsBypass(bool en);

float    stateSmpsCutoffV();
void     stateSetSmpsCutoffV(float v);

float    stateSmpsRecoveryV();
void     stateSetSmpsRecoveryV(float v);

bool     stateBtEnabled();
void     stateSetBtEnabled(bool en);

uint32_t stateBtAutoOffMs();
void     stateSetBtAutoOffMs(uint32_t ms);


bool     stateAutoSenseEnabled();
void     stateSetAutoSenseEnabled(bool en);

uint32_t stateAutoSenseTimeout();
void     stateSetAutoSenseTimeout(uint32_t min);

uint8_t  stateAutoSenseThresh();
void     stateSetAutoSenseThresh(uint8_t thr);

// -------- Live Tuning Parameters --------
float    statePidKp();
void     stateSetPidKp(float kp);

float    statePidKi();
void     stateSetPidKi(float ki);

float    statePidKd();
void     stateSetPidKd(float kd);

float    statePidSetpoint();
void     stateSetPidSetpoint(float sp);

float    stateStbyOffset();
void     stateSetStbyOffset(float ofs);

float    stateOtpThresh();
void     stateSetOtpThresh(float thr);

float    stateSmpsOvThresh();
void     stateSetSmpsOvThresh(float thr);

float    stateStbyOvThresh();
void     stateSetStbyOvThresh(float thr);

float    stateStbyLowThresh();
void     stateSetStbyLowThresh(float thr);

// -------- Professional Features (NVS) --------
uint32_t stateGetTotalMinutes();
void     stateSetTotalMinutes(uint32_t mins);

float    stateGetPeakTemp();
void     stateSetPeakTemp(float t);

void     stateSaveRuntimeStats();

const char* stateGetLastFault();
void     stateSetLastFault(const char* fault);
const std::vector<String>& stateGetFaultHistory();

bool     stateGetBtnLock();
void     stateSetBtnLock(bool lock);

// RTC sync rate-limit (epoch seconds)
uint32_t stateLastRtcSync();
void     stateSetLastRtcSync(uint32_t t);

// -------- Runtime flags (not persisted) --------
bool     powerIsOn();
bool     powerIsStandby();
void     powerSetOn(bool on);

// Safe mode compile-time flag (for diagnostics)
bool     stateSafeModeSoft();

// Automated Hardware Self-Test
enum class SelfTestStatus : uint8_t {
  IDLE = 0,
  RUNNING,
  FINISHED
};

void           stateSelfTestStart();
void           stateSelfTestReset();
static inline void stateSetupStart() { stateSelfTestStart(); }
static inline void stateSetupReset() { stateSelfTestReset(); }
SelfTestStatus stateSelfTestGetStatus();
uint8_t        stateSelfTestGetCurrentStep();
bool           stateSelfTestIsBtStepActive();
bool           stateSelfTestIsBtConnectAllowed();
void           stateSelfTestAcknowledge(); // called after sending report to reset to IDLE
uint16_t       stateSelfTestGetFanDutyOverride(); // returns 0-1023 if testing fan, or >1023 if not overriding

// Diagnostics Results (Valid if status == FINISHED)
struct SelfTestResults {
  bool rtc_ok;
  bool ads1115_ok;
  bool ds18b20_ok;
  bool smps_fault_ok; // true if no latched fault
  bool spk_fault_ok;  // true if no latched fault
  bool gaia_bt_ok;    // true if GAIA BT scan & connection ok
  int8_t gaia_rssi;   // RSSI in dBm
};
void stateSelfTestGetResults(SelfTestResults& outRes);

// System Reboot request (non-blocking)
void     stateRequestReboot(uint32_t delayMs);
bool     stateIsRebootPending();

// Tick ops (placeholder for NVS housekeeping & setup/self-test state machine)
void     stateTick();
