#pragma once
#include <cstdint>
#include <cstddef>

// Concise OTA status
enum class OtaStatus : uint8_t {
  Idle = 0,
  InProgress,
  Success,
  Failed
};

// Init/loop helper for UART OTA state machine
void otaInit();
void otaTick(uint32_t now);

// Start OTA session.
// - expectedSize : .bin file size (mandatory, >0, <= OTA_MAX_BIN_SIZE)
// - expectedCrc32: full file CRC32 (0 = skip CRC check, otherwise must match)
// Return: true if session initialized successfully, false on failure (see otaLastError()).
bool otaBegin(size_t expectedSize, uint32_t expectedCrc32);

// Write binary chunk to active OTA partition.
// Return: actual bytes written; -1 on error (see otaLastError()).
int  otaWrite(const uint8_t* data, size_t len);

// Finalize OTA session.
// - doReboot: if true and success → setBootPartition + ESP.restart()
// Return: true on complete success; false on failure (see otaLastError()).
bool otaEnd(bool doReboot);

// Cancel ongoing OTA session (rollback & clear flags).
void otaAbort();

// Status & diagnostics
OtaStatus otaStatus();
const char* otaLastError();  // last error message (concise, for log/telemetry)

// (Optional) utility flush if transport has pacing limits
void otaYieldOnce();
