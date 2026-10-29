#include "comms_telemetry.h"
#include "comms_internal.h"
#include "config.h"
#include "state.h"
#include "power.h"
#include "power_hw.h"
#include "sensors.h"
#include "analyzer.h"
#include "buzzer.h"
#include <string>
#include <cmath>
#include <vector>

static std::string encodeBase64(const uint8_t* data, size_t len) {
    static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t octet_a = i < len ? data[i] : 0;
        uint32_t octet_b = i + 1 < len ? data[i + 1] : 0;
        uint32_t octet_c = i + 2 < len ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        out.push_back(b64_table[(triple >> 3 * 6) & 0x3F]);
        out.push_back(b64_table[(triple >> 2 * 6) & 0x3F]);
        out.push_back(i + 1 < len ? b64_table[(triple >> 1 * 6) & 0x3F] : '=');
        out.push_back(i + 2 < len ? b64_table[(triple >> 0 * 6) & 0x3F] : '=');
    }
    return out;
}

static void writeTimeISO(cJSON *obj) {
  char buf[24];
  if (!sensorsGetTimeISO(buf, sizeof(buf)) || strlen(buf) < 20) {
    snprintf(buf, sizeof(buf), "1970-01-01T00:00:00Z");
  }
  cJSON_AddStringToObject(obj, "time", buf);
}

static void setFloatOrNull(cJSON *obj, const char *key, float value) {
  if (std::isnan(value) || std::isinf(value)) cJSON_AddNullToObject(obj, key);
  else cJSON_AddNumberToObject(obj, key, value);
}

static void writeNvsSnapshot(cJSON *root) {
  cJSON *nv = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "nvs", nv);

  FanMode mode = stateGetFanMode();
  cJSON_AddNumberToObject(nv, "fan_mode", static_cast<uint8_t>(mode));
  cJSON_AddStringToObject(nv, "fan_mode_str", fanModeToStr(mode));
  cJSON_AddNumberToObject(nv, "fan_duty", (mode == FanMode::AUTO) ? powerHwGetFanDuty() : stateGetFanCustomDuty());
  cJSON_AddBoolToObject(nv, "audio_relay", powerGetAudioRelay());
  cJSON_AddBoolToObject(nv, "spk_pwr", stateSpeakerPowerOn());
  cJSON_AddBoolToObject(nv, "bt_en", stateBtEnabled());
  cJSON_AddBoolToObject(nv, "btn_lock", stateGetBtnLock());
  cJSON_AddBoolToObject(nv, "smps_bypass", stateSmpsBypass());
  cJSON_AddNumberToObject(nv, "smps_cut", stateSmpsCutoffV());
  cJSON_AddNumberToObject(nv, "smps_rec", stateSmpsRecoveryV());
}

static void writeFeatures(cJSON *root) {
  cJSON *feats = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "features", feats);
  cJSON_AddBoolToObject(feats, "bt_autoswitch", static_cast<bool>(FEAT_BT_AUTOSWITCH_AUX));
  cJSON_AddBoolToObject(feats, "fan_boot_test", static_cast<bool>(FEAT_FAN_BOOT_TEST));
  cJSON_AddBoolToObject(feats, "factory_reset_combo", static_cast<bool>(FEAT_FACTORY_RESET_COMBO));
  cJSON_AddBoolToObject(feats, "rtc_temp", static_cast<bool>(FEAT_RTC_TEMP_TELEMETRY));
  cJSON_AddBoolToObject(feats, "smps_protect", static_cast<bool>(FEAT_SMPS_PROTECT_ENABLE));
  cJSON_AddBoolToObject(feats, "ds18b20_softfilter", static_cast<bool>(FEAT_FILTER_DS18B20_SOFT));
  cJSON_AddBoolToObject(feats, "safe_mode", static_cast<bool>(SAFE_MODE_SOFT));
}

static void writeErrors(cJSON *arr) {
  float v = getVoltageInstant();
  float v_stby = getVoltageStandby();

  if (!stateSmpsBypass()) {
    if (v == 0.0f && powerIsOn() && !powerIsInitializing()) cJSON_AddItemToArray(arr, cJSON_CreateString("NO_POWER"));
    else if (v > stateSmpsOvThresh()) cJSON_AddItemToArray(arr, cJSON_CreateString("SMPS_OVERVOLTAGE"));
  }
  if (v_stby > stateStbyOvThresh()) cJSON_AddItemToArray(arr, cJSON_CreateString("STBY_OVERVOLTAGE"));

  if (std::isnan(getHeatsinkC())) cJSON_AddItemToArray(arr, cJSON_CreateString("SENSOR_FAIL"));
  if (powerSpkProtectFault()) cJSON_AddItemToArray(arr, cJSON_CreateString("SPEAKER_PROTECT_FAIL"));
  if (powerQccProtectFault()) cJSON_AddItemToArray(arr, cJSON_CreateString("QCC_POWER_FAIL"));
  if (powerOtpFault()) cJSON_AddItemToArray(arr, cJSON_CreateString("OVER_TEMP_FAULT"));
  if (powerSmpsHwFaultLatched()) cJSON_AddItemToArray(arr, cJSON_CreateString("SMPS_HW_FAULT"));
  if (stateGetTotalMinutes() >= 60000) cJSON_AddItemToArray(arr, cJSON_CreateString("MAINTENANCE_DUE"));
}

static void writeWarnings(cJSON *arr) {
  float v = getVoltageInstant();
  float v_stby = getVoltageStandby();

  if (!stateSmpsBypass() && v > 0.0f && v < stateSmpsCutoffV()) {
      cJSON_AddItemToArray(arr, cJSON_CreateString("SMPS_LOW_VOLTAGE"));
  }
  if (v_stby > 0.0f && v_stby < stateStbyLowThresh()) {
      cJSON_AddItemToArray(arr, cJSON_CreateString("STBY_LOW_VOLTAGE"));
  }

  float tc = getHeatsinkC();
  if (powerIsOn() && !std::isnan(tc) && tc >= (stateOtpThresh() - 10.0f)) {
      cJSON_AddItemToArray(arr, cJSON_CreateString("THERMAL_THROTTLING"));
  }
}

static void writeAnalyzer(cJSON *data) {
  const char *mode = analyzerGetMode();
  uint8_t bandsLen = analyzerGetBandsLen();
  uint8_t vu = analyzerGetVu();

  cJSON *an = cJSON_CreateObject();
  cJSON_AddItemToObject(data, "analyzer", an);
  cJSON_AddStringToObject(an, "mode", mode ? mode : "off");
  cJSON_AddNumberToObject(an, "bands_len", bandsLen);
  cJSON_AddNumberToObject(an, "update_ms", analyzerGetUpdateMs());
  cJSON_AddNumberToObject(an, "vu", vu);
  cJSON_AddNumberToObject(an, "vu_l", analyzerGetVuL());
  cJSON_AddNumberToObject(an, "vu_r", analyzerGetVuR());
  cJSON_AddNumberToObject(an, "gain_l", analyzerGetGainL());
  cJSON_AddNumberToObject(an, "gain_r", analyzerGetGainR());
  cJSON_AddNumberToObject(an, "noise_l", analyzerGetNoiseL());
  cJSON_AddNumberToObject(an, "noise_r", analyzerGetNoiseR());
  cJSON_AddBoolToObject(an, "clipping", analyzerIsClipping());
  cJSON_AddNumberToObject(an, "clip_count", analyzerGetClippingCount());
}

static void writeBuzzer(cJSON *data) {
  cJSON *bz = cJSON_CreateObject();
  cJSON_AddItemToObject(data, "buzzer", bz);
  cJSON_AddBoolToObject(bz, "enabled", buzzerEnabled());
  cJSON_AddStringToObject(bz, "last_tone", buzzerLastTone() ? buzzerLastTone() : "none");
  cJSON_AddNumberToObject(bz, "last_ms", buzzerLastToneAt());
  cJSON_AddBoolToObject(bz, "quiet_now", buzzerQuietHoursActive());
}

static void writeLinkRealtime(cJSON *link, uint32_t now) {
  const uint32_t rxAge = (lastRxBlink == 0) ? 0 : (now - lastRxBlink);
  const uint32_t txAge = (lastTxBlink == 0) ? 0 : (now - lastTxBlink);
  cJSON_AddBoolToObject(link, "alive", (lastRxBlink != 0) && (rxAge < 3000U));
  if (lastRxBlink == 0) cJSON_AddNullToObject(link, "rx_ms");
  else cJSON_AddNumberToObject(link, "rx_ms", rxAge);
  if (lastTxBlink == 0) cJSON_AddNullToObject(link, "tx_ms");
  else cJSON_AddNumberToObject(link, "tx_ms", txAge);
}

void sendFeaturesSnapshot() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "features");
  writeFeatures(root);
  sendTelemetry(root);
}

void sendRealtimeTelemetry(uint32_t now) {
  if (!TELEM_REALTIME_ENABLE) return;

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "telemetry");

  cJSON *rt = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "rt", rt);

  cJSON_AddBoolToObject(rt, "pwr", powerIsOn());
  cJSON_AddBoolToObject(rt, "spk_pwr", powerGetSpeakerPower());
  cJSON_AddBoolToObject(rt, "spk_fault", powerSpkProtectFault());
  cJSON_AddBoolToObject(rt, "bt_en", powerBtEnabled());
  cJSON_AddBoolToObject(rt, "qcc_pwr", powerHwReadBtReady());
  cJSON_AddBoolToObject(rt, "qcc_fault", powerQccProtectFault());
  cJSON_AddNumberToObject(rt, "v_stby", roundf(getVoltageStandby() * 10.0f) / 10.0f);
  cJSON_AddNumberToObject(rt, "v_smps", roundf(getVoltageInstant() * 10.0f) / 10.0f);
  setFloatOrNull(rt, "heat_c", roundf(getHeatsinkC() * 10.0f) / 10.0f);

  const char *mode = analyzerGetMode();
  uint8_t bandsLen = analyzerGetBandsLen();
  cJSON_AddStringToObject(rt, "mode", mode ? mode : "off");
  cJSON_AddNumberToObject(rt, "bands_len", bandsLen);
  cJSON_AddNumberToObject(rt, "vu", analyzerGetVu());
  cJSON_AddNumberToObject(rt, "vu_l", analyzerGetVuL());
  cJSON_AddNumberToObject(rt, "vu_r", analyzerGetVuR());
  cJSON_AddNumberToObject(rt, "gain_l", analyzerGetGainL());
  cJSON_AddNumberToObject(rt, "gain_r", analyzerGetGainR());
  cJSON_AddNumberToObject(rt, "noise_l", analyzerGetNoiseL());
  cJSON_AddNumberToObject(rt, "noise_r", analyzerGetNoiseR());
  cJSON_AddBoolToObject(rt, "clipping", analyzerIsClipping());
  cJSON_AddNumberToObject(rt, "clip_count", analyzerGetClippingCount());
  cJSON_AddNumberToObject(rt, "update_ms", analyzerGetUpdateMs());
  cJSON_AddNumberToObject(rt, "mag_l", (int)analyzerGetDiagMaxMagL());
  cJSON_AddNumberToObject(rt, "mag_r", (int)analyzerGetDiagMaxMagR());
  cJSON_AddNumberToObject(rt, "bk_l", analyzerGetDiagMaxBucketL());
  cJSON_AddNumberToObject(rt, "bk_r", analyzerGetDiagMaxBucketR());
  cJSON_AddBoolToObject(rt, "gate_l", analyzerGetGateOpenL());
  cJSON_AddBoolToObject(rt, "gate_r", analyzerGetGateOpenR());
  cJSON_AddNumberToObject(rt, "thr_l", (int)analyzerGetDiagThrL());
  cJSON_AddNumberToObject(rt, "thr_r", (int)analyzerGetDiagThrR());
  cJSON_AddNumberToObject(rt, "dc_l", (int)analyzerGetDcOffsetL());
  cJSON_AddNumberToObject(rt, "dc_r", (int)analyzerGetDcOffsetR());
  cJSON_AddNumberToObject(rt, "s_l", analyzerGetSamplesL());
  cJSON_AddNumberToObject(rt, "s_r", analyzerGetSamplesR());
  cJSON_AddNumberToObject(rt, "raw_l", analyzerGetRawL0());
  cJSON_AddNumberToObject(rt, "raw_r", analyzerGetRawR0());
  if (mode && strcmp(mode, "fft") == 0) {
    const uint8_t *bandsL = analyzerGetBandsL();
    const uint8_t *bandsR = analyzerGetBandsR();
    const uint8_t *bands = analyzerGetBands();
    if (bandsL) cJSON_AddStringToObject(rt, "bands_l_b64", encodeBase64(bandsL, bandsLen).c_str());
    if (bandsR) cJSON_AddStringToObject(rt, "bands_r_b64", encodeBase64(bandsR, bandsLen).c_str());
    if (bands) cJSON_AddStringToObject(rt, "bands_b64", encodeBase64(bands, bandsLen).c_str());
  }

  cJSON *link = cJSON_CreateObject();
  cJSON_AddItemToObject(rt, "link", link);
  writeLinkRealtime(link, now);
  cJSON_AddStringToObject(rt, "input", powerInputModeStr());
  cJSON_AddStringToObject(rt, "bt_state", powerBtMode() ? "bt" : "aux");

  sendTelemetry(root);
}

void sendStatsReport() {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "stats_report");

  cJSON *data = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "data", data);
  cJSON_AddNumberToObject(data, "total_minutes", stateGetTotalMinutes());
  cJSON_AddNumberToObject(data, "peak_temp", stateGetPeakTemp());
  cJSON_AddStringToObject(data, "last_fault", stateGetLastFault());

  cJSON *hist = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "fault_history", hist);
  const std::vector<std::string>& faults = stateGetFaultHistory();
  for (const std::string& f : faults) {
      cJSON_AddItemToArray(hist, cJSON_CreateString(f.c_str()));
  }

  cJSON_AddBoolToObject(data, "btn_lock", stateGetBtnLock());

  char timeBuf[24];
  if (sensorsGetTimeISO(timeBuf, sizeof(timeBuf))) {
      cJSON_AddStringToObject(data, "current_time", timeBuf);
  }

  sendTelemetry(root);
}

void sendAnalyzerSnapshot(const char *evt) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "analyzer");
  if (evt && *evt) cJSON_AddStringToObject(root, "evt", evt);

  cJSON *data = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "data", data);

  const char *mode = analyzerGetMode();
  cJSON_AddStringToObject(data, "mode", mode ? mode : "off");
  uint8_t bandsLen = analyzerGetBandsLen();
  cJSON_AddNumberToObject(data, "bands_len", bandsLen);
  cJSON_AddNumberToObject(data, "update_ms", analyzerGetUpdateMs());
  cJSON_AddNumberToObject(data, "vu", analyzerGetVu());
  cJSON_AddNumberToObject(data, "vu_l", analyzerGetVuL());
  cJSON_AddNumberToObject(data, "vu_r", analyzerGetVuR());
  cJSON_AddNumberToObject(data, "gain_l", analyzerGetGainL());
  cJSON_AddNumberToObject(data, "gain_r", analyzerGetGainR());
  cJSON_AddNumberToObject(data, "noise_l", analyzerGetNoiseL());
  cJSON_AddNumberToObject(data, "noise_r", analyzerGetNoiseR());
  cJSON_AddNumberToObject(data, "mag_l", (int)analyzerGetDiagMaxMagL());
  cJSON_AddNumberToObject(data, "mag_r", (int)analyzerGetDiagMaxMagR());
  cJSON_AddNumberToObject(data, "bk_l", analyzerGetDiagMaxBucketL());
  cJSON_AddNumberToObject(data, "bk_r", analyzerGetDiagMaxBucketR());
  cJSON_AddBoolToObject(data, "gate_l", analyzerGetGateOpenL());
  cJSON_AddBoolToObject(data, "gate_r", analyzerGetGateOpenR());
  cJSON_AddNumberToObject(data, "thr_l", (int)analyzerGetDiagThrL());
  cJSON_AddNumberToObject(data, "thr_r", (int)analyzerGetDiagThrR());
  cJSON_AddNumberToObject(data, "dc_l", (int)analyzerGetDcOffsetL());
  cJSON_AddNumberToObject(data, "dc_r", (int)analyzerGetDcOffsetR());
  if (mode && strcmp(mode, "fft") == 0) {
    const uint8_t *bandsL = analyzerGetBandsL();
    const uint8_t *bandsR = analyzerGetBandsR();
    const uint8_t *bands = analyzerGetBands();
    if (bandsL) cJSON_AddStringToObject(data, "bands_l_b64", encodeBase64(bandsL, bandsLen).c_str());
    if (bandsR) cJSON_AddStringToObject(data, "bands_r_b64", encodeBase64(bandsR, bandsLen).c_str());
    if (bands) cJSON_AddStringToObject(data, "bands_b64", encodeBase64(bands, bandsLen).c_str());
  }
  sendTelemetry(root);
}

void sendSlowTelemetry(uint32_t now) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "telemetry");

  cJSON *data = cJSON_CreateObject();
  cJSON_AddItemToObject(root, "hz1", data);

  writeTimeISO(data);
  cJSON_AddStringToObject(data, "fw_ver", FW_VERSION);
  cJSON_AddBoolToObject(data, "ota_ready", otaReady);

  cJSON *smps = cJSON_CreateObject();
  cJSON_AddItemToObject(data, "smps", smps);
  cJSON_AddNumberToObject(smps, "v", roundf(getVoltageInstant() * 10.0f) / 10.0f);
  cJSON_AddStringToObject(smps, "stage", powerSmpsTripLatched() ? "trip" : (powerIsOn() ? "armed" : "standby"));
  cJSON_AddNumberToObject(smps, "cutoff", stateSmpsCutoffV());

  cJSON_AddNumberToObject(data, "sleep_timer", powerGetSleepRemainingMinutes());
  cJSON_AddNumberToObject(smps, "recover", stateSmpsRecoveryV());

  cJSON_AddNumberToObject(data, "v_stby", roundf(getVoltageStandby() * 10.0f) / 10.0f);
  cJSON_AddNumberToObject(data, "v_vdd", roundf(getVoltageVdd() * 10.0f) / 10.0f);
  cJSON_AddNumberToObject(data, "v_esp", roundf(getVoltageEsp32() * 10.0f) / 10.0f);

  setFloatOrNull(data, "heat_c", roundf(getHeatsinkC() * 10.0f) / 10.0f);
  setFloatOrNull(data, "rtc_c", roundf(sensorsGetRtcTempC() * 10.0f) / 10.0f);

  cJSON *inputs = cJSON_CreateObject();
  cJSON_AddItemToObject(data, "inputs", inputs);
  cJSON_AddBoolToObject(inputs, "bt", powerBtMode());

  cJSON *states = cJSON_CreateObject();
  cJSON_AddItemToObject(data, "states", states);
  cJSON_AddBoolToObject(states, "on", powerIsOn());
  cJSON_AddBoolToObject(states, "standby", powerIsStandby());
  cJSON_AddBoolToObject(states, "initializing", powerIsInitializing());
  cJSON_AddBoolToObject(states, "bt_failed_perm", powerBtFailedPerm());

  cJSON *errs = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "errors", errs);
  writeErrors(errs);

  cJSON *warns = cJSON_CreateArray();
  cJSON_AddItemToObject(data, "warnings", warns);
  writeWarnings(warns);

  writeAnalyzer(data);
  writeBuzzer(data);
  writeNvsSnapshot(data);

  sendTelemetry(root);
}
