#include "comms_handlers.h"
#include "comms_internal.h"
#include "comms_telemetry.h"
#include "ota.h"
#include "power.h"
#include "main.h"

#include "cJSON.h"
#include <mbedtls/base64.h>
#include <vector>
#include <cstring>

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

void handleCmdOtaBegin(cJSON *item) {
  if (!cJSON_IsObject(item)) {
    sendOtaEventFieldStr("begin_err", "err", "invalid");
    sendOtaError("invalid_begin_payload");
    return;
  }
  cJSON *sizeItem = cJSON_GetObjectItem(item, "size");
  size_t size = (sizeItem && cJSON_IsNumber(sizeItem)) ? (size_t)sizeItem->valuedouble : 0;

  cJSON *crcItem = cJSON_GetObjectItem(item, "crc32");
  const char *crcHex = (crcItem && cJSON_IsString(crcItem)) ? crcItem->valuestring : nullptr;

  uint32_t crc = 0;
  if (crcHex && crcHex[0] != '\0') {
    if (!parseHex32(crcHex, crc)) {
      sendOtaEventFieldStr("begin_err", "err", "crc_invalid");
      sendOtaError("crc_invalid");
      return;
    }
  }
  if (!otaBegin(size, crc)) {
    const char *err = otaLastError();
    sendOtaEventFieldStr("begin_err", "err", err);
    sendOtaError(err);
    return;
  }
  powerSetOtaActive(true);
  otaReady = false;
  forceTel = true;
  sendOtaEvent("begin_ok");
}

void handleCmdOtaWrite(cJSON *item) {
  if (!cJSON_IsObject(item)) {
    sendOtaEventFieldStr("write_err", "err", "invalid");
    sendOtaError("invalid_write_payload");
    return;
  }

  cJSON *seqItem = cJSON_GetObjectItem(item, "seq");
  uint32_t seq = (seqItem && cJSON_IsNumber(seqItem)) ? (uint32_t)seqItem->valuedouble : 0;

  cJSON *dataItem = cJSON_GetObjectItem(item, "data_b64");
  const char *dataB64 = (dataItem && cJSON_IsString(dataItem)) ? dataItem->valuestring : nullptr;

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

void handleCmdOtaEnd(cJSON *item) {
  if (!cJSON_IsObject(item)) {
    sendOtaEventFieldStr("end_err", "err", "invalid");
    sendOtaError("invalid_end_payload");
    return;
  }
  cJSON *rebootItem = cJSON_GetObjectItem(item, "reboot");
  bool reboot = (rebootItem && cJSON_IsBool(rebootItem)) ? cJSON_IsTrue(rebootItem) : false;

  if (!otaEnd(reboot)) {
    const char *err = otaLastError();
    sendOtaEventFieldStr("end_err", "err", err);
    sendOtaError(err);
    return;
  }
  if (!reboot) {
    powerSetOtaActive(false);
    otaReady = true;
    forceTel = true;
  }
  sendOtaEventFieldBool("end_ok", "rebooting", reboot);
}

void handleCmdOtaAbort(cJSON *item) {
  bool doAbort = cJSON_IsBool(item) ? cJSON_IsTrue(item) : true;
  if (!doAbort) { sendOtaEvent("abort_ok"); return; }
  otaAbort();
  sendOtaEvent("abort_ok");
  forceTel = true;
}
