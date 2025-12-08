#include "comms.h"
#include "config.h"
#include "state.h"
#include "power.h"
#include "sensors.h"
#include "analyzer.h"
#include "buzzer.h"
#include "ota.h"
#include "main.h"

#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern HardwareSerial espSerial;
static HardwareSerial &linkSerial = espSerial;  // UART2 (RX16/TX17) - Panel telemetry
static HardwareSerial &debugSerial = Serial;     // USB CDC - Debug logs only

static String rxLine;
static uint32_t lastRxBlink = 0, lastTxBlink = 0;
static uint32_t lastRtMs = 0, lastHz1Ms = 0;
static bool otaReady = true, forceTel = false;

static inline uint32_t ms() { return millis(); }

static inline void ledRxPulse() { digitalWrite(LED_UART_PIN, HIGH); lastRxBlink = ms(); }
static inline void ledTxPulse() { digitalWrite(LED_UART_PIN, HIGH); lastTxBlink = ms(); }
static inline void ledActivityTick(uint32_t now) {
  if (now - lastRxBlink > 60 && now - lastTxBlink > 60) digitalWrite(LED_UART_PIN, LOW);
}

// Send telemetry to UART2 AND USB Serial
template <typename TDoc>
static void sendTelemetry(const TDoc &doc) {
  String out;
  serializeJson(doc, out);
  linkSerial.println(out);   // UART2 (Panel)
  debugSerial.println(out);  // USB Serial
  ledTxPulse();
}

// Send debug log to USB Serial ONLY
static void sendDebugLog(const char *msg) {
  if (!msg) return;
  debugSerial.println(msg);  // USB CDC only
}

template <typename TDoc>
static void sendDebugLogJson(const TDoc &doc) {
  String out;
  serializeJson(doc, out);
  debugSerial.println(out);  // USB CDC only
}

static bool equalsIgnoreCase(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower(*a) != tolower(*b)) return false;
    ++a; ++b;
  }
  return *a == *b;
}

static const char *fanModeToStr(FanMode m) {
  switch (m) {
    case FanMode::AUTO: return "auto";
    case FanMode::CUSTOM: return "custom";
    case FanMode::FAILSAFE: return "failsafe";
    default: return "auto";
  }
}

static bool fanModeFromStr(const char *s, FanMode &out) {
  if (equalsIgnoreCase(s, "auto")) { out = FanMode::AUTO; return true; }
  if (equalsIgnoreCase(s, "custom")) { out = FanMode::CUSTOM; return true; }
  if (equalsIgnoreCase(s, "failsafe")) { out = FanMode::FAILSAFE; return true; }
  return false;
}

static bool variantIsNumber(const JsonVariant &v) {
  return v.is<int>() || v.is<unsigned int>() || v.is<long>() || v.is<unsigned long>() ||
         v.is<float>() || v.is<double>();
}

#define HANDLE_IF_PRESENT(key, handler) \
  do { \
    JsonVariant value = cmd[key]; \
    if (!value.isNull()) handler(value); \
  } while (0)

static void writeTimeISO(JsonObject obj) {
  char buf[24];
  if (!sensorsGetTimeISO(buf, sizeof(buf)) || strlen(buf) < 20) {
    snprintf(buf, sizeof(buf), "1970-01-01T00:00:00Z");
  }
  obj["time"] = buf;
}

static void setFloatOrNull(JsonObject obj, const char *key, float value) {
  if (std::isnan(value)) obj[key] = nullptr;
  else obj[key] = value;
}

static void writeNvsSnapshot(JsonObject root) {
  JsonObject nv = root["nvs"].to<JsonObject>();
  FanMode mode = stateGetFanMode();
  nv["fan_mode"] = static_cast<uint8_t>(mode);
  nv["fan_mode_str"] = fanModeToStr(mode);
  nv["fan_duty"] = stateGetFanCustomDuty();
  nv["spk_big"] = stateSpeakerIsBig();
  nv["spk_pwr"] = stateSpeakerPowerOn();
  nv["bt_en"] = stateBtEnabled();
  nv["bt_autooff"] = stateBtAutoOffMs();
  nv["smps_bypass"] = stateSmpsBypass();
  nv["smps_cut"] = stateSmpsCutoffV();
  nv["smps_rec"] = stateSmpsRecoveryV();
  nv["buzz_enabled"] = buzzerEnabled();
  nv["buzz_volume"] = buzzerGetVolume();
  bool bzQuiet; uint8_t bzStart, bzEnd;
  buzzerGetQuietHours(bzQuiet, bzStart, bzEnd);
  JsonObject quiet = nv["buzz_quiet"].to<JsonObject>();
  quiet["enabled"] = bzQuiet;
  quiet["start"] = bzStart;
  quiet["end"] = bzEnd;
}

static void writeFeatures(JsonObject root) {
  JsonObject feats = root["features"].to<JsonObject>();
  feats["pc_detect"] = static_cast<bool>(FEAT_PC_DETECT_ENABLE);
  feats["bt_autoswitch"] = static_cast<bool>(FEAT_BT_AUTOSWITCH_AUX);
  feats["fan_boot_test"] = static_cast<bool>(FEAT_FAN_BOOT_TEST);
  feats["factory_reset_combo"] = static_cast<bool>(FEAT_FACTORY_RESET_COMBO);
  feats["rtc_temp"] = static_cast<bool>(FEAT_RTC_TEMP_TELEMETRY);
  feats["rtc_sync_policy"] = static_cast<bool>(FEAT_RTC_SYNC_POLICY);
  feats["smps_protect"] = static_cast<bool>(FEAT_SMPS_PROTECT_ENABLE);
  feats["ds18b20_softfilter"] = static_cast<bool>(FEAT_FILTER_DS18B20_SOFT);
  feats["safe_mode"] = static_cast<bool>(SAFE_MODE_SOFT);
}

static void writeErrors(JsonArray arr) {
  float v = getVoltageInstant();
  if (!stateSmpsBypass()) {
    if (v == 0.0f) arr.add("NO_POWER");
    else if (v < stateSmpsCutoffV()) arr.add("LOW_VOLTAGE");
  }
  if (isnan(getHeatsinkC())) arr.add("SENSOR_FAIL");
  if (powerSpkProtectFault()) arr.add("SPEAKER_PROTECT_FAIL");
}

static void writeAnalyzer(JsonObject data) {
  const char *mode = analyzerGetMode();
  uint8_t bandsLen = analyzerGetBandsLen();
  const uint8_t *bands = analyzerGetBands();
  uint8_t vu = analyzerGetVu();

  JsonObject an = data["analyzer"].to<JsonObject>();
  an["mode"] = mode;
  an["bands_len"] = bandsLen;
  an["update_ms"] = analyzerGetUpdateMs();
  an["vu"] = vu;
  if (mode && strcmp(mode, "fft") == 0) {
    JsonArray arr = an["bands"].to<JsonArray>();
    for (uint8_t i = 0; i < bandsLen; ++i) arr.add(static_cast<uint16_t>(bands[i]));
  }

  JsonArray legacy = data["an"].to<JsonArray>();
  for (uint8_t i = 0; i < ANA_BANDS; ++i) {
    uint16_t val = (i < bandsLen) ? static_cast<uint16_t>(bands[i]) : 0u;
    legacy.add(val);
  }
  uint16_t vu1023 = static_cast<uint16_t>(((uint32_t)vu * 1023u + 127u) / 255u);
  data["vu"] = vu1023;
}

static void writeBuzzer(JsonObject data) {
  JsonObject bz = data["buzzer"].to<JsonObject>();
  bz["enabled"] = buzzerEnabled();
  bz["last_tone"] = buzzerLastTone();
  bz["last_ms"] = buzzerLastToneAt();
  bz["quiet_now"] = buzzerQuietHoursActive();
  bool qEn; uint8_t qStart, qEnd;
  buzzerGetQuietHours(qEn, qStart, qEnd);
  JsonObject quiet = bz["quiet"].to<JsonObject>();
  quiet["enabled"] = qEn;
  quiet["start"] = qStart;
  quiet["end"] = qEnd;
}

static void writeLinkRealtime(JsonObject link, uint32_t now) {
  const uint32_t rxAge = (lastRxBlink == 0) ? 0 : (now - lastRxBlink);
  const uint32_t txAge = (lastTxBlink == 0) ? 0 : (now - lastTxBlink);
  link["alive"] = (lastRxBlink != 0) && (rxAge < 3000U);
  if (lastRxBlink == 0) link["rx_ms"] = nullptr;
  else link["rx_ms"] = rxAge;
  if (lastTxBlink == 0) link["tx_ms"] = nullptr;
  else link["tx_ms"] = txAge;
}

static void sendRealtimeTelemetry(uint32_t now) {
  if (!TELEM_REALTIME_ENABLE) return;

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "telemetry";

  JsonObject rt = root["rt"].to<JsonObject>();
  const char *mode = analyzerGetMode();
  uint8_t bandsLen = analyzerGetBandsLen();
  rt["mode"] = mode;
  rt["bands_len"] = bandsLen;
  rt["vu"] = analyzerGetVu();
  rt["update_ms"] = analyzerGetUpdateMs();
  if (mode && strcmp(mode, "fft") == 0) {
    JsonArray arr = rt["bands"].to<JsonArray>();
    const uint8_t *bands = analyzerGetBands();
    for (uint8_t i = 0; i < bandsLen; ++i) arr.add(static_cast<uint16_t>(bands[i]));
  }

  JsonObject link = rt["link"].to<JsonObject>();
  writeLinkRealtime(link, now);
  rt["input"] = powerInputModeStr();
  rt["bt_state"] = powerBtMode() ? "bt" : "aux";

  sendTelemetry(root);  // UART2 only
}

static void sendAnalyzerSnapshot(const char *evt) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "analyzer";
  if (evt && *evt) root["evt"] = evt;
  JsonObject data = root["data"].to<JsonObject>();
  const char *mode = analyzerGetMode();
  data["mode"] = mode;
  uint8_t bandsLen = analyzerGetBandsLen();
  data["bands_len"] = bandsLen;
  data["update_ms"] = analyzerGetUpdateMs();
  data["vu"] = analyzerGetVu();
  if (mode && strcmp(mode, "fft") == 0) {
    JsonArray arr = data["bands"].to<JsonArray>();
    const uint8_t *bands = analyzerGetBands();
    for (uint8_t i = 0; i < bandsLen; ++i) arr.add(static_cast<uint16_t>(bands[i]));
  }
  sendTelemetry(root);  // UART2 only
}

static void sendSlowTelemetry(uint32_t now) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "telemetry";

  JsonObject data = root["hz1"].to<JsonObject>();
  writeTimeISO(data);
  data["fw_ver"] = FW_VERSION;
  data["ota_ready"] = otaReady;

  JsonObject smps = data["smps"].to<JsonObject>();
  smps["v"] = getVoltageInstant();
  smps["stage"] = powerSmpsTripLatched() ? "trip" : (powerIsOn() ? "armed" : "standby");
  smps["cutoff"] = stateSmpsCutoffV();
  smps["recover"] = stateSmpsRecoveryV();

  data["v12"] = getVoltage12V();

  setFloatOrNull(data, "heat_c", getHeatsinkC());
  setFloatOrNull(data, "rtc_c", sensorsGetRtcTempC());

  JsonObject inputs = data["inputs"].to<JsonObject>();
  inputs["bt"] = powerBtMode();
  inputs["speaker"] = powerGetSpeakerSelectBig() ? "big" : "small";

  JsonObject states = data["states"].to<JsonObject>();
  states["on"] = powerIsOn();
  states["standby"] = powerIsStandby();

  JsonArray errs = data["errors"].to<JsonArray>();
  writeErrors(errs);

  JsonObject pc = data["pc_detect"].to<JsonObject>();
  pc["enabled"] = static_cast<bool>(FEAT_PC_DETECT_ENABLE);
  pc["armed"] = powerPcDetectArmed();
  pc["level"] = powerPcDetectLevelActive() ? "LOW" : "HIGH";
  uint32_t lastChange = powerPcDetectLastChangeMs();
  if (lastChange == 0) pc["last_change_ms"] = nullptr;
  else pc["last_change_ms"] = now - lastChange;

  writeAnalyzer(data);
  writeBuzzer(data);
  writeNvsSnapshot(data);
  writeFeatures(data);

  sendTelemetry(root);  // UART2 only
}

static void playAckTone() {
  if (!powerSpkProtectFault() && !stateSafeModeSoft()) buzzerClick();
}

template <typename TValue>
static void sendAckOk(const char *key, const TValue &value, bool tone = true) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ack";
  root["ok"] = true;
  root["changed"] = key;
  root["value"] = value;
  sendTelemetry(root);  // UART2 only
  if (tone) playAckTone();
}

static void sendAckErr(const char *key, const char *reason) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ack";
  root["ok"] = false;
  root["error"] = reason ? reason : "invalid";
  if (key) root["changed"] = key;
  sendTelemetry(root);  // UART2 only
}

static void sendLogInfoOffset(int32_t offset) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["ver"] = "1";
  root["type"] = "log";
  root["lvl"] = "info";
  root["msg"] = "rtc_synced";
  root["offset_sec"] = offset;
  sendTelemetry(root);     // UART2
  sendDebugLogJson(root);  // USB debug
}

static void sendLogWarnReason(const char *msg, const char *reason) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["ver"] = "1";
  root["type"] = "log";
  root["lvl"] = "warn";
  root["msg"] = msg;
  root["reason"] = reason;
  sendTelemetry(root);     // UART2
  sendDebugLogJson(root);  // USB debug
}

static void sendLogErrorReason(const char *msg, const char *reason) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["ver"] = "1";
  root["type"] = "log";
  root["lvl"] = "error";
  root["msg"] = msg;
  root["reason"] = reason;
  sendTelemetry(root);     // UART2
  sendDebugLogJson(root);  // USB debug
}

void commsLogFactoryReset(const char* src) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["ver"] = "1";
  root["type"] = "log";
  root["lvl"] = "info";
  root["msg"] = "factory_reset_executed";
  if (src && src[0] != '\0') root["src"] = src;
  sendTelemetry(root);     // UART2
  sendDebugLogJson(root);  // USB debug
}

static void sendOtaEvent(const char *evt) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ota";
  root["evt"] = evt;
  sendTelemetry(root);  // UART2 only
}

template <typename TValue>
static void sendOtaEvent(const char *evt, const char *field, const TValue &value) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ota";
  root["evt"] = evt;
  root[field] = value;
  sendTelemetry(root);  // UART2 only
}

static void sendOtaWriteOk(uint32_t seq) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ota";
  root["evt"] = "write_ok";
  root["seq"] = seq;
  sendTelemetry(root);  // UART2 only
}

static void sendOtaWriteErr(uint32_t seq, const char *err) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ota";
  root["evt"] = "write_err";
  root["seq"] = seq;
  root["err"] = err ? err : "error";
  sendTelemetry(root);  // UART2 only
}

static void sendOtaError(const char *err) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["type"] = "ota";
  root["evt"] = "error";
  root["err"] = err ? err : "unknown";
  sendTelemetry(root);  // UART2 only
}

static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

static bool parseIso8601ToEpoch(const char *iso, uint32_t &epochOut) {
  if (!iso) return false;
  int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
  if (sscanf(iso, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &m, &d, &hh, &mm, &ss) != 6) return false;
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31 ||
      hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return false;
  int64_t days = daysFromCivil(y, (unsigned)m, (unsigned)d);
  int64_t secs = days * 86400 + hh * 3600 + mm * 60 + ss;
  if (secs < 0 || secs > 0xFFFFFFFFLL) return false;
  epochOut = (uint32_t)secs;
  return true;
}

static bool parseHex32(const char *hex, uint32_t &valueOut) {
  if (!hex) return false;
  uint32_t val = 0;
  const char *p = hex;
  if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
  int digits = 0;
  while (*p) {
    char c = *p++;
    uint8_t nibble = 0;
    if (c >= '0' && c <= '9') nibble = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
    else return false;
    val = (val << 4) | nibble;
    ++digits;
    if (digits > 8) return false;
  }
  if (digits == 0) return false;
  valueOut = val;
  return true;
}

static void handleRtcSync(uint32_t targetEpoch) {
  uint32_t currentEpoch = 0;
  if (!sensorsGetUnixTime(currentEpoch)) {
    sendLogErrorReason("rtc_sync_failed", "rtc_unavailable");
    return;
  }

  int32_t offset = (int32_t)((int64_t)targetEpoch - (int64_t)currentEpoch);
  int32_t absOffset = offset >= 0 ? offset : -offset;
  if (!FEAT_RTC_SYNC_POLICY) {
    if (!sensorsSetUnixTime(targetEpoch)) {
      sendLogErrorReason("rtc_sync_failed", "rtc_set_fail");
      return;
    }
    stateSetLastRtcSync(targetEpoch);
    sendLogInfoOffset(offset);
    forceTel = true;
    return;
  }
  if (absOffset <= RTC_SYNC_MIN_OFFS_SEC) {
    sendLogWarnReason("rtc_sync_skipped", "offset_small");
    return;
  }

  const uint32_t minInterval = (uint32_t)RTC_SYNC_MIN_INTERVAL_H * 3600UL;
  uint32_t last = stateLastRtcSync();
  uint32_t ref = (targetEpoch > currentEpoch) ? targetEpoch : currentEpoch;
  if (last != 0 && (ref - last) < minInterval) {
    sendLogWarnReason("rtc_sync_skipped", "ratelimited");
    return;
  }

  if (!sensorsSetUnixTime(targetEpoch)) {
    sendLogErrorReason("rtc_sync_failed", "rtc_set_fail");
    return;
  }

  stateSetLastRtcSync(targetEpoch);
  sendLogInfoOffset(offset);
  forceTel = true;
}

// Force sync RTC (bypass rate limit)
static void handleRtcSyncForce(uint32_t targetEpoch) {
  uint32_t currentEpoch = 0;
  sensorsGetUnixTime(currentEpoch);
  
  if (!sensorsSetUnixTime(targetEpoch)) {
    sendLogErrorReason("rtc_sync_failed", "rtc_set_fail");
    return;
  }
  
  int32_t offset = (int32_t)((int64_t)targetEpoch - (int64_t)currentEpoch);
  stateSetLastRtcSync(targetEpoch);
  sendLogInfoOffset(offset);
  forceTel = true;
}

static void handleCmdPower(JsonVariant v) {
  if (!v.is<bool>()) { sendAckErr("power", "invalid"); return; }
  bool on = v.as<bool>();
  powerSetMainRelay(on, PowerChangeReason::Command);
  sendAckOk("power", on);
  forceTel = true;
}

static void handleCmdBt(JsonVariant v) {
  if (!v.is<bool>()) { sendAckErr("bt", "invalid"); return; }
  bool en = v.as<bool>();
  powerSetBtEnabled(en);
  sendAckOk("bt", en);
  forceTel = true;
}

static void handleCmdSpkSel(JsonVariant v) {
  if (!v.is<const char*>()) { sendAckErr("spk_sel", "invalid"); return; }
  const char *s = v.as<const char*>();
  bool big;
  if (equalsIgnoreCase(s, "big")) big = true;
  else if (equalsIgnoreCase(s, "small")) big = false;
  else { sendAckErr("spk_sel", "invalid"); return; }
  powerSetSpeakerSelect(big);
  sendAckOk("spk_sel", big ? "big" : "small");
  forceTel = true;
}

static void handleCmdSpkPwr(JsonVariant v) {
  if (!v.is<bool>()) { sendAckErr("spk_pwr", "invalid"); return; }
  bool on = v.as<bool>();
  powerSetSpeakerPower(on);
  sendAckOk("spk_pwr", on);
  forceTel = true;
}

static void handleCmdSmpsBypass(JsonVariant v) {
  if (!v.is<bool>()) { sendAckErr("smps_bypass", "invalid"); return; }
  bool en = v.as<bool>();
  stateSetSmpsBypass(en);
  sendAckOk("smps_bypass", en);
  forceTel = true;
}

static void handleCmdSmpsCut(JsonVariant v) {
  if (!variantIsNumber(v)) { sendAckErr("smps_cut", "invalid"); return; }
  float cut = v.as<float>();
  if (cut < 30.0f || cut > 70.0f || cut >= stateSmpsRecoveryV()) {
    sendAckErr("smps_cut", "range"); return;
  }
  stateSetSmpsCutoffV(cut);
  sendAckOk("smps_cut", cut);
  forceTel = true;
}

static void handleCmdSmpsRec(JsonVariant v) {
  if (!variantIsNumber(v)) { sendAckErr("smps_rec", "invalid"); return; }
  float rec = v.as<float>();
  if (rec < 30.0f || rec > 80.0f || rec <= stateSmpsCutoffV()) {
    sendAckErr("smps_rec", "range"); return;
  }
  stateSetSmpsRecoveryV(rec);
  sendAckOk("smps_rec", rec);
  forceTel = true;
}

static void handleCmdBtAutoOff(JsonVariant v) {
  if (!variantIsNumber(v)) { sendAckErr("bt_autooff", "invalid"); return; }
  double valD = v.as<double>();
  if (valD < 0.0 || valD > 3600000.0) { sendAckErr("bt_autooff", "range"); return; }
  uint32_t val = (uint32_t)(valD + 0.5);
  stateSetBtAutoOffMs(val);
  sendAckOk("bt_autooff", val);
  forceTel = true;
}

static void handleCmdFanMode(JsonVariant v) {
  if (!v.is<const char*>()) { sendAckErr("fan_mode", "invalid"); return; }
  FanMode mode;
  if (!fanModeFromStr(v.as<const char*>(), mode)) {
    sendAckErr("fan_mode", "invalid"); return;
  }
  stateSetFanMode(mode);
  sendAckOk("fan_mode", fanModeToStr(mode));
  forceTel = true;
}

static void handleCmdFanDuty(JsonVariant v) {
  if (!variantIsNumber(v)) { sendAckErr("fan_duty", "invalid"); return; }
  int duty = (int)std::lround(v.as<double>());
  if (duty < 0 || duty > 1023) { sendAckErr("fan_duty", "range"); return; }
  stateSetFanCustomDuty((uint16_t)duty);
  sendAckOk("fan_duty", duty);
  forceTel = true;
}

static void handleCmdRtcSet(JsonVariant v) {
  if (!v.is<const char*>()) { sendAckErr("rtc_set", "invalid"); return; }
  uint32_t epoch = 0;
  if (!parseIso8601ToEpoch(v.as<const char*>(), epoch)) {
    sendAckErr("rtc_set", "invalid"); return;
  }
  handleRtcSync(epoch);
}

static void handleCmdRtcSetEpoch(JsonVariant v) {
  if (!variantIsNumber(v)) { sendAckErr("rtc_set_epoch", "invalid"); return; }
  uint32_t epoch = v.as<uint32_t>();
  handleRtcSyncForce(epoch);  // Use force sync to bypass rate limit
}

static void handleCmdBuzz(JsonVariant v) {
  if (!v.is<JsonObject>()) { sendAckErr("buzz", "invalid"); return; }
  JsonObject o = v.as<JsonObject>();
  uint32_t f = o["f"] | BUZZER_PWM_BASE_FREQ;
  uint16_t d = o["d"] | BUZZER_DUTY_DEFAULT;
  uint16_t msDur = o["ms"] | 60;
  buzzerCustom(f, d, msDur);
  sendAckOk("buzz", true, false);
}

static void handleCmdNvsReset(JsonVariant v) {
  if (!v.is<bool>() || !v.as<bool>()) { sendAckErr("nvs_reset", "invalid"); return; }
  stateFactoryReset();
  powerSetSpeakerSelect(stateSpeakerIsBig());
  powerSetSpeakerPower(stateSpeakerPowerOn());
  powerSetBtEnabled(stateBtEnabled());
  sendAckOk("nvs_reset", true);
  forceTel = true;
}

static void handleCmdFactoryReset(JsonVariant v) {
  if (!v.is<bool>() || !v.as<bool>()) { sendAckErr("factory_reset", "invalid"); return; }
  if (powerIsOn()) { sendAckErr("factory_reset", "system_active"); return; }
  sendAckOk("factory_reset", true);
  forceTel = true;
  appPerformFactoryReset("FACTORY RESET (UART)", "uart");
}

static void handleCmdOtaBegin(JsonVariant v) {
  if (!v.is<JsonObject>()) {
    sendOtaEvent("begin_err", "err", "invalid");
    sendOtaError("invalid_begin_payload");
    return;
  }
  JsonObject o = v.as<JsonObject>();
  size_t size = o["size"] | 0;
  const char *crcHex = o["crc32"] | nullptr;
  uint32_t crc = 0;
  if (crcHex && crcHex[0] != '\0') {
    if (!parseHex32(crcHex, crc)) {
      sendOtaEvent("begin_err", "err", "crc_invalid");
      sendOtaError("crc_invalid");
      return;
    }
  }
  if (!otaBegin(size, crc)) {
    const char *err = otaLastError();
    sendOtaEvent("begin_err", "err", err);
    sendOtaError(err);
    return;
  }
  powerSetOtaActive(true);
  commsSetOtaReady(false);
  sendOtaEvent("begin_ok");
  forceTel = true;
}

static void handleCmdOtaWrite(JsonVariant v) {
  if (!v.is<JsonObject>()) {
    sendOtaEvent("write_err", "err", "invalid");
    sendOtaError("invalid_write_payload");
    return;
  }
  JsonObject o = v.as<JsonObject>();
  uint32_t seq = o["seq"] | 0;
  const char *dataB64 = o["data_b64"] | nullptr;
  if (!dataB64) {
    sendOtaWriteErr(seq, "invalid_data");
    sendOtaError("invalid_data");
    return;
  }
  size_t inLen = strlen(dataB64);
  std::vector<uint8_t> decoded((inLen * 3) / 4 + 4);
  size_t outLen = 0;
  int rc = mbedtls_base64_decode(decoded.data(), decoded.size(), &outLen,
                                 reinterpret_cast<const unsigned char*>(dataB64), inLen);
  if (rc != 0) {
    sendOtaWriteErr(seq, "b64_decode");
    sendOtaError("b64_decode");
    return;
  }
  int wrote = otaWrite(decoded.data(), outLen);
  if (wrote < 0) {
    const char *err = otaLastError();
    sendOtaWriteErr(seq, err);
    sendOtaError(err);
    return;
  }
  sendOtaWriteOk(seq);
  otaYieldOnce();
}

static void handleCmdOtaEnd(JsonVariant v) {
  if (!v.is<JsonObject>()) {
    sendOtaEvent("end_err", "err", "invalid");
    sendOtaError("invalid_end_payload");
    return;
  }
  JsonObject o = v.as<JsonObject>();
  bool reboot = o["reboot"] | false;
  if (!otaEnd(reboot)) {
    const char *err = otaLastError();
    sendOtaEvent("end_err", "err", err);
    sendOtaError(err);
    return;
  }
  if (!reboot) {
    powerSetOtaActive(false);
    commsSetOtaReady(true);
  }
  sendOtaEvent("end_ok", "rebooting", reboot);
  forceTel = true;
}

static void handleCmdOtaAbort(JsonVariant v) {
  bool doAbort = v.is<bool>() ? v.as<bool>() : true;
  if (!doAbort) { sendOtaEvent("abort_ok"); return; }
  otaAbort();
  sendOtaEvent("abort_ok");
  forceTel = true;
}

static void handleAnalyzerJson(JsonObject obj) {
  const char *cmd = obj["cmd"] | "get";
  if (strcmp(cmd, "set") == 0) {
    if (obj["mode"].is<const char*>()) analyzerSetMode(obj["mode"].as<const char*>());
    if (obj["bands"].is<int>()) analyzerSetBands(static_cast<uint8_t>(obj["bands"].as<int>()));
    if (obj["update_ms"].is<int>()) analyzerSetUpdateMs(static_cast<uint16_t>(obj["update_ms"].as<int>()));
    analyzerSaveToNvs();
    sendAckOk("analyzer", "set");
    sendAnalyzerSnapshot("set");
    forceTel = true;
  } else if (strcmp(cmd, "get") == 0) {
    sendAnalyzerSnapshot("get");
  } else {
    sendAckErr("analyzer", "invalid_cmd");
  }
}

static void handleJsonLine(const String &line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return;

  const char *type = doc["type"] | "";
  if (strcmp(type, "analyzer") == 0) {
    JsonObject root = doc.as<JsonObject>();
    handleAnalyzerJson(root);
    return;
  }
  if (strcmp(type, "cmd") != 0 && strcmp(type, "command") != 0) return;

  JsonObject root = doc.as<JsonObject>();
  JsonObject cmd = root["cmd"];
  if (cmd.isNull()) return;

  HANDLE_IF_PRESENT("power", handleCmdPower);
  HANDLE_IF_PRESENT("bt", handleCmdBt);
  HANDLE_IF_PRESENT("spk_sel", handleCmdSpkSel);
  HANDLE_IF_PRESENT("spk_pwr", handleCmdSpkPwr);
  HANDLE_IF_PRESENT("smps_bypass", handleCmdSmpsBypass);
  HANDLE_IF_PRESENT("smps_cut", handleCmdSmpsCut);
  HANDLE_IF_PRESENT("smps_rec", handleCmdSmpsRec);
  HANDLE_IF_PRESENT("bt_autooff", handleCmdBtAutoOff);
  HANDLE_IF_PRESENT("fan_mode", handleCmdFanMode);
  HANDLE_IF_PRESENT("fan_duty", handleCmdFanDuty);
  HANDLE_IF_PRESENT("rtc_set", handleCmdRtcSet);
  HANDLE_IF_PRESENT("rtc_set_epoch", handleCmdRtcSetEpoch);
  HANDLE_IF_PRESENT("ota_begin", handleCmdOtaBegin);
  HANDLE_IF_PRESENT("ota_write", handleCmdOtaWrite);
  HANDLE_IF_PRESENT("ota_end", handleCmdOtaEnd);
  HANDLE_IF_PRESENT("ota_abort", handleCmdOtaAbort);
  HANDLE_IF_PRESENT("buzz", handleCmdBuzz);
  HANDLE_IF_PRESENT("nvs_reset", handleCmdNvsReset);
  HANDLE_IF_PRESENT("factory_reset", handleCmdFactoryReset);
}

#undef HANDLE_IF_PRESENT

void commsInit() {
  pinMode(LED_UART_PIN, OUTPUT);
  digitalWrite(LED_UART_PIN, LOW);

  // Initialize USB Serial for debug logs only (921600 baud)
  debugSerial.begin(SERIAL_BAUD_USB);
  debugSerial.println("[DEBUG] USB Serial initialized (921600 baud)");
  
  // Initialize UART2 for Panel telemetry (921600 baud)
  linkSerial.begin(SERIAL_BAUD_LINK, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);
  
  rxLine.reserve(4096);
  lastRtMs = 0;
  lastHz1Ms = 0;
  otaReady = true;
  forceTel = true;
  
  sendDebugLog("[DEBUG] UART2 initialized (921600 baud)");
}

void commsTick(uint32_t now, bool sqwTick) {
  ledActivityTick(now);

  // Read commands from UART2 (Panel) - primary source
  while (linkSerial.available()) {
    int c = linkSerial.read();
    if (c < 0) break;
    ledRxPulse();

    if (c == '\n' || c == '\r') {
      if (rxLine.length() > 0) {
        handleJsonLine(rxLine);
        rxLine = "";
      }
    } else {
      if (rxLine.length() < 4000) rxLine += (char)c;
      else rxLine = "";
    }
  }
  
  // Also read from USB Serial (for testing/debugging)
  while (debugSerial.available()) {
    int c = debugSerial.read();
    if (c < 0) break;
    ledRxPulse();

    if (c == '\n' || c == '\r') {
      if (rxLine.length() > 0) {
        handleJsonLine(rxLine);
        rxLine = "";
      }
    } else {
      if (rxLine.length() < 4000) rxLine += (char)c;
      else rxLine = "";
    }
  }

  // Send realtime telemetry to UART2 only (if system ON)
  if (TELEM_REALTIME_ENABLE && powerIsOn()) {
    uint32_t intervalRt = (TELEM_HZ_REALTIME > 0) ? (1000UL / TELEM_HZ_REALTIME) : 0;
    if (intervalRt == 0 || now - lastRtMs >= intervalRt) {
      sendRealtimeTelemetry(now);
      lastRtMs = now;
    }
  }

  // Send slow telemetry to UART2 only
  bool shouldSendSlow = forceTel;
  uint32_t slowInterval = (TELEM_SLOW_HZ > 0) ? (1000UL / TELEM_SLOW_HZ) : 0;
  if (!shouldSendSlow) {
    if (sqwTick) shouldSendSlow = true;
    else if (slowInterval == 0 || now - lastHz1Ms >= slowInterval) shouldSendSlow = true;
  }

  if (shouldSendSlow) {
    sendSlowTelemetry(now);
    lastHz1Ms = now;
    forceTel = false;
  }
}

void commsForceTelemetry() { forceTel = true; }

void commsSetOtaReady(bool ready) {
  if (otaReady != ready) {
    otaReady = ready;
    forceTel = true;
  }
}

void commsLog(const char* level, const char* msg) {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["ver"] = "1";
  root["type"] = "log";
  root["lvl"] = level ? level : "info";
  root["msg"] = msg ? msg : "";
  sendTelemetry(root);     // UART2
  sendDebugLogJson(root);  // USB debug
}