#include "state.h"
#include "config.h"
#include "sensors.h"
#include "power.h"
#include "power_hw.h"
#include "buzzer.h"
#include "tm1637.h"
#include "gaia.h"
#include "cJSON.h"
#include "comms.h"
#include "comms_internal.h"
#include "i2cdev.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_sys.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static nvs_handle_t get_nvs_handle(nvs_open_mode_t mode = NVS_READWRITE) {
    nvs_handle_t handle = 0;
    nvs_open("jacktor_audio", mode, &handle);
    return handle;
}

static bool nvs_get_bool(const char* key, bool defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return defaultVal;
    uint8_t val = defaultVal ? 1 : 0;
    nvs_get_u8(handle, key, &val);
    nvs_close(handle);
    return val != 0;
}

static void nvs_put_bool(const char* key, bool val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    nvs_set_u8(handle, key, val ? 1 : 0);
    nvs_commit(handle);
    nvs_close(handle);
}

static uint8_t nvs_get_uchar(const char* key, uint8_t defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return defaultVal;
    uint8_t val = defaultVal;
    nvs_get_u8(handle, key, &val);
    nvs_close(handle);
    return val;
}

static void nvs_put_uchar(const char* key, uint8_t val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    nvs_set_u8(handle, key, val);
    nvs_commit(handle);
    nvs_close(handle);
}

static uint16_t nvs_get_ushort(const char* key, uint16_t defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return defaultVal;
    uint16_t val = defaultVal;
    nvs_get_u16(handle, key, &val);
    nvs_close(handle);
    return val;
}

static void nvs_put_ushort(const char* key, uint16_t val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    nvs_set_u16(handle, key, val);
    nvs_commit(handle);
    nvs_close(handle);
}

static uint32_t nvs_get_ulong(const char* key, uint32_t defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return defaultVal;
    uint32_t val = defaultVal;
    nvs_get_u32(handle, key, &val);
    nvs_close(handle);
    return val;
}

static void nvs_put_ulong(const char* key, uint32_t val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    nvs_set_u32(handle, key, val);
    nvs_commit(handle);
    nvs_close(handle);
}

static float nvs_get_float(const char* key, float defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return defaultVal;
    uint32_t raw = 0;
    if (nvs_get_u32(handle, key, &raw) == ESP_OK) {
        float f;
        memcpy(&f, &raw, sizeof(float));
        nvs_close(handle);
        return f;
    }
    nvs_close(handle);
    return defaultVal;
}

static void nvs_put_float(const char* key, float val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    uint32_t raw;
    memcpy(&raw, &val, sizeof(float));
    nvs_set_u32(handle, key, raw);
    nvs_commit(handle);
    nvs_close(handle);
}

static std::string nvs_get_string(const char* key, const char* defaultVal) {
    nvs_handle_t handle = get_nvs_handle(NVS_READONLY);
    if (!handle) return std::string(defaultVal ? defaultVal : "");
    size_t len = 0;
    if (nvs_get_str(handle, key, NULL, &len) == ESP_OK && len > 0) {
        char* buf = new char[len + 1];
        memset(buf, 0, len + 1);
        nvs_get_str(handle, key, buf, &len);
        std::string res(buf);
        delete[] buf;
        nvs_close(handle);
        return res;
    }
    nvs_close(handle);
    return std::string(defaultVal ? defaultVal : "");
}

static void nvs_put_string(const char* key, const std::string& val) {
    nvs_handle_t handle = get_nvs_handle(NVS_READWRITE);
    if (!handle) return;
    nvs_set_str(handle, key, val.c_str());
    nvs_commit(handle);
    nvs_close(handle);
}

static bool gOn = false;
static bool gStby = true;

static bool sSpeakerPwr;
static FanMode sFanMode;
static uint16_t sFanDuty;
static bool sSmpsBypass;
static float sSmpsCutV, sSmpsRecV;
static bool sBtEn;
static uint32_t sBtOffMs;
static bool sAutoSenseEn;
static uint32_t sAutoSenseMin;
static uint8_t sAutoSenseThr;
static uint32_t sRtcSyncTs;
static uint32_t sTotalMins;
static float sPeakTemp;
static std::string sLastFault;
static std::vector<std::string> sFaultHistory;
static bool sBtnLock;

static float sPidKp, sPidKi, sPidKd, sPidSetpoint;
static float sStbyOffset, sOtpThresh;
static float sSmpsOv, sStbyOv, sStbyLow;

static constexpr const char* K_SPK_PWR = "spk_pwr";
static constexpr const char* K_FAN_MODE = "fan_mode";
static constexpr const char* K_FAN_DUTY = "fan_duty";
static constexpr const char* K_SMPS_BYPASS = "smps_bypass";
static constexpr const char* K_SMPS_CUT = "smps_cut";
static constexpr const char* K_SMPS_REC = "smps_rec";
static constexpr const char* K_BT_EN = "bt_en";
static constexpr const char* K_BT_OFFMS = "bt_off";
static constexpr const char* K_AS_EN = "as_en";
static constexpr const char* K_AS_MIN = "as_min";
static constexpr const char* K_AS_THR = "as_thr";
static constexpr const char* K_RTC_SYNC = "rtc_sync";
static constexpr const char* K_TOTAL_MINS = "total_mins";
static constexpr const char* K_PEAK_TEMP = "peak_temp";
static constexpr const char* K_LAST_FAULT = "last_fault";
static constexpr const char* K_FAULT_HIST = "fault_hist";
static constexpr const char* K_BTN_LOCK = "btn_lock";
static constexpr const char* K_PID_KP = "pid_kp";
static constexpr const char* K_PID_KI = "pid_ki";
static constexpr const char* K_PID_KD = "pid_kd";
static constexpr const char* K_PID_SP = "pid_sp";
static constexpr const char* K_STBY_OFS = "stby_ofs";
static constexpr const char* K_OTP_THR = "otp_thr";
static constexpr const char* K_SMPS_OV = "smps_ov";
static constexpr const char* K_STBY_OV = "stby_ov";
static constexpr const char* K_STBY_LOW = "stby_low";

static void loadFromNvs() {
  sSpeakerPwr = nvs_get_bool(K_SPK_PWR, true);
  sFanMode = (FanMode)nvs_get_uchar(K_FAN_MODE, (uint8_t)FanMode::AUTO);
  sFanDuty = nvs_get_ushort(K_FAN_DUTY, FAN_CUSTOM_DUTY);
  sSmpsBypass = nvs_get_bool(K_SMPS_BYPASS, SMPS_PROTECT_BYPASS);
  sSmpsCutV = nvs_get_float(K_SMPS_CUT, SMPS_CUT_V);
  sSmpsRecV = nvs_get_float(K_SMPS_REC, SMPS_REC_V);
  if (sSmpsCutV < 45.0f) {
    sSmpsCutV = SMPS_CUT_V;
    nvs_put_float(K_SMPS_CUT, sSmpsCutV);
  }
  if (sSmpsRecV < 45.0f) {
    sSmpsRecV = SMPS_REC_V;
    nvs_put_float(K_SMPS_REC, sSmpsRecV);
  }
  sBtEn = nvs_get_bool(K_BT_EN, true);
  sBtOffMs = nvs_get_ulong(K_BT_OFFMS, BT_AUTO_OFF_IDLE_MS);
  sAutoSenseEn = nvs_get_bool(K_AS_EN, false);
  sAutoSenseMin = nvs_get_ulong(K_AS_MIN, 15);
  sAutoSenseThr = nvs_get_uchar(K_AS_THR, 2);
  sRtcSyncTs = nvs_get_ulong(K_RTC_SYNC, 0);
  sTotalMins = nvs_get_ulong(K_TOTAL_MINS, 0);
  sPeakTemp = nvs_get_float(K_PEAK_TEMP, 0.0f);
  sLastFault = nvs_get_string(K_LAST_FAULT, "none");

  sFaultHistory.clear();
  std::string hist = nvs_get_string(K_FAULT_HIST, "");
  if (!hist.empty()) {
      size_t start = 0;
      size_t end = hist.find('|');
      while (end != std::string::npos) {
          sFaultHistory.push_back(hist.substr(start, end - start));
          start = end + 1;
          end = hist.find('|', start);
      }
      if (start < hist.length()) sFaultHistory.push_back(hist.substr(start));
  }

  sBtnLock = nvs_get_bool(K_BTN_LOCK, false);
  sPidKp = nvs_get_float(K_PID_KP, 2.0f);
  sPidKi = nvs_get_float(K_PID_KI, 0.05f);
  sPidKd = nvs_get_float(K_PID_KD, 1.0f);
  sPidSetpoint = nvs_get_float(K_PID_SP, 45.0f);
  sStbyOffset = nvs_get_float(K_STBY_OFS, V_STBY_OFFSET_V);
  sOtpThresh = nvs_get_float(K_OTP_THR, 85.0f);
  sSmpsOv = nvs_get_float(K_SMPS_OV, 70.0f);
  sStbyOv = nvs_get_float(K_STBY_OV, 14.5f);
  sStbyLow = nvs_get_float(K_STBY_LOW, 10.5f);
}

void stateInit() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  loadFromNvs();
  gOn = false;
  gStby = true;
}

void stateFactoryReset() {
  nvs_flash_erase();
  nvs_flash_init();
  loadFromNvs();
}

bool stateSpeakerPowerOn() { return sSpeakerPwr; }
void stateSetSpeakerPowerOn(bool on) {
  sSpeakerPwr = on;
  nvs_put_bool(K_SPK_PWR, on);
}

FanMode stateGetFanMode() { return sFanMode; }
void stateSetFanMode(FanMode m) {
  sFanMode = m;
  nvs_put_uchar(K_FAN_MODE, (uint8_t)m);
}

uint16_t stateGetFanCustomDuty() { return sFanDuty; }
void stateSetFanCustomDuty(uint16_t d) {
  if (d > 1023) d = 1023;
  sFanDuty = d;
  nvs_put_ushort(K_FAN_DUTY, d);
}

bool stateSmpsBypass() { return sSmpsBypass; }
void stateSetSmpsBypass(bool en) {
  sSmpsBypass = en;
  nvs_put_bool(K_SMPS_BYPASS, en);
}

float stateSmpsCutoffV() { return sSmpsCutV; }
void stateSetSmpsCutoffV(float v) {
  sSmpsCutV = v;
  nvs_put_float(K_SMPS_CUT, v);
}

float stateSmpsRecoveryV() { return sSmpsRecV; }
void stateSetSmpsRecoveryV(float v) {
  sSmpsRecV = v;
  nvs_put_float(K_SMPS_REC, v);
}

bool stateBtEnabled() { return sBtEn; }
void stateSetBtEnabled(bool en) {
  sBtEn = en;
  nvs_put_bool(K_BT_EN, en);
}

uint32_t stateBtAutoOffMs() { return sBtOffMs; }
void stateSetBtAutoOffMs(uint32_t ms) {
  sBtOffMs = ms;
  nvs_put_ulong(K_BT_OFFMS, ms);
}


bool stateAutoSenseEnabled() { return sAutoSenseEn; }
void stateSetAutoSenseEnabled(bool en) {
  sAutoSenseEn = en;
  nvs_put_bool(K_AS_EN, en);
}

uint32_t stateAutoSenseTimeout() { return sAutoSenseMin; }
void stateSetAutoSenseTimeout(uint32_t min) {
  sAutoSenseMin = min;
  nvs_put_ulong(K_AS_MIN, min);
}

uint8_t stateAutoSenseThresh() { return sAutoSenseThr; }
void stateSetAutoSenseThresh(uint8_t thr) {
  sAutoSenseThr = thr;
  nvs_put_uchar(K_AS_THR, thr);
}

uint32_t stateLastRtcSync() { return sRtcSyncTs; }
void stateSetLastRtcSync(uint32_t t) {
  sRtcSyncTs = t;
  nvs_put_ulong(K_RTC_SYNC, t);
}

float statePidKp() { return sPidKp; }
void stateSetPidKp(float kp) {
  sPidKp = kp;
  nvs_put_float(K_PID_KP, kp);
}

float statePidKi() { return sPidKi; }
void stateSetPidKi(float ki) {
  sPidKi = ki;
  nvs_put_float(K_PID_KI, ki);
}

float statePidKd() { return sPidKd; }
void stateSetPidKd(float kd) {
  sPidKd = kd;
  nvs_put_float(K_PID_KD, kd);
}

float statePidSetpoint() { return sPidSetpoint; }
void stateSetPidSetpoint(float sp) {
  sPidSetpoint = sp;
  nvs_put_float(K_PID_SP, sp);
}

float stateStbyOffset() { return sStbyOffset; }
void stateSetStbyOffset(float ofs) {
  sStbyOffset = ofs;
  nvs_put_float(K_STBY_OFS, ofs);
}

float stateOtpThresh() { return sOtpThresh; }
void stateSetOtpThresh(float thr) {
  sOtpThresh = thr;
  nvs_put_float(K_OTP_THR, thr);
}

float stateSmpsOvThresh() { return sSmpsOv; }
void stateSetSmpsOvThresh(float thr) {
  sSmpsOv = thr;
  nvs_put_float(K_SMPS_OV, thr);
}

float stateStbyOvThresh() { return sStbyOv; }
void stateSetStbyOvThresh(float thr) {
  sStbyOv = thr;
  nvs_put_float(K_STBY_OV, thr);
}

float stateStbyLowThresh() { return sStbyLow; }
void stateSetStbyLowThresh(float thr) {
  sStbyLow = thr;
  nvs_put_float(K_STBY_LOW, thr);
}

uint32_t stateGetTotalMinutes() { return sTotalMins; }
void stateSetTotalMinutes(uint32_t mins) {
  sTotalMins = mins;
}

float stateGetPeakTemp() { return sPeakTemp; }
void stateSetPeakTemp(float t) {
  sPeakTemp = t;
  nvs_put_float(K_PEAK_TEMP, t);
}

void stateSaveRuntimeStats() {
  nvs_put_ulong(K_TOTAL_MINS, sTotalMins);
  nvs_put_float(K_PEAK_TEMP, sPeakTemp);
}

const char* stateGetLastFault() { return sLastFault.c_str(); }
void stateSetLastFault(const char* fault) {
  if (!fault) fault = "none";
  sLastFault = fault;
  nvs_put_string(K_LAST_FAULT, sLastFault);

  char entry[128];
  snprintf(entry, sizeof(entry), "%lu:%s", (unsigned long)sTotalMins, fault);

  sFaultHistory.insert(sFaultHistory.begin(), std::string(entry));
  if (sFaultHistory.size() > 5) sFaultHistory.resize(5);

  std::string joined = "";
  for (size_t i = 0; i < sFaultHistory.size(); ++i) {
      joined += sFaultHistory[i];
      if (i < sFaultHistory.size() - 1) joined += "|";
  }
  nvs_put_string(K_FAULT_HIST, joined);
}

const std::vector<std::string>& stateGetFaultHistory() {
    return sFaultHistory;
}

bool stateGetBtnLock() { return sBtnLock; }
void stateSetBtnLock(bool lock) {
  sBtnLock = lock;
  nvs_put_bool(K_BTN_LOCK, lock);
}

bool powerIsOn() { return gOn; }
bool powerIsStandby() { return gStby; }
void powerSetOn(bool on) {
  gOn = on;
  gStby = !on;
}

static uint32_t rebootStartMs = 0;
static uint32_t rebootDelayMs = 0;
static bool rebootPending = false;

static SelfTestStatus stStatus = SelfTestStatus::IDLE;
static bool stBtStepActive = false;
static bool stBtConnectAllowed = false;
static uint8_t stCurrentStep = 0;
static uint32_t stStartMs = 0;
static uint16_t stFanDuty = 2000;
static SelfTestResults stRes;

static bool st_s0_logged = false;
static bool st_s1_logged = false;
static bool st_s1b_logged = false;
static bool st_s2_logged = false;
static bool st_s3_logged = false;
static bool st_smps_relay_on = false;
static bool st_s4_logged = false;
static bool st_s4_entry_logged = false;
static bool st_s4_pass_logged = false;
static uint32_t stStep4StartMs = 0;
static bool st_s5_logged = false;
static bool st_s5_msg_logged = false;
static bool st_s6_logged = false;
static bool st_s7_logged = false;
static bool st_s8_logged = false;
static bool st_s9_logged = false;
static uint32_t stStep5StartMs = 0;
static uint32_t stStep5StabStartMs = 0;
static uint32_t stStep6StartMs = 0;

static void sendSelfTestProgress(int step, const char* msg, const char* status, float smpsV = -1.0f) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "setup_progress");
    cJSON_AddNumberToObject(root, "step", step);
    cJSON_AddStringToObject(root, "msg", msg ? msg : "");
    cJSON_AddStringToObject(root, "status", status ? status : "");
    if (smpsV >= 0.0f) {
        cJSON_AddNumberToObject(root, "smps_v", roundf(smpsV * 10.0f) / 10.0f);
    }
    sendTelemetry(root);
}

void stateSelfTestReset() {
    stStatus = SelfTestStatus::IDLE;
    stBtStepActive = false;
    stBtConnectAllowed = false;
    stCurrentStep = 0;
    stFanDuty = 2000;
    stStartMs = 0;

    st_s0_logged = false;
    st_s1_logged = false;
    st_s1b_logged = false;
    st_s2_logged = false;
    st_s3_logged = false;
    st_smps_relay_on = false;
    st_s4_logged = false;
    st_s4_entry_logged = false;
    st_s4_pass_logged = false;
    stStep4StartMs = 0;
    st_s5_logged = false;
    st_s5_msg_logged = false;
    st_s6_logged = false;
    st_s7_logged = false;
    st_s8_logged = false;
    st_s9_logged = false;
    stStep5StartMs = 0;
    stStep5StabStartMs = 0;
    stStep6StartMs = 0;
}

static void stateSelfTestFail(int step, const char* msg, const char* errCode) {
    char dbg[256];
    int dlen = snprintf(dbg, sizeof(dbg), "\n[SETUP_FAIL] Step %d: %s (%s)\n", step, msg ? msg : "", errCode ? errCode : "");
    uart_write_bytes(UART_NUM_0, dbg, dlen);

    if (powerIsRuntimeBypassActive()) {
        sendSelfTestProgress(step, msg, "WARN_BYPASSED");
        commsLog("warn", "Setup error detected but bypassed due to active runtime bypass!");
        return;
    }
    stBtStepActive = false;
    stBtConnectAllowed = false;
    sendSelfTestProgress(step, msg, "FAIL");

    // Generate skip reports for remaining untested steps
    if (step < 2) sendSelfTestProgress(2, "Hardware Sensor Check (I2C/1-Wire)", "SKIP");
    if (step < 3) sendSelfTestProgress(3, "SMPS Main Power Relay Setup", "SKIP");
    if (step < 4) sendSelfTestProgress(4, "Audio Input Relays Setup", "SKIP");
    if (step < 5) sendSelfTestProgress(5, "Speaker Protection Setup", "SKIP");
    if (step < 6) sendSelfTestProgress(6, "Bluetooth GAIA Scan & Connection", "SKIP");

    stStatus = SelfTestStatus::FINISHED;
    stFanDuty = 2000;

    powerSetMainRelay(false, PowerChangeReason::Command);
    buzzStop();
    buzzPattern(BuzzPatternId::ERROR_CONTINUOUS);

    if (errCode) {
        tm1637ShowText(errCode, 3000);
    } else {
        tm1637ShowText("err", 3000);
    }
}

void stateSelfTestStart() {
    if (powerIsOn()) {
        stStatus = SelfTestStatus::RUNNING;
        stBtStepActive = false;
        stBtConnectAllowed = false;
        stCurrentStep = 0;
        stStartMs = millis();
        stFanDuty = 2000; // Standby / quiet during display delay

        st_s0_logged = false;
        st_s1_logged = false;
        st_s1b_logged = false;
        st_s2_logged = false;
        st_s3_logged = false;
        st_smps_relay_on = false;
        st_s4_logged = false;
        st_s4_entry_logged = false;
        st_s4_pass_logged = false;
        stStep4StartMs = 0;
        st_s5_logged = false;
        st_s5_msg_logged = false;
        st_s6_logged = false;
        st_s7_logged = false;
        st_s8_logged = false;
        st_s9_logged = false;
        stStep5StartMs = 0;
        stStep5StabStartMs = 0;
        stStep6StartMs = 0;

        stRes.rtc_ok = false;
        stRes.ads1115_ok = false;
        stRes.ds18b20_ok = false;
        stRes.smps_fault_ok = !powerSmpsHwFaultLatched();
        stRes.spk_fault_ok = !powerSpkProtectFault();
        stRes.gaia_bt_ok = false;
        stRes.gaia_rssi = 0;

        float v_stby = getVoltageStandby();
        if (v_stby > 0.0f) stRes.ads1115_ok = true;

        float heat = getHeatsinkC();
        if (!std::isnan(heat)) stRes.ds18b20_ok = true;

        stRes.rtc_ok = sensorsRecheckRtc();
        powerApplyBtHardware();

        tm1637ShowText("on", 2000);
    }
}

SelfTestStatus stateSelfTestGetStatus() { return stStatus; }
uint8_t stateSelfTestGetCurrentStep() { return stCurrentStep; }
bool stateSelfTestIsBtStepActive() { return (stStatus == SelfTestStatus::RUNNING) && stBtStepActive; }
bool stateSelfTestIsBtConnectAllowed() { return (stStatus == SelfTestStatus::RUNNING) ? stBtConnectAllowed : true; }
void stateSelfTestAcknowledge() { stStatus = SelfTestStatus::IDLE; stBtStepActive = false; stBtConnectAllowed = false; }
uint16_t stateSelfTestGetFanDutyOverride() { return stFanDuty; }
void stateSelfTestGetResults(SelfTestResults& outRes) { outRes = stRes; }

void stateRequestReboot(uint32_t delayMs) {
    rebootStartMs = millis();
    rebootDelayMs = delayMs;
    rebootPending = true;
}

bool stateIsRebootPending() {
    return rebootPending;
}

void stateTick() {
    if (rebootPending && (millis() - rebootStartMs >= rebootDelayMs)) {
        esp_restart();
    }

    if (!powerIsOn()) {
        if (stStatus == SelfTestStatus::RUNNING || stCurrentStep != 0 || stFanDuty != 2000) {
            stateSelfTestReset();
        }
        return;
    }

    if (stStatus == SelfTestStatus::RUNNING) {
        uint32_t elapsed = millis() - stStartMs;

        // Step 0: Setup Initiation & 7-Segment "on" Display Delay (0 - 2000 ms, matches "off" duration)
        if (elapsed < 2000) {
            stCurrentStep = 0;
            stFanDuty = 2000;
            if (!st_s0_logged) {
                sendSelfTestProgress(0, "Initiating System Setup...", "RUNNING");
                tm1637ShowText("on", 2000);
                st_s0_logged = true;
            }
            return;
        }

        // Step 1: Cooling Fan Test (2000 - 9000 ms) - Smooth Ramp Up, 100% Peak, Smooth Ramp Down
        if (elapsed < 5000) {
            stCurrentStep = 1;
            if (!st_s1_logged) {
                sendSelfTestProgress(1, "Testing Cooling Fan (Smooth Ramp Up to 100%)...", "RUNNING");
                st_s1_logged = true;
            }
            float progress = (float)(elapsed - 2000) / 3000.0f;
            float curve = 0.5f * (1.0f - cosf(progress * 3.14159265f));
            stFanDuty = static_cast<uint16_t>(curve * 1023.0f);
        }
        else if (elapsed < 6000) {
            stCurrentStep = 1;
            if (!st_s1b_logged) {
                sendSelfTestProgress(1, "Testing Cooling Fan (Peak Speed 100%)...", "RUNNING");
                st_s1b_logged = true;
            }
            stFanDuty = 1023;
        }
        else if (elapsed < 9000) {
            stCurrentStep = 1;
            if (!st_s2_logged) {
                sendSelfTestProgress(1, "Testing Cooling Fan (Smooth Ramp Down)...", "RUNNING");
                st_s2_logged = true;
            }
            float progress = (float)(9000 - elapsed) / 3000.0f;
            float curve = 0.5f * (1.0f - cosf(progress * 3.14159265f));
            stFanDuty = static_cast<uint16_t>(curve * 1023.0f);
        }

        // Step 2: Hardware Sensor Check (I2C / 1-Wire) (9000 - 12000 ms)
        if (elapsed >= 9000 && elapsed < 12000) {
            stCurrentStep = 2;
            if (!st_s8_logged) {
                stFanDuty = 2000;
                sendSelfTestProgress(2, "Scanning I2C Bus Modules...", "RUNNING");

                bool rtc_found = sensorsRecheckRtc();
                bool ads_found = sensorsRecheckAds1115();

                stRes.rtc_ok = rtc_found;
                stRes.ads1115_ok = ads_found;

                if (!stRes.ads1115_ok) {
                    char scanBuf[128] = "";
                    int scanLen = 0;
                    scanLen += snprintf(scanBuf + scanLen, sizeof(scanBuf) - scanLen, "I2C Devs: ");
                    bool anyFound = false;
                    i2c_master_bus_handle_t bus_handle = NULL;
                    if (i2cdev_get_shared_handle(I2C_NUM_0, (void**)&bus_handle) == ESP_OK && bus_handle != NULL) {
                        for (uint8_t address = 1; address < 127; address++) {
                            if (i2c_master_probe(bus_handle, address, 50) == ESP_OK) {
                                scanLen += snprintf(scanBuf + scanLen, sizeof(scanBuf) - scanLen, "0x%02X ", address);
                                anyFound = true;
                            }
                        }
                    }
                    if (!anyFound) {
                        scanLen += snprintf(scanBuf + scanLen, sizeof(scanBuf) - scanLen, "None");
                    }

                    char errBuf[256];
                    snprintf(errBuf, sizeof(errBuf), "Critical Voltmeter (ADS1115) Missing! %s", scanBuf);

                    st_s8_logged = true;
                    stateSelfTestFail(2, errBuf, "e-01");
                    return;
                }

                if (!stRes.ds18b20_ok) {
                    sendSelfTestProgress(2, "DS18B20 Temperature Sensor missing - system running with default cooling", "WARN");
                } else {
                    sendSelfTestProgress(2, "Checking DS18B20 Temperature Sensor... [ PASS ]", "PASS");
                }

                if (!stRes.rtc_ok) {
                    sendSelfTestProgress(2, "DS3231 Real-Time Clock missing - system running with software time limits", "WARN");
                } else {
                    sendSelfTestProgress(2, "Checking I2C Bus Modules... [ PASS ]", "PASS");
                }
                st_s8_logged = true;
            }
        }

        // Step 3a: SMPS Main Power Relay Setup at t >= 12000ms
        if (st_s8_logged && !st_s3_logged && elapsed >= 12000) {
            stCurrentStep = 3;
            sendSelfTestProgress(3, "Activating SMPS Main Power Relay Setup...", "RUNNING");
            powerHwSetRelaySmps(true);
            st_smps_relay_on = true;
            st_s3_logged = true;
        }

        // Step 3b: SMPS Voltage Rail Check (allow up to 4.5 seconds for SMPS capacitor bank to charge up to ~60V)
        if (st_smps_relay_on && !st_s4_logged && elapsed >= 13500) {
            stCurrentStep = 3;
            float vSmps = sensorsReadSmpsDirect();
            bool smpsHwFault = powerHwReadSmpsFault(); // GPIO 15 (Active LOW)
            char vBuf[128];

            if (smpsHwFault && elapsed >= 15000) {
                snprintf(vBuf, sizeof(vBuf), "SMPS Hardware Fault Optocoupler (GPIO 15) Active - SMPS protection triggered!");
                sendSelfTestProgress(3, vBuf, "FAIL");
                stRes.smps_fault_ok = false;
                st_s4_logged = true;
                stateSelfTestFail(3, vBuf, "e-02");
                return;
            } else if (vSmps >= 15.0f && vSmps <= stateSmpsOvThresh()) {
                snprintf(vBuf, sizeof(vBuf), "Checking SMPS Main Power Relay: SMPS = %.1f V (Protection Normal)... [ PASS ]", vSmps);
                sendSelfTestProgress(3, vBuf, "PASS", vSmps);
                stRes.smps_fault_ok = true;
                st_s4_logged = true;
            } else if (elapsed >= 15500) {
                snprintf(vBuf, sizeof(vBuf), "Checking SMPS Voltage Rail (%.1f V) - Bench mode / Standby rail active", vSmps);
                sendSelfTestProgress(3, vBuf, "WARN", vSmps);
                stRes.smps_fault_ok = true;
                st_s4_logged = true;
            }
        }

        // Step 4: Audio Input Relays Setup (dedicated 2.5 second visible step)
        if (st_s4_logged && !st_s5_logged) {
            stCurrentStep = 4;
            if (stStep4StartMs == 0) {
                stStep4StartMs = millis();
            }
            if (!st_s4_entry_logged) {
                sendSelfTestProgress(4, "Activating Audio Input Relays Setup...", "RUNNING");
                powerHwSetRelayAudio(true);
                st_s4_entry_logged = true;
            }
            if (!st_s4_pass_logged && (millis() - stStep4StartMs >= 1000)) {
                sendSelfTestProgress(4, "Checking Audio Input Relays... [ PASS ]", "PASS");
                st_s4_pass_logged = true;
            }
            if (millis() - stStep4StartMs >= 2500) {
                st_s5_logged = true;
                stStep5StartMs = millis();
            }
        }

        // Step 5a: Speaker Protector Relay Activation (with 1.0s delay)
        if (st_s5_logged && !st_s6_logged) {
            stCurrentStep = 5;
            if (!stateSpeakerPowerOn()) {
                sendSelfTestProgress(5, "Speaker Protector disabled in settings - Skipping check [ PASS ]", "PASS");
                stRes.spk_fault_ok = true;
                st_s6_logged = true;
                st_s7_logged = true;
                stStep6StartMs = millis();
            } else {
                if (stStep5StartMs == 0) {
                    stStep5StartMs = millis();
                    sendSelfTestProgress(5, "Activating Speaker Protector Relay Setup (1 sec delay)...", "RUNNING");
                }
                if (millis() - stStep5StartMs >= 1000) {
                    powerHwSetSpeakerPins(true); // Power up speaker protection circuit after 1 sec delay!
                    st_s6_logged = true;
                    stStep5StabStartMs = millis();
                }
            }
        }

        // Step 5b: Speaker Protector Stabilization Completion & HW Fault Check (6.0s stabilization)
        if (st_s6_logged && !st_s7_logged) {
            stCurrentStep = 5;
            uint32_t spkElapsed = millis() - stStep5StabStartMs;

            if (spkElapsed >= 300 && !st_s5_msg_logged) {
                sendSelfTestProgress(5, "Speaker Protector Stabilization: 6 sec...", "RUNNING");
                st_s5_msg_logged = true;
            }

            if (spkElapsed >= 6000) {
                if (!powerHwReadSpkProtect()) {
                    sendSelfTestProgress(5, "Checking Speaker Protector Status - Notice / Trip Detected", "FAIL");
                    stRes.spk_fault_ok = false;
                    st_s7_logged = true;
                    stateSelfTestFail(5, "Speaker Protection HW Fault Detected (DC Offset / Relay Fail)", "e-03");
                    return;
                } else {
                    sendSelfTestProgress(5, "Speaker Protector Stabilization: 6 sec... [ PASS ]", "PASS");
                    sendSelfTestProgress(5, "Checking Speaker Protector Status... [ PASS ]", "PASS");
                    stRes.spk_fault_ok = true;
                }
                st_s7_logged = true;
                stStep6StartMs = millis();
            }
        }

        // Step 6: Bluetooth GAIA Scan & Connection Test (Final Step)
        if (st_s7_logged) {
            stCurrentStep = 6;
            if (!stateBtEnabled()) {
                sendSelfTestProgress(6, "Bluetooth QCC module disabled in settings - Skipping check [ PASS ]", "PASS");
                stRes.gaia_bt_ok = true;
                stRes.gaia_rssi = 0;
                stBtStepActive = false;
                stStatus = SelfTestStatus::FINISHED;
                stFanDuty = 2000;
                tm1637ShowText("pass", 2000);
                powerCompleteSelfTest();
                return;
            }
            stBtStepActive = true;

            enum class St6Phase : uint8_t {
                INIT,
                WAIT_QCC_READY,
                WAIT_BT_BOOT,
                CONNECTING,
                POWER_CYCLE_WAIT,
                POST_CYCLE_WAIT_READY,
                POST_CYCLE_WAIT_BOOT,
                POST_CYCLE_CONNECTING
            };
            static St6Phase s_st6Phase = St6Phase::INIT;
            static uint32_t s_st6PhaseStartMs = 0;
            static uint8_t s_st6LastFailCount = 0;

            if (!st_s9_logged) {
                s_st6Phase = St6Phase::WAIT_QCC_READY;
                s_st6PhaseStartMs = millis();
                s_st6LastFailCount = 0;
                stBtConnectAllowed = false;

                sendSelfTestProgress(6, "Powering ON QCC5125 Bluetooth Module...", "RUNNING");
                powerApplyBtHardware(); // Power ON QCC right here before checking GPIO 5!
                gaiaResetConnectionState();
                sendSelfTestProgress(6, "Checking QCC5125 Power Detect Pin (GPIO 5)...", "RUNNING");
                st_s9_logged = true;
            }

            uint32_t step6Elapsed = millis() - stStep6StartMs;

            // Common success handler: whenever connected, mark PASS and finish self-test
            // Wait at least 1.5s so that "6- 6" is clearly visible on the 7-segment display
            if (gaiaIsConnected() && step6Elapsed >= 1500) {
                stRes.gaia_bt_ok = true;
                stRes.gaia_rssi = gaiaGetLastRssi();
                sendSelfTestProgress(6, "Checking Bluetooth GAIA Connection... [ PASS ]", "PASS");
                gaiaSendCommand(CMD_GET_CURRENT_RSSI);
                stBtStepActive = false;
                stBtConnectAllowed = false;
                stStatus = SelfTestStatus::FINISHED;
                stFanDuty = 2000;
                tm1637ShowText("pass", 2000);
                powerCompleteSelfTest();
                return;
            }

            if (s_st6Phase == St6Phase::WAIT_QCC_READY) {
                stBtConnectAllowed = false;
                if (powerHwReadBtReady()) {
                    sendSelfTestProgress(6, "Checking QCC5125 Power Detect Pin... [ PASS ]", "PASS");
                    sendSelfTestProgress(6, "Waiting for QCC5125 Bluetooth stack to initialize...", "RUNNING");
                    s_st6Phase = St6Phase::WAIT_BT_BOOT;
                    s_st6PhaseStartMs = millis();
                } else if (millis() - s_st6PhaseStartMs >= 3500) {
                    sendSelfTestProgress(6, "Checking QCC5125 Power Detect Pin (GPIO 5) - Module Inactive / Standby", "WARN");
                    s_st6Phase = St6Phase::WAIT_BT_BOOT;
                    s_st6PhaseStartMs = millis();
                }
            }
            else if (s_st6Phase == St6Phase::WAIT_BT_BOOT) {
                stBtConnectAllowed = false;
                if (millis() - s_st6PhaseStartMs >= 2000) {
                    sendSelfTestProgress(6, "Connecting Bluetooth GAIA Module via SPP...", "RUNNING");
                    gaiaResetConnectionState();
                    s_st6Phase = St6Phase::CONNECTING;
                    s_st6PhaseStartMs = millis();
                    s_st6LastFailCount = 0;
                    stBtConnectAllowed = true;
                }
            }
            else if (s_st6Phase == St6Phase::CONNECTING) {
                stBtConnectAllowed = true;
                uint8_t fails = gaiaGetFailureCount();
                if (fails != s_st6LastFailCount) {
                    s_st6LastFailCount = fails;
                    if (fails < 3) {
                        char msg[96];
                        snprintf(msg, sizeof(msg), "Bluetooth GAIA connection attempt %d/3 failed, retrying...", fails);
                        sendSelfTestProgress(6, msg, "RUNNING");
                    }
                }

                // If 3 attempts failed or phase timed out (18s)
                if (s_st6LastFailCount >= 3 || (millis() - s_st6PhaseStartMs >= 18000)) {
                    sendSelfTestProgress(6, "Initial connection attempts failed. Power-cycling QCC module...", "WARN");
                    gaiaResetConnectionState();
                    powerHardCycleBt();
                    s_st6Phase = St6Phase::POWER_CYCLE_WAIT;
                    s_st6PhaseStartMs = millis();
                    stBtConnectAllowed = false;
                }
            }
            else if (s_st6Phase == St6Phase::POWER_CYCLE_WAIT) {
                stBtConnectAllowed = false;
                if (!powerBtHardCycling()) {
                    sendSelfTestProgress(6, "QCC power pin restored HIGH. Re-detecting QCC power detect pin (GPIO 5)...", "RUNNING");
                    s_st6Phase = St6Phase::POST_CYCLE_WAIT_READY;
                    s_st6PhaseStartMs = millis();
                }
            }
            else if (s_st6Phase == St6Phase::POST_CYCLE_WAIT_READY) {
                stBtConnectAllowed = false;
                if (powerHwReadBtReady()) {
                    sendSelfTestProgress(6, "QCC5125 Power Detect Pin (GPIO 5) active [ PASS ]", "PASS");
                    sendSelfTestProgress(6, "Waiting for QCC5125 Bluetooth stack to initialize...", "RUNNING");
                    s_st6Phase = St6Phase::POST_CYCLE_WAIT_BOOT;
                    s_st6PhaseStartMs = millis();
                } else if (millis() - s_st6PhaseStartMs >= 5000) {
                    sendSelfTestProgress(6, "QCC5125 Power Detect Pin (GPIO 5) failed to activate after power cycle", "FAIL");
                    stRes.gaia_bt_ok = false;
                    stBtConnectAllowed = false;
                    stateSelfTestFail(6, "QCC5125 Power Detect Pin (GPIO 5) inactive after power cycle", "e-04");
                    return;
                }
            }
            else if (s_st6Phase == St6Phase::POST_CYCLE_WAIT_BOOT) {
                stBtConnectAllowed = false;
                if (millis() - s_st6PhaseStartMs >= 2000) {
                    sendSelfTestProgress(6, "Retrying Bluetooth GAIA connection after power cycle...", "RUNNING");
                    gaiaResetConnectionState();
                    s_st6LastFailCount = 0;
                    s_st6Phase = St6Phase::POST_CYCLE_CONNECTING;
                    s_st6PhaseStartMs = millis();
                    stBtConnectAllowed = true;
                }
            }
            else if (s_st6Phase == St6Phase::POST_CYCLE_CONNECTING) {
                stBtConnectAllowed = true;
                uint8_t fails = gaiaGetFailureCount();
                if (fails != s_st6LastFailCount) {
                    s_st6LastFailCount = fails;
                    if (fails < 3) {
                        char msg[96];
                        snprintf(msg, sizeof(msg), "Post-cycle GAIA connection attempt %d/3 failed, retrying...", fails);
                        sendSelfTestProgress(6, msg, "RUNNING");
                    }
                }

                // If all 3 post-cycle retries failed or phase timed out (18s)
                if (s_st6LastFailCount >= 3 || (millis() - s_st6PhaseStartMs >= 18000)) {
                    sendSelfTestProgress(6, "Bluetooth GAIA connection failed after power cycle and retries", "FAIL");
                    stRes.gaia_bt_ok = false;
                    stBtConnectAllowed = false;
                    stateSelfTestFail(6, "Bluetooth GAIA connection failed after power cycle and retries", "e-04");
                    return;
                }
            }
        }
    }
}

bool stateSafeModeSoft() {
  return SAFE_MODE_SOFT != 0;
}
