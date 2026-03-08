#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void commsInit();
void commsTick(uint32_t now);
void commsSendAmpCommand(JsonDocument &doc);
void commsSendAmpCommandRaw(const String &jsonStr);
extern String lastAmpTelemetry;
