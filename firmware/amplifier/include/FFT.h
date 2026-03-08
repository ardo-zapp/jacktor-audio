#pragma once
#include <Arduino.h>
#include <cstring>

static constexpr uint8_t WS_BANDS_8  = 8;
static constexpr uint8_t WS_BANDS_16 = 16;
static constexpr uint8_t WS_BANDS_24 = 24;
static constexpr uint8_t WS_BANDS_32 = 32;
static constexpr uint8_t WS_BANDS_64 = 64;

static constexpr uint16_t BandCutoffTable8[WS_BANDS_8] = {
    100, 250, 500, 1000, 2000, 4000, 8000, 16000};

static constexpr uint16_t BandCutoffTable16[WS_BANDS_16] = {
    30, 50, 100, 150, 250, 400, 650, 1000, 1600, 2500, 4000, 6000, 12000, 14000, 16000, 17000};

// 24 bands - logarithmic spacing between 8 and 32
static constexpr uint16_t BandCutoffTable24[WS_BANDS_24] = {
    30, 45, 65, 90, 120, 160, 210, 280, 370, 490, 650, 860,
    1140, 1500, 2000, 2650, 3500, 4650, 6150, 8150, 10800, 14300, 16000, 17500};

static constexpr uint16_t BandCutoffTable32[WS_BANDS_32] = {
    45, 90, 130, 180, 220, 260, 310, 350, 390, 440, 480, 525, 650, 825, 1000, 1300,
    1600, 2050, 2500, 3000, 4000, 5125, 6250, 9125, 12000, 13000, 14000, 15000, 16000, 16500, 17000, 17500};

static constexpr uint16_t BandCutoffTable64[WS_BANDS_64] = {
    45, 90, 130, 180, 220, 260, 310, 350, 390, 440, 480, 525, 565, 610, 650, 690,
    735, 780, 820, 875, 920, 950, 1000, 1050, 1080, 1120, 1170, 1210, 1250, 1300, 1340, 1380,
    1430, 1470, 1510, 1560, 1616, 1767, 1932, 2113, 2310, 2526, 2762, 3019, 3301, 3610, 3947, 4315,
    4718, 5159, 5640, 6167, 6743, 7372, 8061, 8813, 9636, 10536, 11520, 12595, 13771, 15057, 16463, 18000};

static uint16_t gBandCutoff[WS_BANDS_64];
static uint8_t gBandCount = WS_BANDS_16;

static inline void WsSetNumberOfBands(uint8_t bands) {
  const uint16_t *src = nullptr;
  uint8_t len = 0;
  switch (bands) {
    case WS_BANDS_8:
      src = BandCutoffTable8;
      len = WS_BANDS_8;
      break;
    case WS_BANDS_24:
      src = BandCutoffTable24;
      len = WS_BANDS_24;
      break;
    case WS_BANDS_32:
      src = BandCutoffTable32;
      len = WS_BANDS_32;
      break;
    case WS_BANDS_64:
      src = BandCutoffTable64;
      len = WS_BANDS_64;
      break;
    case WS_BANDS_16:
    default:
      src = BandCutoffTable16;
      len = WS_BANDS_16;
      break;
  }
  std::memcpy(gBandCutoff, src, len * sizeof(uint16_t));
  gBandCount = len;
}

static inline uint8_t WsGetBandsLen() { return gBandCount; }

static inline uint16_t WsGetCutoff(uint8_t idx) {
  if (idx >= gBandCount) {
    return gBandCutoff[gBandCount - 1];
  }
  return gBandCutoff[idx];
}

static inline uint32_t WsBucketFrequency(uint16_t bucket, uint32_t samplingFrequency, uint16_t fftSize) {
  if (bucket <= 1) {
    return 0;
  }
  const uint32_t offset = static_cast<uint32_t>(bucket - 2);
  const uint32_t halfFs = samplingFrequency / 2U;
  const uint32_t halfFft = fftSize / 2U;
  return (offset * halfFs) / halfFft;
}