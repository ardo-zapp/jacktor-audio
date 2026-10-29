#include "ota.h"
#include "config.h"
#include "comms.h"
#include "power.h"

#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>

static OtaStatus status = OtaStatus::Idle;
static std::string errMsg;
static size_t expectedSize = 0;
static uint32_t expectedCrc = 0;
static size_t written = 0;
static uint32_t crcRunning = 0;
static bool rebootPending = false;
static uint32_t rebootAt = 0;

static esp_ota_handle_t updateHandle = 0;
static const esp_partition_t *updatePartition = nullptr;

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

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
  updateHandle = 0;
  updatePartition = nullptr;
  commsSetOtaReady(true);
  powerSetOtaActive(false);
}

void otaTick(uint32_t now) {
  if (rebootPending && now >= rebootAt) {
    rebootPending = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
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

  updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    setError("No OTA partition");
    return false;
  }

  esp_err_t err = esp_ota_begin(updatePartition, sz, &updateHandle);
  if (err != ESP_OK) {
    setError(esp_err_to_name(err));
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

  esp_err_t err = esp_ota_write(updateHandle, data, len);
  if (err != ESP_OK) {
    setError(esp_err_to_name(err));
    status = OtaStatus::Failed;
    return -1;
  }
  written += len;
  if (expectedCrc) {
    crcRunning = crc32_update(crcRunning, data, len);
  }
  return (int)len;
}

bool otaEnd(bool doReboot) {
  if (status != OtaStatus::InProgress) {
    setError("OTA not in progress");
    return false;
  }

  if (written != expectedSize) {
    setError("Size mismatch");
    status = OtaStatus::Failed;
    esp_ota_abort(updateHandle);
    updateHandle = 0;
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  if (expectedCrc && crcRunning != expectedCrc) {
    setError("CRC mismatch");
    status = OtaStatus::Failed;
    esp_ota_abort(updateHandle);
    updateHandle = 0;
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  esp_err_t err = esp_ota_end(updateHandle);
  updateHandle = 0;
  if (err != ESP_OK) {
    setError(esp_err_to_name(err));
    status = OtaStatus::Failed;
    commsSetOtaReady(true);
    powerSetOtaActive(false);
    return false;
  }

  err = esp_ota_set_boot_partition(updatePartition);
  if (err != ESP_OK) {
    setError(esp_err_to_name(err));
    status = OtaStatus::Failed;
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
  if (status == OtaStatus::InProgress && updateHandle != 0) {
    esp_ota_abort(updateHandle);
    updateHandle = 0;
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
  vTaskDelay(1);
}