#pragma once
#include <cstdint>
#include <cstddef>

void analyzerLoadFromNvs();
void analyzerSaveToNvs();

void analyzerInit();
void analyzerStartCore0();
void analyzerStop();

void analyzerSetMode(const char *mode);     // "off" | "vu" | "fft"
void analyzerSetBands(uint8_t bands);        // 8 | 16 | 32 | 64
void analyzerSetUpdateMs(uint16_t ms);       // 16..100 (clamped)
void analyzerSetEnabled(bool enabled);
void analyzerCalibrateAdc();

void analyzerSetGainL(float gain);
void analyzerSetGainR(float gain);
void analyzerSetNoiseL(float noise);
void analyzerSetNoiseR(float noise);
float analyzerGetGainL();
float analyzerGetGainR();
float analyzerGetNoiseL();
float analyzerGetNoiseR();
void analyzerResetGainNoise();
void analyzerSetTestChannel(uint8_t chMode); // 0=stereo, 1=left, 2=right
uint8_t analyzerGetTestChannel();

uint8_t analyzerGetBandsLen();
const uint8_t *analyzerGetBands();
const uint8_t *analyzerGetBandsL();
const uint8_t *analyzerGetBandsR();
uint8_t analyzerGetVu();
uint8_t analyzerGetVuL();
uint8_t analyzerGetVuR();
void    analyzerGetVu(uint8_t& outVuL, uint8_t& outVuR);
bool    analyzerIsClipping();
uint32_t analyzerGetClippingCount();
void    analyzerResetClippingCount();
const char *analyzerGetMode();
uint16_t analyzerGetUpdateMs();
bool analyzerEnabled();

float    analyzerGetDiagMaxMagL();
float    analyzerGetDiagMaxMagR();
uint16_t analyzerGetDiagMaxBucketL();
uint16_t analyzerGetDiagMaxBucketR();
bool     analyzerGetGateOpenL();
bool     analyzerGetGateOpenR();
float    analyzerGetDiagThrL();
float    analyzerGetDiagThrR();
float    analyzerGetDcOffsetL();
float    analyzerGetDcOffsetR();
uint32_t analyzerGetSamplesR();
uint32_t analyzerGetSamplesL();
int16_t  analyzerGetRawR0();
int16_t  analyzerGetRawL0();
