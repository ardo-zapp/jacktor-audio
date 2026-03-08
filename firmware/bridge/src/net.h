#pragma once
#include <Arduino.h>

void netInit();
void netTick(uint32_t now);
void netConnectToWifi(const String& ssid, const String& password);
void netSyncRTC();
bool netIsConnected();
String netGetIP();
