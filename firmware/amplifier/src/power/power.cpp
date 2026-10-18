#include "power.h"
#include "power_hw.h"
#include "power_fault.h"
#include "config.h"
#include "state.h"
#include "sensors.h"
#include "comms.h"
#include "analyzer.h"
#include "tm1637.h"
#include "buzzer.h"
#include "gaia.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <cstring>

static bool relayOn = false;
static bool relayRequested = false;
static PowerStateListener powerListener = nullptr;

static bool audioRelayOn = false;
static bool spkPwr = false;
static bool spkHwOn = false;

static bool btEn = false;
static bool btHwOn = false;
static bool btMode = false;
static uint32_t btLossSinceMs = 0;
static uint32_t btLowSinceMs = 0;
static uint32_t btLastAuxMs = 0;
static bool btInitializing = false;
static uint32_t btPowerOnMs = 0;
static bool btFailedPerm = false;
static bool btHardCycling = false;
static uint32_t btHardCycleStartMs = 0;

static uint32_t smpsSoftstartUntilMs = 0;
static uint32_t smpsValidSince = 0;
static bool smpsHwFaultLatched = false;

static bool spkProtectOk = true;
static bool protectFaultLatched = false;
static bool protectFaultLogged = false;
static uint32_t spkProtectArmUntilMs = 0;
static uint32_t protectLastChangeMs = 0;
static bool alarmsSilenced = false;
static uint32_t spkHwOnAt = 0;

static bool qccProtectOk = true;
static bool qccProtectFaultLatched = false;
static bool qccProtectActiveLogged = false;
static uint32_t qccProtectArmUntilMs = 0;
static uint32_t qccProtectLastChangeMs = 0;

static bool runtimeBypassActive = false;
static bool otpLatched = false;
static bool safeModeActive = SAFE_MODE_SOFT;
static uint32_t sleepTargetEpoch = 0;
static bool fanBootTestDone = false;
static uint32_t fanBootTestUntilMs = 0;
static bool s_debouncedBt = false;
static uint32_t s_btDebounceMs = 0;
static bool s_lastBtRaw = false;
static uint32_t s_btHighSinceMs = 0;
static uint32_t s_btLowSinceMs = 0;
static uint32_t s_lastHighDurationMs = 0;
static uint32_t s_lastLowDurationMs = 0;

static void btDetectionReset(uint32_t now) {
  s_debouncedBt = powerHwReadBtStatus();
  s_lastBtRaw = s_debouncedBt;
  s_btDebounceMs = now;
  s_btHighSinceMs = s_debouncedBt ? now : 0;
  s_btLowSinceMs = s_debouncedBt ? 0 : now;
  s_lastHighDurationMs = 0;
  s_lastLowDurationMs = 0;
  powerHwResetBtLastLowMs();
  btMode = false;
}

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void delay(uint32_t ms) {
  vTaskDelay(pdMS_TO_TICKS(ms));
}

static void notifyPowerChange(bool prevOn, bool nowOn, PowerChangeReason reason) {
  if (powerListener && prevOn != nowOn) {
    powerListener(prevOn ? PowerState::On : PowerState::Standby,
                  nowOn ? PowerState::On : PowerState::Standby, reason);
  }
}

static void applyRelay(bool on) {
  powerHwSetRelaySmps(on);
  powerHwSetRelayAudio(audioRelayOn);

  if (on != relayOn) {
    relayOn = on;
  }
}

static bool globalLowVoltage = false;
static bool globalSmpsFaultActive = false;

static void applySpeakerPower() {
  if (stateSelfTestGetStatus() == SelfTestStatus::RUNNING) {
    return; // Self-test steps (Step 5a/5b) manage speaker relays directly
  }
  bool shouldHwOn = false;
  const bool bypassSmps = stateSmpsBypass() || runtimeBypassActive;
  if (relayOn && spkPwr) {
    if (powerSmpsSoftstartActive()) shouldHwOn = false;
    else if (FEAT_SMPS_PROTECT_ENABLE && !bypassSmps && (powerFaultSmpsIsCutActive() || powerFaultSmpsIsTripLatched())) shouldHwOn = false;
    else if ((!bypassSmps && globalLowVoltage) || globalSmpsFaultActive) shouldHwOn = false;
    else shouldHwOn = true;
  }
  if (safeModeActive) shouldHwOn = false;
  if (shouldHwOn != spkHwOn) {
    powerHwSetSpeakerPins(shouldHwOn);
    spkHwOn = shouldHwOn;
    if (shouldHwOn) {
        spkHwOnAt = millis();
        spkProtectArmUntilMs = spkHwOnAt + SPK_PROTECT_ARM_MS;
        protectLastChangeMs = spkHwOnAt;
        spkProtectOk = powerHwReadSpkProtect();
    } else {
        spkHwOnAt = 0;
        spkProtectArmUntilMs = 0;
    }
  }
}

static void applyBtHardware(uint32_t now) {
  if (btHardCycling) {
    return;
  }
  bool shouldHwOn = false;
  bool isSelftestRunning = (stateSelfTestGetStatus() == SelfTestStatus::RUNNING);
  const bool bypassSmps = stateSmpsBypass() || runtimeBypassActive;

  if (isSelftestRunning && btEn) {
    shouldHwOn = stateSelfTestIsBtStepActive();
  } else if (btInitializing && btEn) {
    shouldHwOn = true;
  } else if (relayOn && btEn) {
    if (powerSmpsSoftstartActive()) shouldHwOn = false;
    else if (FEAT_SMPS_PROTECT_ENABLE && !bypassSmps && (powerFaultSmpsIsCutActive() || powerFaultSmpsIsTripLatched())) shouldHwOn = false;
    else if ((!bypassSmps && globalLowVoltage) || globalSmpsFaultActive) shouldHwOn = false;
    else shouldHwOn = true;
  }

  if (shouldHwOn != btHwOn) {
    powerHwSetBtPins(shouldHwOn);
    btHwOn = shouldHwOn;
    if (shouldHwOn) {
      btPowerOnMs = millis();
      qccProtectArmUntilMs = btPowerOnMs + 2000U;
      qccProtectLastChangeMs = btPowerOnMs;
      qccProtectOk = powerHwReadBtReady();
      qccProtectActiveLogged = false;
    } else {
      btPowerOnMs = 0;
      qccProtectArmUntilMs = 0;
    }
  }
}

static void fanTick(uint32_t now) {
  if (!fanBootTestDone) {
      if (now >= fanBootTestUntilMs) {
          fanBootTestDone = true;
      } else {
          return;
      }
  }

  if (powerIsOn() && stateSelfTestGetStatus() == SelfTestStatus::RUNNING) {
      uint16_t selftestOverride = stateSelfTestGetFanDutyOverride();
      if (selftestOverride <= 1023) {
          powerHwSetFanDuty(selftestOverride);
          return;
      }
  }

  FanMode mode = stateGetFanMode();
  uint16_t duty = 0;
  float tc = getHeatsinkC();

  if (powerIsOn() && !powerIsInitializing()) {
    if (!isnan(tc) && tc >= (stateOtpThresh() - 10.0f)) {
        duty = 1023;
        static uint32_t lastThermalWarnMs = 0;
        if (millis() - lastThermalWarnMs >= 10000) {
            commsLog("warn", "thermal_throttling_high_temp");
            lastThermalWarnMs = millis();
        }
    } else if (mode == FanMode::CUSTOM) {
      duty = stateGetFanCustomDuty();
    } else if (mode == FanMode::FAILSAFE) {
      duty = 1023;
    } else {
      if (isnan(tc) || tc <= 0.0f) {
        bool selftestAllowed = (stateSelfTestGetStatus() != SelfTestStatus::RUNNING);
        if (selftestAllowed) {
            static uint32_t lastNanWarnMs = 0;
            static uint32_t nanPersistStartMs = 0;

            if (nanPersistStartMs == 0) nanPersistStartMs = now;

            if (now - nanPersistStartMs >= 5000) {
                if (now - lastNanWarnMs >= 10000) {
                  lastNanWarnMs = now;
                  commsLog("warn", "temp_sensor_nan_forcing_fan_full");
                }
                duty = 1023;
            }
        } else {
            duty = 0;
        }
      } else {
        static float integral = 0.0f;
        static float prevError = 0.0f;
        static uint32_t lastPidMs = 0;
        static uint16_t lastDuty = FAN_AUTO_MIN;

        if (lastPidMs == 0) lastPidMs = now;
        if (now - lastPidMs >= 1000) {
            float dt = (now - lastPidMs) / 1000.0f;
            lastPidMs = now;

            float setpoint = statePidSetpoint();
            float error = tc - setpoint;

            integral += error * dt;
            if (integral > 1000.0f) integral = 1000.0f;
            else if (integral < -1000.0f) integral = -1000.0f;

            float derivative = (error - prevError) / dt;
            prevError = error;

            float output = (statePidKp() * error) + (statePidKi() * integral) + (statePidKd() * derivative);

            // In runtime: always maintain quiet baseline rotation (FAN_AUTO_MIN) when cool
            float dutyFloat = FAN_AUTO_MIN + output;

            if (dutyFloat > 1023.0f) {
                lastDuty = 1023;
            } else if (dutyFloat < FAN_AUTO_MIN) {
                lastDuty = FAN_AUTO_MIN;
            } else {
                lastDuty = static_cast<uint16_t>(dutyFloat);
            }
        }
        duty = lastDuty;
      }
    }
  } else {
    // STANDBY / POWER OFF mode:
    // Fan is completely OFF when cool, but spins to cool down heatsink if hot.
    static bool standbyCoolingActive = false;
    if (!isnan(tc)) {
        if (!standbyCoolingActive && tc >= 50.0f) {
            standbyCoolingActive = true;
        } else if (standbyCoolingActive && tc < 42.0f) {
            standbyCoolingActive = false;
        }
    } else {
        standbyCoolingActive = false;
    }

    if (standbyCoolingActive) {
        // Heatsink is hot in standby: spin fan to cool down with smooth curve
        float dutyFloat = FAN_AUTO_MIN + (tc - 42.0f) * 35.0f;
        if (dutyFloat > 1023.0f) dutyFloat = 1023.0f;
        else if (dutyFloat < FAN_AUTO_MIN) dutyFloat = FAN_AUTO_MIN;
        duty = static_cast<uint16_t>(dutyFloat);
    } else {
        // Cooled down or cold boot in standby: Fan OFF (duty 0)
        duty = 0;
    }
  }

  powerHwSetFanDuty(duty);
}

void powerSetRuntimeBypass(bool active) {
  runtimeBypassActive = active;
  if (active) {
    commsLog("warn", "temporary_runtime_bypass_activated");
  }
}

bool powerIsRuntimeBypassActive(void) {
  return runtimeBypassActive;
}

bool powerIsInitializing() {
  return (stateSelfTestGetStatus() == SelfTestStatus::RUNNING);
}

uint32_t powerGetBtPowerOnMs() {
  return btPowerOnMs;
}

bool powerBtHardCycling() {
  return btHardCycling;
}

bool powerBtFailedPerm() {
  return btFailedPerm;
}

void powerClearBtFailedPerm() {
  btFailedPerm = false;
}

void powerSetBtFailedPerm(bool failed) {
  btFailedPerm = failed;
}

void powerBypassBtInitializing() {
  btInitializing = false;
  btFailedPerm = false;
}

void powerNotifyBtConnected() {
  btInitializing = false;
}

void powerCompleteSelfTest() {
  relayOn = true;
  relayRequested = true;
  audioRelayOn = true;
  spkPwr = stateSpeakerPowerOn();
  btEn = stateBtEnabled();

  smpsSoftstartUntilMs = 0;
  smpsValidSince = millis();
  spkProtectArmUntilMs = millis() + SPK_PROTECT_ARM_MS;
  qccProtectArmUntilMs = millis() + 2000U;
  qccProtectLastChangeMs = millis();
  qccProtectOk = powerHwReadBtReady();

  btDetectionReset(millis());

  applyRelay(true);
  applyBtHardware(millis());
  applySpeakerPower();
}

void powerApplyBtHardware() {
  applyBtHardware(millis());
}

void powerHardCycleBt() {
  powerHwSetBtPins(false);
  btHwOn = false;
  btHardCycleStartMs = millis();
  btHardCycling = true;
  btPowerOnMs = 0;
  qccProtectArmUntilMs = 0;
  commsLog("info", "Starting QCC non-blocking hard power cycle (BT pins pulled low)...");
}

void powerInit() {
  powerHwInit();
  powerFaultInit();

  relayRequested = false;
  relayOn = false;
  audioRelayOn = false;
  powerHwSetRelaySmps(false);
  powerHwSetRelayAudio(false);

  spkPwr = stateSpeakerPowerOn();
  spkHwOn = false;
  powerHwSetSpeakerPins(false);

  btEn = stateBtEnabled();
  btHwOn = false;
  powerHwSetBtPins(false);

  uint32_t now = millis();
  btLastAuxMs = now;
  btLowSinceMs = 0;
  btDetectionReset(now);
  smpsHwFaultLatched = false;
  spkProtectOk = powerHwReadSpkProtect();
  protectLastChangeMs = now;
  protectFaultLatched = false;
  protectFaultLogged = false;
  qccProtectOk = powerHwReadBtReady();
  qccProtectLastChangeMs = now;
  qccProtectFaultLatched = false;
  qccProtectActiveLogged = false;
  qccProtectArmUntilMs = 0;
  btInitializing = false;
  btPowerOnMs = 0;

  if (FEAT_FAN_BOOT_TEST) {
    fanBootTestDone = false;
    fanBootTestUntilMs = now + FAN_BOOT_TEST_MS;
    powerHwSetFanDuty(FAN_BOOT_TEST_DUTY);
  } else {
    fanBootTestDone = true;
    powerHwSetFanDuty(0);
  }

  applyBtHardware(now);
  if (safeModeActive) {
    powerHwSetFanDuty(0);
    powerHwSetSpeakerPins(false);
    spkPwr = false;
    commsLog("warn", "safe_mode");
  }
  spkProtectArmUntilMs = now + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
}

void powerTick(const uint32_t now) {
  float currentTemp = getHeatsinkC();

  if (btHardCycling) {
      if (now - btHardCycleStartMs >= 1000) {
          powerHwSetBtPins(true);
          btHwOn = true;
          btPowerOnMs = now;
          btHardCycling = false;
          qccProtectArmUntilMs = now + 2000U;
          qccProtectLastChangeMs = now;
          qccProtectOk = powerHwReadBtReady();
          qccProtectActiveLogged = false;
          commsLog("info", "QCC non-blocking hard power cycle complete (BT pins pulled high).");
      }
  }

  static float lastTemp = 0.0f;
  static uint32_t lastTempCheckMs = 0;
  if (!isnan(currentTemp) && powerIsOn() && spkHwOn) {
      if (lastTempCheckMs == 0) {
          lastTemp = currentTemp;
          lastTempCheckMs = now;
      } else if (now - lastTempCheckMs >= 1000) {
          float riseRate = currentTemp - lastTemp;
          if (riseRate >= 2.0f) {
              powerHwSetSpeakerPins(false);
              spkHwOn = false;
              commsLog("error", "emergency_mute_abnormal_temp_rise");
              buzzPattern(BuzzPatternId::PROTECT_LONG);
          }
          lastTemp = currentTemp;
          lastTempCheckMs = now;
      }
  } else {
      lastTempCheckMs = 0;
  }

  if (powerIsOn() && spkHwOn && analyzerIsClipping()) {
      static uint32_t lastClipLogMs = 0;
      if (now - lastClipLogMs >= 5000) {
          lastClipLogMs = now;
          commsLog("warn", "audio_signal_clipping_detected");
      }
  }

  if (!isnan(currentTemp) && currentTemp > stateGetPeakTemp()) {
      stateSetPeakTemp(currentTemp);
  }

  static uint32_t lastMaintCheckMins = 0;
  uint32_t currentTotalMins = stateGetTotalMinutes();
  if (lastMaintCheckMins == 0) lastMaintCheckMins = currentTotalMins;
  if (currentTotalMins >= lastMaintCheckMins + 60000) {
      commsLog("info", "maintenance_due_check_heatsink_dust");
      lastMaintCheckMins = currentTotalMins;
  }

  static uint32_t lastMinuteUpdateMs = 0;
  static uint32_t lastHourlyNvsSaveMs = 0;
  if (powerIsOn()) {
      if (lastMinuteUpdateMs == 0) {
          lastMinuteUpdateMs = now;
          lastHourlyNvsSaveMs = now;
      }

      if (now - lastMinuteUpdateMs >= 60000) {
          stateSetTotalMinutes(stateGetTotalMinutes() + 1);
          lastMinuteUpdateMs = now;
      }

      if (now - lastHourlyNvsSaveMs >= 3600000) {
          stateSaveRuntimeStats();
          lastHourlyNvsSaveMs = now;
          commsLog("info", "hourly_stats_backup_saved");
      }
  } else {
      lastMinuteUpdateMs = 0;
      lastHourlyNvsSaveMs = 0;

      // Standby fail-safe: actively enforce all relays and outputs are strictly OFF in standby
      if (relayOn || relayRequested) {
          relayOn = false;
          relayRequested = false;
          powerHwSetRelaySmps(false);
          gpio_set_level((gpio_num_t)RELAY_SMPS_PIN, RELAY_SMPS_ACTIVE_HIGH ? 0 : 1);
      }
      if (audioRelayOn) {
          audioRelayOn = false;
          powerHwSetRelayAudio(false);
          gpio_set_level((gpio_num_t)RELAY_AUDIO_PIN, RELAY_AUDIO_ACTIVE_HIGH ? 0 : 1);
      }
      if (spkHwOn) {
          spkHwOn = false;
          powerHwSetSpeakerPins(false);
          gpio_set_level((gpio_num_t)SPEAKER_POWER_SWITCH_PIN, 0);
      }
      if (btHwOn) {
          btHwOn = false;
          powerHwSetBtPins(false);
          gpio_set_level((gpio_num_t)BT_ENABLE_PIN, 0);
      }
  }

  if (powerIsOn() && !powerIsInitializing() && !isnan(currentTemp) && currentTemp >= stateOtpThresh() && !otpLatched) {
      otpLatched = true;
      stateSetLastFault("over_temperature");
      if (runtimeBypassActive) {
          commsLog("error", "over_temperature_bypassed_system_running");
      } else {
          powerSetMainRelay(false, PowerChangeReason::Command);
      }
  }

  if (sleepTargetEpoch > 0 && powerIsOn()) {
      uint32_t currentEpoch = 0;
      if (sensorsGetUnixTime(currentEpoch) && currentEpoch >= sleepTargetEpoch) {
          sleepTargetEpoch = 0;
          powerSetMainRelay(false, PowerChangeReason::Command);
      }
  }

  float vSmps = getVoltageInstant();
  float v_stby = getVoltageStandby();

  if (powerIsOn() && !powerIsInitializing() && !powerSmpsSoftstartActive()) {
      if (vSmps > stateSmpsOvThresh() || v_stby > stateStbyOvThresh()) {
          stateSetLastFault(vSmps > stateSmpsOvThresh() ? "ov_smps" : "ov_stby");
          if (runtimeBypassActive) {
              commsLog("error", "over_voltage_bypassed_system_running");
          } else {
              powerSetMainRelay(false, PowerChangeReason::Command);
          }
          buzzStop();
          buzzPattern(BuzzPatternId::ERROR);
      }

      const bool bypassSmps = stateSmpsBypass() || runtimeBypassActive;
      bool isLv = (!bypassSmps && vSmps > 0.0f && vSmps < stateSmpsCutoffV()) || (v_stby > 0.0f && v_stby < stateStbyLowThresh());
      if (isLv != globalLowVoltage) {
          globalLowVoltage = isLv;
          if (globalLowVoltage) {
              buzzPattern(BuzzPatternId::WARNING_LOW_VOLT);
          }
      }
  } else {
      globalLowVoltage = false;
  }

  static uint32_t smpsFaultStartMs = 0;
  if (powerIsOn() && !powerIsInitializing()) {
      if (powerHwReadSmpsFault()) {
          if (!globalSmpsFaultActive) {
              globalSmpsFaultActive = true;
              smpsFaultStartMs = now;
              buzzStop();
              buzzPattern(BuzzPatternId::ERROR_CONTINUOUS);
          }

          if (!smpsHwFaultLatched && (now - smpsFaultStartMs > 3000)) {
              smpsHwFaultLatched = true;
              stateSetLastFault("smps_hw_fault");
              commsLog("error", "SMPS Fault Optocoupler (GPIO 15) Active - SMPS protection triggered!");
              if (runtimeBypassActive) {
                  commsLog("error", "smps_fault_bypassed_system_running");
              } else {
                  powerSetSpeakerPower(false);
                  powerSetMainRelay(false, PowerChangeReason::SmpsFaultDetect);
              }
          }
      } else {
          if (globalSmpsFaultActive) {
              globalSmpsFaultActive = false;
              buzzStop();
          }
      }
  } else {
      globalSmpsFaultActive = false;
  }

  static uint32_t lastAudioAboveThreshMs = now;
  if (powerIsOn() && stateAutoSenseEnabled()) {
      uint8_t currentVu = analyzerGetVu();
      if (currentVu >= stateAutoSenseThresh()) {
          lastAudioAboveThreshMs = now;
      } else {
          uint32_t timeoutMs = stateAutoSenseTimeout() * 60000UL;
          if (now - lastAudioAboveThreshMs >= timeoutMs) {
              powerSetMainRelay(false, PowerChangeReason::Command);
              lastAudioAboveThreshMs = now;
          }
      }
  } else {
      lastAudioAboveThreshMs = now;
  }

  fanTick(now);
  if ((safeModeActive || globalSmpsFaultActive) && fanBootTestDone) {
    powerHwSetFanDuty(0);
  }

  if (!powerIsOn()) smpsValidSince = 0;

  powerFaultTick(now, relayOn, powerSmpsSoftstartActive(), getVoltageInstant(), stateSmpsCutoffV(), stateSmpsRecoveryV());

  if (!protectFaultLatched && !qccProtectFaultLatched && !powerSmpsHwFaultLatched() && !powerSmpsTripLatched() && !isnan(getHeatsinkC())) {
      alarmsSilenced = false;
  }

  if (relayOn && !powerSmpsSoftstartActive()) {
    const bool smpsBypass = stateSmpsBypass();
    if (smpsBypass) {
        if (smpsValidSince == 0) smpsValidSince = now - 3500;
    } else {
        if (!powerFaultSmpsIsCutActive() && !powerFaultSmpsIsTripLatched()) {
          if (smpsValidSince == 0) smpsValidSince = now;
        } else {
          smpsValidSince = 0;
        }

        if (powerFaultSmpsIsTripLatched() && (stateSelfTestGetStatus() != SelfTestStatus::RUNNING)) {
            if (!smpsHwFaultLatched) {
                smpsHwFaultLatched = true;
                buzzStop();
                buzzPattern(BuzzPatternId::SMPS_ERROR);
                if (runtimeBypassActive) {
                    commsLog("error", "smps_trip_bypassed_system_running");
                } else {
                    powerSetMainRelay(false, PowerChangeReason::SmpsFaultDetect);
                }
            }
        }
    }
  } else {
    smpsValidSince = 0;
  }

#if FEAT_SPK_PROTECT_ENABLE
  const bool spkIsHwOn = spkHwOn;
  const bool monitorAllowed = spkIsHwOn && (spkProtectArmUntilMs > 0) && (now >= spkProtectArmUntilMs);

  if (monitorAllowed) {
    bool ok = powerHwReadSpkProtect();
    if (ok && !protectFaultLogged) {
      protectFaultLogged = true;
      commsLog("info", "speaker_protector_active_gpio34_ok");
    }
    if (ok != spkProtectOk) {
      spkProtectOk = ok;
      protectLastChangeMs = now;
    } else {
      if (!spkProtectOk && !protectFaultLatched) {
        if (now - protectLastChangeMs >= SPK_PROTECT_FAULT_MS) {
            protectFaultLatched = true;
            stateSetLastFault("speaker_protector_trip");
            commsLog("error", "speaker_protector_fault_gpio34_low");
            if (!alarmsSilenced) {
                buzzStop();
                buzzPattern(BuzzPatternId::PROTECT_LONG);
            }
        }
      }
      if (spkProtectOk && protectFaultLatched) {
          protectFaultLatched = false;
          commsLog("info", "speaker_protector_recovered_ok");
          buzzStop();
      }
    }
  } else {
    spkProtectOk = true;
    protectFaultLogged = false;
  }
#else
  protectFaultLatched = false;
  spkProtectOk = true;
#endif

  // Runtime QCC Power Detect Monitoring (GPIO 5 / QCC_PWR_DETECT_PIN)
  const bool qccIsHwOn = relayOn && btEn && btHwOn && !btHardCycling && !powerIsInitializing();
  if (qccIsHwOn) {
    bool qccReady = powerHwReadBtReady();
    if (!qccProtectActiveLogged && qccReady) {
      qccProtectActiveLogged = true;
      commsLog("info", "qcc_power_detect_active_gpio5_high");
    }
    const bool qccMonitorAllowed = (qccProtectArmUntilMs > 0) && (now >= qccProtectArmUntilMs);
    if (qccMonitorAllowed) {
      if (qccReady != qccProtectOk) {
        qccProtectOk = qccReady;
        qccProtectLastChangeMs = now;
      } else {
        if (!qccProtectOk && !qccProtectFaultLatched) {
          if (now - qccProtectLastChangeMs >= 800U) {
            qccProtectFaultLatched = true;
            stateSetLastFault("qcc_power_detect_trip");
            commsLog("error", "qcc_power_detect_fault_gpio5_low");
            if (!alarmsSilenced) {
              buzzStop();
              buzzPattern(BuzzPatternId::PROTECT_LONG);
            }
          }
        }
        if (qccProtectOk && qccProtectFaultLatched) {
          qccProtectFaultLatched = false;
          commsLog("info", "qcc_power_detect_recovered_ok");
          buzzStop();
          btFailedPerm = false;
          gaiaResetConnectionState();
        }
      }
    } else {
      qccProtectOk = qccReady;
      qccProtectLastChangeMs = now;
    }
  } else {
    qccProtectOk = true;
  }

  if (stateSelfTestGetStatus() == SelfTestStatus::RUNNING) {
    btDetectionReset(now);
  } else if (FEAT_BT_AUTOSWITCH_AUX && btHwOn) {
    const bool rawCurrent = powerHwReadBtStatus();

    // 1. Noise Debounce: require raw level to remain steady for >= 30ms
    if (rawCurrent != s_lastBtRaw) {
      s_lastBtRaw = rawCurrent;
      s_btDebounceMs = now;
    }
    if ((now - s_btDebounceMs >= 30) && (s_lastBtRaw != s_debouncedBt)) {
      bool prevDebounced = s_debouncedBt;
      s_debouncedBt = s_lastBtRaw;

      // Track edge transitions and duration of previous state
      if (s_debouncedBt && !prevDebounced) {
        // Rising edge: was LOW, now HIGH
        s_lastLowDurationMs = (s_btLowSinceMs > 0) ? (now - s_btLowSinceMs) : 0;
        s_btLowSinceMs = 0;
        s_btHighSinceMs = now;
      } else if (!s_debouncedBt && prevDebounced) {
        // Falling edge: was HIGH, now LOW
        s_lastHighDurationMs = (s_btHighSinceMs > 0) ? (now - s_btHighSinceMs) : 0;
        s_btHighSinceMs = 0;
        s_btLowSinceMs = now;

        // In BT mode: If it just dropped LOW after a blinking-period pulse (200ms - 2000ms),
        // and previous low duration was also a blinking pulse (200ms - 2000ms), this confirms active BLINKING!
        if (btMode) {
          if (s_lastHighDurationMs >= 200 && s_lastHighDurationMs <= 2000 &&
              s_lastLowDurationMs >= 200 && s_lastLowDurationMs <= 2000) {
            btMode = false;
            commsLog("info", "input_mode_changed_to_aux");
          }
        }
      }
    }

    // 2. Track continuous high and low durations
    if (s_debouncedBt) {
      if (s_btHighSinceMs == 0) s_btHighSinceMs = now;
      s_btLowSinceMs = 0;
    } else {
      if (s_btLowSinceMs == 0) s_btLowSinceMs = now;
      s_btHighSinceMs = 0;
    }

    if (!btMode) {
      // In AUX mode: pin blinks ~1s HIGH / ~1s LOW.
      // Transition to BT only if pin is continuously solid HIGH for >= 3000ms.
      if (s_debouncedBt && (s_btHighSinceMs > 0) && (now - s_btHighSinceMs >= 3000)) {
        btMode = true;
        commsLog("info", "input_mode_changed_to_bt");
      }
    } else {
      // In BT mode: pin is solid HIGH with occasional single heartbeat dip (~800-1000ms).
      // Transition to AUX if pin is continuously LOW for >= 2500ms (disconnected or module powered down).
      if (!s_debouncedBt && (s_btLowSinceMs > 0) && (now - s_btLowSinceMs >= 2500)) {
        btMode = false;
        commsLog("info", "input_mode_changed_to_aux");
      }
    }
  }

  applyBtHardware(now);
  applySpeakerPower();
}

void powerSetMainRelay(bool on, PowerChangeReason reason) {
  const bool prevOn = relayOn || powerIsOn();
  relayRequested = on;
  if (safeModeActive) on = false;

  if (!on && prevOn) {
    btLastAuxMs = 0;
    btLowSinceMs = 0;
    btLossSinceMs = 0;
    btDetectionReset(millis());
  }

  if (!on) {
    relayOn = false;
    relayRequested = false;
    audioRelayOn = false;
    spkHwOn = false;
    btHwOn = false;
    btMode = false;
    smpsValidSince = 0;
    sleepTargetEpoch = 0;
    smpsSoftstartUntilMs = 0;
    spkProtectArmUntilMs = 0;
    qccProtectArmUntilMs = 0;

    stateSelfTestReset(); // Fully reset all setup state, override, and timers on power off
    powerHwSetFanDuty(0); // Immediately cut fan duty override on standby transition
    runtimeBypassActive = false; // Reset temporary bypass on power off
    if (reason == PowerChangeReason::Command || reason == PowerChangeReason::Button || reason == PowerChangeReason::FactoryReset) {
       powerFaultSmpsReset();
       globalSmpsFaultActive = false;
       globalLowVoltage = false;
       smpsHwFaultLatched = false;
       alarmsSilenced = false;
    }

    protectFaultLatched = false;
    spkProtectOk = true;
    qccProtectFaultLatched = false;
    qccProtectOk = true;
    qccProtectActiveLogged = false;

    // 1. Cut speaker protection first to protect speakers from pop
    powerHwSetSpeakerPins(false);
    gpio_set_level((gpio_num_t)SPEAKER_POWER_SWITCH_PIN, 0);

    // 2. Cut audio input relays
    powerHwSetRelayAudio(false);
    gpio_set_level((gpio_num_t)RELAY_AUDIO_PIN, RELAY_AUDIO_ACTIVE_HIGH ? 0 : 1);

    // 3. Cut SMPS main power relay unconditionally
    powerHwSetRelaySmps(false);
    gpio_set_level((gpio_num_t)RELAY_SMPS_PIN, RELAY_SMPS_ACTIVE_HIGH ? 0 : 1);

    // 4. Cut BT power
    btInitializing = false;
    btPowerOnMs = 0;
    btFailedPerm = false;
    powerHwSetBtPins(false);
    gpio_set_level((gpio_num_t)BT_ENABLE_PIN, 0);

    gaiaDisconnect();
  } else {
    btFailedPerm = false;
    btInitializing = false;
    btPowerOnMs = 0;
    btHwOn = false;
    qccProtectFaultLatched = false;
    qccProtectActiveLogged = false;
    powerHwSetBtPins(false);
    gpio_set_level((gpio_num_t)BT_ENABLE_PIN, 0);
  }

  const bool nowOn = relayOn;

  powerSetOn(on);
  notifyPowerChange(prevOn, nowOn, reason);

  if (on && !prevOn) {
    btDetectionReset(millis());
    powerSmpsStartSoftstart(SMPS_SOFTSTART_MS);
    powerFaultSmpsReset();
    smpsValidSince = 0;
    otpLatched = false;
    smpsHwFaultLatched = false;
    spkProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + SPK_PROTECT_ARM_MS;
    qccProtectArmUntilMs = millis() + SMPS_SOFTSTART_MS + 2000U;

    stateSelfTestStart();
  }

  applyBtHardware(millis());
}

void powerSetAudioRelay(bool on) {
  audioRelayOn = on;
  powerHwSetRelayAudio(on);
}

bool powerGetAudioRelay() {
  return audioRelayOn;
}

bool powerMainRelay() { return relayOn; }
void powerRegisterStateListener(PowerStateListener listener) { powerListener = listener; }
PowerState powerCurrentState() { return relayOn ? PowerState::On : PowerState::Standby; }

void powerSetSpeakerPower(bool on) {
  spkPwr = on;
  stateSetSpeakerPowerOn(on);
  if (!on) {
    if (protectFaultLatched) {
      buzzStop();
    }
    protectFaultLatched = false;
    protectFaultLogged = false;
    spkProtectOk = true;
  } else {
    protectFaultLatched = false;
    protectFaultLogged = false;
    alarmsSilenced = false;
  }
  applySpeakerPower();
  if (on && spkHwOn && powerIsOn() && !powerIsInitializing()) {
    commsLog("info", "speaker_protector_sensor_armed");
  }
}

bool powerGetSpeakerPower() { return spkPwr; }

void powerSetAlarmsSilenced(bool silenced) { alarmsSilenced = silenced; }
bool powerAlarmsSilenced() { return alarmsSilenced; }

void powerSetBtEnabled(bool en, bool persist) {
  btEn = en;
  if (persist) {
    stateSetBtEnabled(en);
  }
  if (powerIsOn() && !powerIsInitializing()) {
    tm1637ShowText(en ? "bt" : "aux");
  }
  uint32_t now = millis();

  if (en) {
    btLastAuxMs = 0;
    btLowSinceMs = 0;
    qccProtectFaultLatched = false;
    qccProtectActiveLogged = false;
    alarmsSilenced = false;
    if (powerIsOn()) {
      btInitializing = true;
      btPowerOnMs = now;
      btFailedPerm = false;
      gaiaResetConnectionState();
    }
  } else {
    if (qccProtectFaultLatched) {
      buzzStop();
    }
    qccProtectFaultLatched = false;
    qccProtectOk = true;
    qccProtectActiveLogged = false;
    qccProtectArmUntilMs = 0;
    btMode = false;
    btLowSinceMs = 0;
    btLastAuxMs = now;
    btInitializing = false;
    btPowerOnMs = 0;
    btFailedPerm = false;
    gaiaDisconnect();
  }

  applyBtHardware(now);

  if (en && btHwOn) {
    btMode = false;
    btLowSinceMs = 0;
    btLastAuxMs = now;
    qccProtectArmUntilMs = now + 2000U;
    qccProtectLastChangeMs = now;
    qccProtectOk = powerHwReadBtReady();
    if (powerIsOn() && !powerIsInitializing()) {
      commsLog("info", "qcc_power_detect_sensor_armed");
    }
  } else if (!en && powerIsOn() && !powerIsInitializing()) {
    commsLog("info", "qcc_power_disabled_sensor_disarmed");
  }
}

bool powerBtEnabled() { return btEn; }
bool powerBtMode() { return btMode; }

void powerSetOtaActive(bool on) { (void)on; }

bool powerSpkProtectFault() {
#if !FEAT_SPK_PROTECT_ENABLE
  return false;
#endif
  if (!relayOn) return false;
  if (!spkPwr) return false;
  if (powerSmpsSoftstartActive()) return false;
#if FEAT_SMPS_PROTECT_ENABLE
  const bool bypassSmps = stateSmpsBypass() || runtimeBypassActive;
  if (!bypassSmps && (powerFaultSmpsIsCutActive() || powerFaultSmpsIsTripLatched())) return false;
#endif
  if (millis() < spkProtectArmUntilMs) return false;
  return protectFaultLatched;
}

bool powerQccProtectFault() {
  if (!relayOn) return false;
  if (!btEn || !btHwOn) return false;
  if (btHardCycling) return false;
  if (powerSmpsSoftstartActive()) return false;
  if (powerIsInitializing()) return false;
#if FEAT_SMPS_PROTECT_ENABLE
  const bool bypassSmps = stateSmpsBypass() || runtimeBypassActive;
  if (!bypassSmps && (powerFaultSmpsIsCutActive() || powerFaultSmpsIsTripLatched())) return false;
#endif
  if (millis() < qccProtectArmUntilMs) return false;
  return qccProtectFaultLatched;
}

const char* powerInputModeStr() { return btMode ? "bt" : "aux"; }
bool powerSmpsTripLatched() { return powerFaultSmpsIsTripLatched(); }
bool powerSmpsHwFaultLatched() { return smpsHwFaultLatched; }

void powerSmpsStartSoftstart(uint32_t msDelay) { smpsSoftstartUntilMs = millis() + msDelay; }
bool powerSmpsSoftstartActive() { return millis() < smpsSoftstartUntilMs; }
bool powerSmpsIsValid() {
  if (!relayOn) return false;
  if (powerSmpsSoftstartActive()) return false;
  if (smpsValidSince == 0) return false;
  return (millis() - smpsValidSince) >= 3000;
}

bool powerOtpFault() {
    return otpLatched;
}

void powerSetSleepTimer(uint32_t minutes) {
    if (minutes == 0) {
        sleepTargetEpoch = 0;
    } else {
        uint32_t currentEpoch = 0;
        if (sensorsGetUnixTime(currentEpoch)) {
            sleepTargetEpoch = currentEpoch + (minutes * 60);
        }
    }
}

uint32_t powerGetSleepRemainingMinutes() {
    if (sleepTargetEpoch == 0) return 0;
    uint32_t currentEpoch = 0;
    if (sensorsGetUnixTime(currentEpoch)) {
        if (sleepTargetEpoch > currentEpoch) {
            return (sleepTargetEpoch - currentEpoch) / 60;
        }
    }
    return 0;
}
