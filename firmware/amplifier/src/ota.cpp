#include "ota.h"
#include "config.h"
#include "comms.h"
#include "power.h"

#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>

static OtaStatus status = OtaStatus::Idle;
static String errMsg;
static size_t expectedSize = 0;
static uint32_t expectedCrc = 0;
static size_t written = 0;
static uint32_t crcRunning = 0;
static bool rebootPending = false;
static uint32_t rebootAt = 0;

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i=0; i<256; ++i) {
      uint32_t c = i;
      for (int j=0; j<8; ++j) {
        if (c & 1) c = 0xEDB88320UL ^ (c >> 1);
        else c = c >> 1;
      }
      table[i] = c;
    }
    init = true;
  }
  crc = ~crc;
  for (size_t i=0; i<len; ++i) {
    crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

static inline void setError(const char* msg) {
  errMsg = msg ? msg : "OTA error";
}

void otaInit() {
  status = OtaStatus::Idle;
  errMsg = "";
  expectedSize = 0;
  expectedCrc = 0;
  written = 0;
  crcRunning = 0;
  rebootPending = false;
  rebootAt = 0;
  commsSetOtaReady(true);
  powerSetOtaActive(false);
}

void otaTick(uint32_t now) {
  if (rebootPending && now >= rebootAt) {
    rebootPending = false;
    delay(50);
    ESP.restart();
  }
}

OtaStatus otaStatus() { return status; }
const char* otaLastError() { return errMsg.c_str(); }

bool otaBegin(size_t sz, uint32_t crc32) {
  if (status == OtaStatus::InProgress) {
    setError("OTA already in progress");
    return false;
  }
  if (sz == 0 || sz > OTA_MAX_BIN_SIZE) {
    setError("Invalid size");
    return false;
  }

  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  if (!next) {
    setError("No OTA partition");
    return false;
  }

  if (!Update.begin(sz, U_FLASH, 0, HIGH)) {
    setError(Update.errorString());
    return false;
  }

  commsSetOtaReady(false);
  powerSetOtaActive(true);

  expectedSize = sz;
  expectedCrc = crc32;
  written = 0;
  crcRunning = 0;
  status = OtaStatus::InProgress;
  errMsg = "";
  rebootPending = false;
  rebootAt = 0;

  return true;
}

int otaWrite(const uint8_t* data, size_t len) {
  if (status != OtaStatus::InProgress) {
    setError("OTA not started");
    return -1;
  }
  if (!data || len == 0) return 0;

  size_t remain = (expectedSize > written) ? (expectedSize - written) : 0;
  if (len > remain) len = remain;

  size_t w = Update.write(const_cast<uint8_t*>(data), len);
  if (w != len) {
    setError(Update.errorString());
    status = OtaStatus::Failed;
    return -1;
  }
  written += w;
  if (expectedCrc) {
    crcRunning = crc32_update(crcRunning, data, w);
  }
  return (int)w;
}

bool otaEnd(bool doReboot) {
  if (status != OtaStatus::InProgress) {
    setError("OTA not in progress");
    return false;
  }

  if (written != expectedSize) {
    setError("Size mismatch");
    status = OtaStatus::Failed;
    Update.abort();
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  if (expectedCrc && crcRunning != expectedCrc) {
    setError("CRC mismatch");
    status = OtaStatus::Failed;
    Update.abort();
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  if (!Update.end(true)) {
    setError(Update.errorString());
    status = OtaStatus::Failed;
    Update.abort();
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  status = OtaStatus::Success;
  errMsg = "";

  if (!doReboot) {
    commsSetOtaReady(true);
    powerSetOtaActive(false);
  }

  if (doReboot) {
    rebootPending = true;
    rebootAt = millis() + 200;
  }
  return true;
}

void otaAbort() {
  if (status == OtaStatus::InProgress) {
    Update.abort();
  }
  status = OtaStatus::Idle;
  errMsg = "OTA aborted";
  expectedSize = 0;
  expectedCrc = 0;
  written = 0;
  crcRunning = 0;
  rebootPending = false;
  rebootAt = 0;

  commsSetOtaReady(true);
  powerSetOtaActive(false);
}

void otaYieldOnce() {
  delay(0);
}