#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

void displayInit();
void displayTick(uint32_t now);
void displayUpdateTelemetry(const JsonDocument& doc);
void displayBootLog(const char* msg);
void displaySetBacklight(bool on);
void displayStartUI();
