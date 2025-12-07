#pragma once
#include <Arduino.h>

void uiInit();
void uiTick(uint32_t now);
void uiShowBoot(uint32_t holdMs);
void uiShowFactoryReset(const char* subtitle, uint32_t holdMs);
void uiBootLogLine(const char* label, bool ok);
void uiShowError(const char* msg);
void uiShowWarning(const char* msg);
void uiClearErrorToRun();
void uiShowStandby();
void uiForceStandby();
void uiTransitionToRun();
void uiSetClock(const char* hhmmss);
void uiSetDate(const char* yyyymmdd);
void uiSetInputStatus(bool bt, bool speakerBig);
bool uiIsErrorActive();