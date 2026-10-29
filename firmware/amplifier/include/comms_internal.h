#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <cJSON.h>
#include "state.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

bool takeUartTxMutex(TickType_t xTicksToWait = pdMS_TO_TICKS(100));
void giveUartTxMutex();

class EspIdfUart {
private:
  uart_port_t _port;
public:
  EspIdfUart(uart_port_t port) : _port(port) {}
  void print(const char* s) {
    if (s && *s) uart_write_bytes(_port, s, strlen(s));
  }
  void print(const std::string& s) {
    if (!s.empty()) uart_write_bytes(_port, s.c_str(), s.length());
  }
  void println(const char* s) {
    if (s && *s) {
      size_t len = strlen(s);
      if (len < 510) {
        char stackBuf[512];
        memcpy(stackBuf, s, len);
        stackBuf[len] = '\n';
        stackBuf[len + 1] = '\0';
        uart_write_bytes(_port, stackBuf, len + 1);
      } else {
        char* buf = (char*)malloc(len + 2);
        if (buf) {
          memcpy(buf, s, len);
          buf[len] = '\n';
          buf[len + 1] = '\0';
          uart_write_bytes(_port, buf, len + 1);
          free(buf);
        } else {
          uart_write_bytes(_port, s, len);
          uart_write_bytes(_port, "\n", 1);
        }
      }
    } else {
      uart_write_bytes(_port, "\n", 1);
    }
  }
  void println(const std::string& s) {
    print(s + "\n");
  }
  void printf(const char* format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    print(buf);
  }
};

extern EspIdfUart linkSerial;
extern EspIdfUart debugSerial;
extern uint32_t lastRxBlink;
extern uint32_t lastTxBlink;
extern bool otaReady;
extern bool forceTel;

void ledRxPulse();
void sendTelemetry(cJSON *root);
void sendDebugLogJson(cJSON *root);
void sendDebugLog(const char *msg);

bool equalsIgnoreCase(const char *a, const char *b);
const char *fanModeToStr(FanMode m);
bool fanModeFromStr(const char *s, FanMode &out);

void sendAckOkBool(const char *key, bool val, bool tone);
void sendAckOkInt(const char *key, int val, bool tone);
void sendAckOkFloat(const char *key, float val, bool tone);
void sendAckOkStr(const char *key, const char *val, bool tone);

void sendAckErr(const char *key, const char *reason);
void sendOtaEvent(const char *evt);
void sendOtaEventFieldStr(const char *evt, const char *field, const char *value);
void sendOtaEventFieldBool(const char *evt, const char *field, bool value);

void sendOtaWriteOk(uint32_t seq);
void sendOtaWriteErr(uint32_t seq, const char *err);
void sendOtaError(const char *err);

void sendLogInfoOffset(int32_t offset);
void sendLogErrorReason(const char *msg, const char *reason);
