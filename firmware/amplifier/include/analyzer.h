#pragma once
#include <Arduino.h>

void analyzerLoadFromNvs();
void analyzerSaveToNvs();

void analyzerInit();
void analyzerStartCore0();
void analyzerStop();

void analyzerSetMode(const char *mode);     // "off" | "vu" | "fft"
void analyzerSetBands(uint8_t bands);        // 8 | 16 | 32 | 64
void analyzerSetUpdateMs(uint16_t ms);       // 16..100 (clamped)
void analyzerSetEnabled(bool enabled);

uint8_t analyzerGetBandsLen();
const uint8_t *analyzerGetBands();
uint8_t analyzerGetVu();
const char *analyzerGetMode();
uint16_t analyzerGetUpdateMs();
bool analyzerEnabled();
