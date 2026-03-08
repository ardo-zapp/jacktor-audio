#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void commsInit();
void commsTick(uint32_t now);
void commsSendAmpCommand(JsonDocument &doc);
void commsSendAmpCommandRaw(const String &jsonStr);
extern String lastAmpTelemetry;
// Add declaration for displayBootLog wrapper inside comms/net so they dont directly include LVGL/display if not needed
// actually we already included display.h so it's fine
