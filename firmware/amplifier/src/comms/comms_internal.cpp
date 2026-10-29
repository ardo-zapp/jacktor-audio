#include "comms_internal.h"
#include "config.h"
#include "state.h"
#include "buzzer.h"
#include "power.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cctype>

uint32_t lastRxBlink = 0;
uint32_t lastTxBlink = 0;

static SemaphoreHandle_t s_uartTxMutex = NULL;

static void ensureUartTxMutex() {
  if (s_uartTxMutex == NULL) {
    s_uartTxMutex = xSemaphoreCreateRecursiveMutex();
  }
}

bool takeUartTxMutex(TickType_t xTicksToWait) {
  ensureUartTxMutex();
  if (s_uartTxMutex != NULL) {
    return (xSemaphoreTakeRecursive(s_uartTxMutex, xTicksToWait) == pdTRUE);
  }
  return true;
}

void giveUartTxMutex() {
  if (s_uartTxMutex != NULL) {
    xSemaphoreGiveRecursive(s_uartTxMutex);
  }
}

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void ledRxPulse() {
  lastRxBlink = millis();
}

void sendTelemetry(cJSON *root) {
  if (!root) return;
  char *rendered = cJSON_PrintUnformatted(root);
  if (rendered) {
    if (takeUartTxMutex()) {
      linkSerial.println(rendered);
      debugSerial.println(rendered);
      giveUartTxMutex();
    }
    cJSON_free(rendered);
  }
  cJSON_Delete(root);
  lastTxBlink = millis();
}

void sendDebugLogJson(cJSON *root) {
  if (!root) return;
  char *rendered = cJSON_PrintUnformatted(root);
  if (rendered) {
    if (takeUartTxMutex()) {
      linkSerial.println(rendered);
      debugSerial.println(rendered);
      giveUartTxMutex();
    }
    cJSON_free(rendered);
  }
  cJSON_Delete(root);
}

void sendDebugLog(const char *msg) {
  if (!msg || !*msg) return;
  if (takeUartTxMutex()) {
    linkSerial.println(msg);
    debugSerial.println(msg);
    giveUartTxMutex();
  }
}

bool equalsIgnoreCase(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a; ++b;
  }
  return *a == *b;
}

const char *fanModeToStr(FanMode m) {
  switch (m) {
    case FanMode::AUTO: return "auto";
    case FanMode::CUSTOM: return "custom";
    case FanMode::FAILSAFE: return "failsafe";
    default: return "auto";
  }
}

bool fanModeFromStr(const char *s, FanMode &out) {
  if (equalsIgnoreCase(s, "auto")) { out = FanMode::AUTO; return true; }
  if (equalsIgnoreCase(s, "custom")) { out = FanMode::CUSTOM; return true; }
  if (equalsIgnoreCase(s, "failsafe")) { out = FanMode::FAILSAFE; return true; }
  return false;
}

void playAckTone() {
  if (!powerSpkProtectFault() && !stateSafeModeSoft()) buzzerClick();
}

void sendAckOkBool(const char *key, bool val, bool tone) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddBoolToObject(root, "ok", true);
  if (key) cJSON_AddStringToObject(root, "changed", key);
  cJSON_AddBoolToObject(root, "value", val);
  sendTelemetry(root);
  if (tone) playAckTone();
}

void sendAckOkInt(const char *key, int val, bool tone) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddBoolToObject(root, "ok", true);
  if (key) cJSON_AddStringToObject(root, "changed", key);
  cJSON_AddNumberToObject(root, "value", val);
  sendTelemetry(root);
  if (tone) playAckTone();
}

void sendAckOkFloat(const char *key, float val, bool tone) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddBoolToObject(root, "ok", true);
  if (key) cJSON_AddStringToObject(root, "changed", key);
  cJSON_AddNumberToObject(root, "value", val);
  sendTelemetry(root);
  if (tone) playAckTone();
}

void sendAckOkStr(const char *key, const char *val, bool tone) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddBoolToObject(root, "ok", true);
  if (key) cJSON_AddStringToObject(root, "changed", key);
  cJSON_AddStringToObject(root, "value", val ? val : "");
  sendTelemetry(root);
  if (tone) playAckTone();
}

void sendAckErr(const char *key, const char *reason) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ack");
  cJSON_AddBoolToObject(root, "ok", false);
  cJSON_AddStringToObject(root, "error", reason ? reason : "invalid");
  if (key) cJSON_AddStringToObject(root, "changed", key);
  sendTelemetry(root);
}

void sendOtaEvent(const char *evt) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", evt ? evt : "");
  sendTelemetry(root);
}

void sendOtaEventFieldStr(const char *evt, const char *field, const char *value) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", evt ? evt : "");
  if (field) cJSON_AddStringToObject(root, field, value ? value : "");
  sendTelemetry(root);
}

void sendOtaEventFieldBool(const char *evt, const char *field, bool value) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", evt ? evt : "");
  if (field) cJSON_AddBoolToObject(root, field, value);
  sendTelemetry(root);
}

void sendOtaWriteOk(uint32_t seq) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", "write_ok");
  cJSON_AddNumberToObject(root, "seq", seq);
  sendTelemetry(root);
}

void sendOtaWriteErr(uint32_t seq, const char *err) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", "write_err");
  cJSON_AddNumberToObject(root, "seq", seq);
  cJSON_AddStringToObject(root, "err", err ? err : "error");
  sendTelemetry(root);
}

void sendOtaError(const char *err) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", "ota");
  cJSON_AddStringToObject(root, "evt", "error");
  cJSON_AddStringToObject(root, "err", err ? err : "unknown");
  sendTelemetry(root);
}

void sendLogInfoOffset(int32_t offset) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ver", "1");
  cJSON_AddStringToObject(root, "type", "log");
  cJSON_AddStringToObject(root, "lvl", "info");
  cJSON_AddStringToObject(root, "msg", "rtc_synced");
  cJSON_AddNumberToObject(root, "offset_sec", offset);
  sendTelemetry(root);
}

void sendLogErrorReason(const char *msg, const char *reason) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ver", "1");
  cJSON_AddStringToObject(root, "type", "log");
  cJSON_AddStringToObject(root, "lvl", "error");
  cJSON_AddStringToObject(root, "msg", msg ? msg : "");
  cJSON_AddStringToObject(root, "reason", reason ? reason : "");
  sendTelemetry(root);
}
