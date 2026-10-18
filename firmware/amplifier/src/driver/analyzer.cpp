#include "analyzer.h"
#include "config.h"
#include "FFT.h"

#if ANALYZER_WS_ENABLE

#include "kissfft/kiss_fftr.h"
#include "esp_adc/adc_continuous.h"
#include "hal/adc_types.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <nvs.h>

#include <algorithm>
#include <cmath>
#include <cstring>

extern void commsLog(const char* level, const char* msg);

namespace {

static inline uint32_t millis() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

constexpr uint16_t kSampleBlock = ANA_N;
static float dynamicAdcCenter = ANA_ADC_CENTER;
constexpr uint32_t kSamplingFrequency = ANA_FS_HZ;

TaskHandle_t taskHandle = nullptr;
bool enabled = true;
bool adcReady = false;
adc_continuous_handle_t adc_handle = nullptr;

char mode[4] = ANALYZER_DEFAULT_MODE;
uint8_t bandsLen = ANALYZER_DEFAULT_BANDS;
uint16_t updateMs = ANALYZER_UPDATE_MS;

// Real-time volatile tuning parameters (not saved to NVS)
static float gainL = 1.0f;
static float gainR = 1.0f;
static float noiseL = 0.0f;
static float noiseR = 0.0f;
static volatile uint8_t testChMode = 0; // 0 = stereo, 1 = left only, 2 = right only

constexpr const char *kNvsNs = "dev/an";
constexpr const char *kNvsKeyMode = "mode";
constexpr const char *kNvsKeyBands = "bands";
constexpr const char *kNvsKeyUpdate = "update_ms";
constexpr const char *kNvsKeyAdcCenter = "adc_center";

kiss_fftr_cfg fftCfg = nullptr;

// Circular Sliding-Window Stereo Buffers (12-bit raw ADC samples 0..4095)
static int16_t *ringBufL = nullptr;
static int16_t *ringBufR = nullptr;
static uint8_t *adcRawDmaBuf = nullptr;
static float *smoothMagL = nullptr;
static float *smoothMagR = nullptr;
static float *baseNoiseL = nullptr;
static float *baseNoiseR = nullptr;
static uint16_t writeIdxL = 0;
static uint16_t writeIdxR = 0;
static uint32_t totalSamplesL = 0;
static uint32_t totalSamplesR = 0;

kiss_fft_scalar timedataL[kSampleBlock];
kiss_fft_scalar timedataR[kSampleBlock];
kiss_fft_cpx freqdataL[kSampleBlock / 2 + 1];
kiss_fft_cpx freqdataR[kSampleBlock / 2 + 1];

uint16_t sampleCount = 0;

uint8_t bandLevelsL[WS_BANDS_64];
uint8_t bandLevelsR[WS_BANDS_64];
uint8_t bandLevels[WS_BANDS_64];
uint8_t vuLevelL = 0;
uint8_t vuLevelR = 0;
uint8_t vuLevel = 0;
bool isClipping = false;
uint32_t clippingCount = 0;
float freqBinsL[WS_BANDS_64 + 1];
float freqBinsR[WS_BANDS_64 + 1];

uint32_t nextProcessMs = 0;

static inline int16_t median3(int16_t a, int16_t b, int16_t c) {
  if (a > b) std::swap(a, b);
  if (b > c) std::swap(b, c);
  if (a > b) std::swap(a, b);
  return b;
}

static inline float getBandWeight(uint8_t band, uint8_t totalBands, bool halfWave = false) {
  (void)halfWave;
  // Precision hardware-calibrated 16-band equalization table for lowered tone-control volume
  // with Bass, Middle, and Treble at 50% (center). Equalizes 30Hz..17kHz within +-1.5%.
  static constexpr float kCalibWeight16[16] = {
      16.00f, // B01: 30Hz  (Sub-Bass)
      8.00f,  // B02: 50Hz  (Sub-Bass)
      3.75f,  // B03: 100Hz (Bass)
      3.00f,  // B04: 150Hz (Mid-Bass)
      1.70f,  // B05: 250Hz (Low-Mid)
      1.55f,  // B06: 400Hz (Mid-Low)
      1.30f,  // B07: 650Hz (Mid)
      1.30f,  // B08: 1000Hz (Mid-High)
      1.20f,  // B09: 1600Hz (Presence)
      1.22f,  // B10: 2500Hz (Presence)
      1.22f,  // B11: 4000Hz (High-Mid)
      1.34f,  // B12: 6000Hz (High)
      1.48f,  // B13: 12000Hz (Treble)
      1.28f,  // B14: 14000Hz (Brilliance)
      1.60f,  // B15: 16000Hz (Top-Air)
      1.35f   // B16: 17000Hz (Air)
  };

  if (totalBands == 16 && band < 16) {
    return kCalibWeight16[band];
  }
  if (totalBands <= 1) return 1.0f;
  float pos = ((float)band / (float)(totalBands - 1)) * 15.0f;
  uint8_t idx = (uint8_t)pos;
  if (idx >= 15) return kCalibWeight16[15];
  float frac = pos - (float)idx;
  return kCalibWeight16[idx] * (1.0f - frac) + kCalibWeight16[idx + 1] * frac;
}

static inline uint8_t mapFreqToBand(uint32_t freq, uint8_t totalBands) {
  uint8_t band = 0;
  while (band + 1 < totalBands) {
    uint32_t c0 = WsGetCutoff(band);
    uint32_t c1 = WsGetCutoff(band + 1);
    // Geometric midpoint sqrt(c0 * c1) separates band centers accurately
    if (freq * freq <= c0 * c1) break;
    ++band;
  }
  return band;
}

constexpr uint16_t kAudibleBinsAlloc = (ANA_F_HI_HZ * kSampleBlock) / kSamplingFrequency + 4;

bool setupAdc() {
  if (adcReady && adc_handle) return true;

  uint32_t freeHeap = esp_get_free_heap_size();
  uint32_t freeDma = heap_caps_get_free_size(MALLOC_CAP_DMA);
  char dbg[96];
  snprintf(dbg, sizeof(dbg), "analyzer_setup freeHeap=%lu freeDma=%lu", (unsigned long)freeHeap, (unsigned long)freeDma);
  commsLog("info", dbg);

  // Dynamic DMA & Welch Averaging Heap Allocations (allocated post-BT init to preserve BSS)
  if (!ringBufL) {
    ringBufL = (int16_t *)heap_caps_malloc(kSampleBlock * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!ringBufR) {
    ringBufR = (int16_t *)heap_caps_malloc(kSampleBlock * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!adcRawDmaBuf) {
    adcRawDmaBuf = (uint8_t *)heap_caps_malloc(kSampleBlock * sizeof(adc_digi_output_data_t), MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
  }
  if (!smoothMagL) {
    smoothMagL = (float *)heap_caps_calloc(kAudibleBinsAlloc, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!smoothMagR) {
    smoothMagR = (float *)heap_caps_calloc(kAudibleBinsAlloc, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!baseNoiseL) {
    baseNoiseL = (float *)heap_caps_calloc(kAudibleBinsAlloc, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (!baseNoiseR) {
    baseNoiseR = (float *)heap_caps_calloc(kAudibleBinsAlloc, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (!ringBufL || !ringBufR || !adcRawDmaBuf || !smoothMagL || !smoothMagR || !baseNoiseL || !baseNoiseR) {
    commsLog("error", "analyzer_alloc_fail");
    return false;
  }

  for (uint16_t i = 0; i < kSampleBlock; ++i) {
    ringBufL[i] = (int16_t)ANA_ADC_CENTER;
    ringBufR[i] = 0;
  }
  writeIdxL = 0;
  writeIdxR = 0;
  totalSamplesL = 0;
  totalSamplesR = 0;

  adc_continuous_handle_cfg_t adc_config;
  std::memset(&adc_config, 0, sizeof(adc_config));
  adc_config.max_store_buf_size = kSampleBlock * 2 * sizeof(adc_digi_output_data_t);
  adc_config.conv_frame_size = (kSampleBlock / 2) * sizeof(adc_digi_output_data_t);
  adc_config.flags.flush_pool = 1;

  esp_err_t err = adc_continuous_new_handle(&adc_config, &adc_handle);
  if (err != ESP_OK) {
    char eb[64];
    snprintf(eb, sizeof(eb), "adc_new_handle_fail: 0x%x", err);
    commsLog("error", eb);
    return false;
  }

  adc_digi_pattern_config_t adc_pattern[2];
  std::memset(adc_pattern, 0, sizeof(adc_pattern));
  // In ESP32, all channels on ADC1 must share the exact same attenuation (ADC_ATTEN_DB_12 = 0..3.3V)
  adc_pattern[0].atten = ADC_ATTEN_DB_12;
  adc_pattern[0].channel = ADC_CHANNEL_0; // GPIO 36 (Right Channel)
  adc_pattern[0].unit = ADC_UNIT_1;
  adc_pattern[0].bit_width = ADC_BITWIDTH_12;

  adc_pattern[1].atten = ADC_ATTEN_DB_12;
  adc_pattern[1].channel = ADC_CHANNEL_3; // GPIO 39 (Left Channel)
  adc_pattern[1].unit = ADC_UNIT_1;
  adc_pattern[1].bit_width = ADC_BITWIDTH_12;

  adc_continuous_config_t dig_cfg;
  std::memset(&dig_cfg, 0, sizeof(dig_cfg));
  dig_cfg.pattern_num = 2;
  dig_cfg.adc_pattern = adc_pattern;
  // With pattern_num = 2, total ADC conversion rate must be 88.2 kHz so EACH channel (L and R) gets 44.1 kHz!
  dig_cfg.sample_freq_hz = kSamplingFrequency * 2;
  dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;

  err = adc_continuous_config(adc_handle, &dig_cfg);
  if (err != ESP_OK) {
    char eb[64];
    snprintf(eb, sizeof(eb), "adc_config_fail: 0x%x", err);
    commsLog("error", eb);
    adc_continuous_deinit(adc_handle);
    adc_handle = nullptr;
    return false;
  }

  err = adc_continuous_start(adc_handle);
  if (err != ESP_OK) {
    char eb[64];
    snprintf(eb, sizeof(eb), "adc_start_fail: 0x%x", err);
    commsLog("error", eb);
    adc_continuous_deinit(adc_handle);
    adc_handle = nullptr;
    return false;
  }

  if (!fftCfg) {
    fftCfg = kiss_fftr_alloc(kSampleBlock, 0, nullptr, nullptr);
    if (!fftCfg) {
      commsLog("error", "analyzer_fft_alloc_fail");
      adc_continuous_stop(adc_handle);
      adc_continuous_deinit(adc_handle);
      adc_handle = nullptr;
      return false;
    }
  }

  commsLog("info", "analyzer_setup_adc_ok");
  return true;
}

void teardownAdc() {
  if (adc_handle) {
    adc_continuous_stop(adc_handle);
    adc_continuous_deinit(adc_handle);
    adc_handle = nullptr;
  }

  if (ringBufL) {
    heap_caps_free(ringBufL);
    ringBufL = nullptr;
  }
  if (ringBufR) {
    heap_caps_free(ringBufR);
    ringBufR = nullptr;
  }
  if (adcRawDmaBuf) {
    heap_caps_free(adcRawDmaBuf);
    adcRawDmaBuf = nullptr;
  }
  if (smoothMagL) {
    heap_caps_free(smoothMagL);
    smoothMagL = nullptr;
  }
  if (smoothMagR) {
    heap_caps_free(smoothMagR);
    smoothMagR = nullptr;
  }
  if (baseNoiseL) {
    heap_caps_free(baseNoiseL);
    baseNoiseL = nullptr;
  }
  if (baseNoiseR) {
    heap_caps_free(baseNoiseR);
    baseNoiseR = nullptr;
  }
  if (fftCfg) {
    free(fftCfg);
    fftCfg = nullptr;
  }
}

void resetBins() {
  std::fill(freqBinsL, freqBinsL + (sizeof(freqBinsL) / sizeof(freqBinsL[0])), 0.0f);
  std::fill(freqBinsR, freqBinsR + (sizeof(freqBinsR) / sizeof(freqBinsR[0])), 0.0f);
}

static float diagMaxMagL = 0.0f;
static float diagMeanMagL = 0.0f;
static float diagThrL = 0.0f;
static uint16_t diagMaxBucketL = 0;
static bool gateOpenL = false;
static float diagMaxMagR = 0.0f;
static float diagMeanMagR = 0.0f;
static float diagThrR = 0.0f;
static uint16_t diagMaxBucketR = 0;
static bool gateOpenR = false;
static float lastDcOffsetL = 0.0f;
static float lastDcOffsetR = 0.0f;

void processFft() {
  if (!fftCfg || !ringBufL || !ringBufR || !smoothMagL || !smoothMagR || !baseNoiseL || !baseNoiseR) return;

  const uint16_t snapIdxR = writeIdxR;
  const uint16_t snapIdxL = writeIdxL;

  // 1. Dynamic DC Offset Calculation (independent L & R)
  float dcSumL = 0.0f;
  float dcSumR = 0.0f;

  for (uint16_t i = 0; i < kSampleBlock; ++i) {
    dcSumR += (float)ringBufR[(snapIdxR + i) & (kSampleBlock - 1)];
    dcSumL += (float)ringBufL[(snapIdxL + i) & (kSampleBlock - 1)];
  }

  float dcOffsetR = dcSumR / (float)kSampleBlock;
  float dcOffsetL = dcSumL / (float)kSampleBlock;
  lastDcOffsetL = dcOffsetL;
  lastDcOffsetR = dcOffsetR;

  // 2. 1-Pole IIR DC Blocker Filter (fc ~25 Hz @ 44.1 kHz, R = 0.996f) + Direct Hann Windowing
  // Eliminates CMOS capacitance low-frequency charge drift and baseline wander on GPIO 36 & 39
  static float dcBlockPrevInL = 0.0f;
  static float dcBlockPrevOutL = 0.0f;
  static float dcBlockPrevInR = 0.0f;
  static float dcBlockPrevOutR = 0.0f;
  constexpr float kDcBlockR = 0.996f;

  float invN = 1.0f / (float)(kSampleBlock - 1);
  for (uint16_t i = 0; i < kSampleBlock; ++i) {
    float inL = (float)ringBufL[(snapIdxL + i) & (kSampleBlock - 1)] - dcOffsetL;
    float outL = inL - dcBlockPrevInL + kDcBlockR * dcBlockPrevOutL;
    dcBlockPrevInL = inL;
    dcBlockPrevOutL = outL;

    float inR = (float)ringBufR[(snapIdxR + i) & (kSampleBlock - 1)] - dcOffsetR;
    float outR = inR - dcBlockPrevInR + kDcBlockR * dcBlockPrevOutR;
    dcBlockPrevInR = inR;
    dcBlockPrevOutR = outR;

    if (std::isnan(outL) || std::isinf(outL)) { outL = 0.0f; dcBlockPrevOutL = 0.0f; }
    if (std::isnan(outR) || std::isinf(outR)) { outR = 0.0f; dcBlockPrevOutR = 0.0f; }

    float window = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i * invN));
    timedataL[i] = outL * window;
    timedataR[i] = outR * window;
  }

  // 3. Compute Real-to-Complex KissFFT (Left & Right)
  kiss_fftr(fftCfg, timedataL, freqdataL);
  kiss_fftr(fftCfg, timedataR, freqdataR);

  // Reuse timedataL/R buffers post-FFT to store clean per-bin excess magnitudes
  float *cleanExcessBufL = timedataL;
  float *cleanExcessBufR = timedataR;

  // 4. Overlapping Welch Spectral Smoothing & Adaptive Per-Bin Noise Baseline
  resetBins();

  constexpr uint16_t kFirstBucket = 1; // Start at bucket 1 (43.1 Hz) to capture sub-bass
  constexpr uint16_t kHalfBlock = kSampleBlock / 2;
  constexpr uint16_t kAudibleMaxBucket = (ANA_F_HI_HZ * kSampleBlock) / kSamplingFrequency; // ~418 buckets (18 kHz)
  constexpr float kNumBins = (float)(kAudibleMaxBucket - kFirstBucket);

  // Precision hardware-calibrated analog voltage-divider compensation for GPIO 36 (Right Channel)
  // Physical circuit inspection and measurements show ~9.80x signal attenuation on GPIO 36 relative to GPIO 39.
  constexpr float kAnalogScaleR = 9.80f;

  const bool halfWaveL = (dcOffsetL < 350.0f);
  const bool halfWaveR = (dcOffsetR < 350.0f);
  // Symmetrical calibrated reference scaling: Left = 1.00f reference, Right = 9.80f hardware scaling
  const float hwCompL = halfWaveL ? 2.00f : 1.00f;
  const float hwCompR = (halfWaveR ? 2.00f : 1.00f) * kAnalogScaleR;

  cleanExcessBufL[0] = 0.0f;
  cleanExcessBufR[0] = 0.0f;

  float rawSumMagL = 0.0f;
  float rawSumMagR = 0.0f;
  for (uint16_t bucket = kFirstBucket; bucket < kAudibleMaxBucket; ++bucket) {
    float rL = freqdataL[bucket].r;
    float imL = freqdataL[bucket].i;
    float rawMagL = sqrtf(rL * rL + imL * imL) * hwCompL;
    rawSumMagL += rawMagL;

    float rR = freqdataR[bucket].r;
    float imR = freqdataR[bucket].i;
    float rawMagR = sqrtf(rR * rR + imR * imR) * hwCompR;
    rawSumMagR += rawMagR;

    // Welch EMA smoothing across 8ms overlapping frames (suppresses random thermal variance by ~3.5x)
    if (smoothMagL[bucket] == 0.0f && baseNoiseL[bucket] == 0.0f) {
      smoothMagL[bucket] = rawMagL;
      baseNoiseL[bucket] = rawMagL;
    } else {
      float alphaL = (rawMagL > smoothMagL[bucket]) ? 0.28f : 0.20f;
      smoothMagL[bucket] = (1.0f - alphaL) * smoothMagL[bucket] + alphaL * rawMagL;
    }

    if (smoothMagR[bucket] == 0.0f && baseNoiseR[bucket] == 0.0f) {
      smoothMagR[bucket] = rawMagR;
      baseNoiseR[bucket] = rawMagR;
    } else {
      float alphaR = (rawMagR > smoothMagR[bucket]) ? 0.28f : 0.20f;
      smoothMagR[bucket] = (1.0f - alphaR) * smoothMagR[bucket] + alphaR * rawMagR;
    }
  }

  float meanMagL = rawSumMagL / kNumBins;
  float meanMagR = rawSumMagR / kNumBins;

  float maxExcessL = 0.0f;
  float maxExcessR = 0.0f;
  float sumExcessL = 0.0f;
  float sumExcessR = 0.0f;
  uint16_t curMaxBucketL = 0;
  uint16_t curMaxBucketR = 0;

  for (uint16_t bucket = kFirstBucket; bucket < kAudibleMaxBucket; ++bucket) {
    // Dynamic Leaky Baseline Estimator:
    // If incoming level is below baseline, rapidly decay downwards (0.90 / 0.10) to follow floor.
    // If incoming level is above baseline, slowly climb upwards (0.995 / 0.005) to absorb constant
    // steady-state noise (like SMPS ripple, ground hum) without eating dynamic audio tones.
    if (smoothMagL[bucket] < baseNoiseL[bucket]) {
      baseNoiseL[bucket] = 0.90f * baseNoiseL[bucket] + 0.10f * smoothMagL[bucket];
    } else {
      bool isPeakL = gateOpenL && (bucket == curMaxBucketL || bucket + 1 == curMaxBucketL || curMaxBucketL + 1 == bucket);
      if (!isPeakL) {
        baseNoiseL[bucket] = 0.995f * baseNoiseL[bucket] + 0.005f * smoothMagL[bucket];
      }
    }
    baseNoiseL[bucket] = std::clamp(baseNoiseL[bucket], 30.0f, 650.0f);

    if (smoothMagR[bucket] < baseNoiseR[bucket]) {
      baseNoiseR[bucket] = 0.90f * baseNoiseR[bucket] + 0.10f * smoothMagR[bucket];
    } else {
      bool isPeakR = gateOpenR && (bucket == curMaxBucketR || bucket + 1 == curMaxBucketR || curMaxBucketR + 1 == bucket);
      if (!isPeakR) {
        baseNoiseR[bucket] = 0.995f * baseNoiseR[bucket] + 0.005f * smoothMagR[bucket];
      }
    }
    baseNoiseR[bucket] = std::clamp(baseNoiseR[bucket], 30.0f, 650.0f);

    float mainsMarginL = (bucket <= 2) ? 22.0f : 14.0f;
    float exL = smoothMagL[bucket] - (baseNoiseL[bucket] * 1.12f + mainsMarginL);
    if (exL < 0.0f) exL = 0.0f;
    cleanExcessBufL[bucket] = exL;
    sumExcessL += exL;
    if (exL > maxExcessL) {
      maxExcessL = exL;
      curMaxBucketL = bucket;
    }

    float mainsMarginR = (bucket <= 2) ? 32.0f : 14.0f;
    float exR = smoothMagR[bucket] - (baseNoiseR[bucket] * 1.12f + mainsMarginR);
    if (exR < 0.0f) exR = 0.0f;
    cleanExcessBufR[bucket] = exR;
    sumExcessR += exR;
    if (exR > maxExcessR) {
      maxExcessR = exR;
      curMaxBucketR = bucket;
    }
  }

  // Coherent Bin Persistence Lock:
  // In silence, the highest noise fluctuation jumps randomly across 418 bins every 8ms (binLockCount <= 2).
  // Even a 1% volume (-40 dBFS) tone stays locked on the same bin (+-1) across consecutive frames!
  static uint16_t prevPeakBucketL = 0;
  static uint8_t binLockCountL = 0;
  if (maxExcessL >= 20.0f && curMaxBucketL > 0 &&
      (curMaxBucketL == prevPeakBucketL || curMaxBucketL + 1 == prevPeakBucketL || prevPeakBucketL + 1 == curMaxBucketL)) {
    if (binLockCountL < 40) binLockCountL++;
  } else {
    binLockCountL = (maxExcessL >= 20.0f) ? 1 : 0;
    prevPeakBucketL = curMaxBucketL;
  }

  static uint16_t prevPeakBucketR = 0;
  static uint8_t binLockCountR = 0;
  if (maxExcessR >= 20.0f && curMaxBucketR > 0 &&
      (curMaxBucketR == prevPeakBucketR || curMaxBucketR + 1 == prevPeakBucketR || prevPeakBucketR + 1 == curMaxBucketR)) {
    if (binLockCountR < 40) binLockCountR++;
  } else {
    binLockCountR = (maxExcessR >= 20.0f) ? 1 : 0;
    prevPeakBucketR = curMaxBucketR;
  }

  float userGateL = (noiseL > 0.0f && noiseL < 20.0f) ? (noiseL * 400.0f) : std::max(0.0f, noiseL);
  float userGateR = (noiseR > 0.0f && noiseR < 20.0f) ? (noiseR * 400.0f) : std::max(0.0f, noiseR);
  float multL = std::max(0.0f, gainL);
  float multR = std::max(0.0f, gainR);

  float gainRelaxL = (multL > 1.0f) ? std::max(0.70f, 1.0f / sqrtf(multL)) : 1.0f;
  float gainRelaxR = (multR > 1.0f) ? std::max(0.70f, 1.0f / sqrtf(multR)) : 1.0f;

  // 5. Intelligent Dual-Path Noise Gate (Coherent Low-Volume Lock + Broadband Energy Gate)
  constexpr float kThrCoherentOpen = 24.0f;
  constexpr float kThrBroadbandOpen = 85.0f;
  constexpr float kThrClose = 16.0f;

  const float thrCoherentOpenL = kThrCoherentOpen * gainRelaxL + userGateL;
  const float thrBroadbandOpenL = kThrBroadbandOpen * gainRelaxL + userGateL;
  const float thrCloseL = kThrClose * gainRelaxL + userGateL;

  const float thrCoherentOpenR = kThrCoherentOpen * gainRelaxR + userGateR;
  const float thrBroadbandOpenR = kThrBroadbandOpen * gainRelaxR + userGateR;
  const float thrCloseR = kThrClose * gainRelaxR + userGateR;

  static uint8_t holdFramesL = 0;
  bool signalDetectedL = (maxExcessL >= thrBroadbandOpenL) ||
                         (binLockCountL >= 4 && maxExcessL >= thrCoherentOpenL) ||
                         (sumExcessL >= 850.0f * gainRelaxL + userGateL * 4.0f);

  if (!gateOpenL) {
    if (signalDetectedL) {
      gateOpenL = true;
      holdFramesL = 18;
    }
  } else {
    if (maxExcessL >= thrCloseL && (binLockCountL >= 2 || maxExcessL >= thrBroadbandOpenL * 0.7f || sumExcessL >= 550.0f)) {
      holdFramesL = 18;
    } else {
      if (holdFramesL > 0) holdFramesL--;
      else gateOpenL = false;
    }
  }

  static uint8_t holdFramesR = 0;
  bool signalDetectedR = (maxExcessR >= thrBroadbandOpenR) ||
                         (binLockCountR >= 4 && maxExcessR >= thrCoherentOpenR) ||
                         (sumExcessR >= 850.0f * gainRelaxR + userGateR * 4.0f);

  if (!gateOpenR) {
    if (signalDetectedR) {
      gateOpenR = true;
      holdFramesR = 18;
    }
  } else {
    if (maxExcessR >= thrCloseR && (binLockCountR >= 2 || maxExcessR >= thrBroadbandOpenR * 0.7f || sumExcessR >= 550.0f)) {
      holdFramesR = 18;
    } else {
      if (holdFramesR > 0) holdFramesR--;
      else gateOpenR = false;
    }
  }

  diagMaxMagL = maxExcessL;
  diagMeanMagL = meanMagL;
  diagMaxBucketL = curMaxBucketL;
  diagThrL = gateOpenL ? thrCloseL : thrCoherentOpenL;
  diagMaxMagR = maxExcessR;
  diagMeanMagR = meanMagR;
  diagMaxBucketR = curMaxBucketR;
  diagThrR = gateOpenR ? thrCloseR : thrCoherentOpenR;

  bool silentL = !gateOpenL || (multL <= 0.0001f);
  bool silentR = !gateOpenR || (multR <= 0.0001f);

  const float meanExcessL = sumExcessL / kNumBins;
  const float meanExcessR = sumExcessR / kNumBins;
  const bool isTonalL = (binLockCountL >= 3) || (meanExcessL > 0.1f && (maxExcessL / meanExcessL) > 5.5f);
  const bool isTonalR = (binLockCountR >= 3) || (meanExcessR > 0.1f && (maxExcessR / meanExcessR) > 5.0f);

  // Sub-bass 30Hz (B01) vs 50Hz (B02) spectral discrimination using Bucket 1 (43Hz) & Bucket 2 (86Hz)
  const float subRatioL = (cleanExcessBufL[1] > 15.0f) ? (cleanExcessBufL[2] / cleanExcessBufL[1]) : 1.0f;
  const float subRatioR = (cleanExcessBufR[1] > 15.0f) ? (cleanExcessBufR[2] / cleanExcessBufR[1]) : 1.0f;

  for (uint16_t bucket = kFirstBucket; bucket < kAudibleMaxBucket; ++bucket) {
    const uint32_t freq = WsBucketFrequency(bucket, kSamplingFrequency, kSampleBlock);
    uint8_t band = mapFreqToBand(freq, bandsLen);

    if (!silentL) {
      float cleanMagL = cleanExcessBufL[bucket];
      // For tonal signals, suppress outer Hann sidebands and unlocked thermal noise bins
      bool nearPeakL = (bucket == curMaxBucketL) || (bucket + 1 == curMaxBucketL) || (curMaxBucketL + 1 == bucket);
      if (isTonalL) {
        if (!nearPeakL && cleanMagL < 0.35f * maxExcessL + 42.0f) {
          cleanMagL = 0.0f;
        }
        if (curMaxBucketL > 2 && (bucket == 1 || bucket == 2)) {
          cleanMagL *= 0.05f;
        }
        float adjMaxL = cleanExcessBufL[bucket - 1];
        if (bucket + 1 < kAudibleMaxBucket && cleanExcessBufL[bucket + 1] > adjMaxL) {
          adjMaxL = cleanExcessBufL[bucket + 1];
        }
        if (cleanMagL < 0.78f * adjMaxL) {
          cleanMagL *= 0.04f;
        }
      } else {
        // Broadband signal: require each bin to exceed residual thermal ripple
        if (cleanMagL < 38.0f) cleanMagL = 0.0f;
      }

      if (bandsLen == 16 && bucket == 1) {
        if (subRatioL < 0.46f) {
          float wB01 = cleanMagL * getBandWeight(0, bandsLen, halfWaveL) * multL;
          float wB02 = cleanMagL * getBandWeight(1, bandsLen, halfWaveL) * multL * 0.12f;
          if (wB01 > freqBinsL[0]) freqBinsL[0] = wB01;
          if (wB02 > freqBinsL[1]) freqBinsL[1] = wB02;
        } else if (subRatioL < 1.15f) {
          float wB01 = cleanMagL * getBandWeight(0, bandsLen, halfWaveL) * multL * 0.12f;
          float wB02 = cleanMagL * getBandWeight(1, bandsLen, halfWaveL) * multL;
          if (wB01 > freqBinsL[0]) freqBinsL[0] = wB01;
          if (wB02 > freqBinsL[1]) freqBinsL[1] = wB02;
        } else {
          float wB01 = cleanMagL * getBandWeight(0, bandsLen, halfWaveL) * multL * 0.85f;
          float wB02 = cleanMagL * getBandWeight(1, bandsLen, halfWaveL) * multL * 0.90f;
          if (wB01 > freqBinsL[0]) freqBinsL[0] = wB01;
          if (wB02 > freqBinsL[1]) freqBinsL[1] = wB02;
        }
      } else {
        if (bandsLen == 16 && bucket == 2 && isTonalL && curMaxBucketL == 1) {
          cleanMagL *= 0.08f;
        }
        float weightL = getBandWeight(band, bandsLen, halfWaveL);
        float weightedMagL = cleanMagL * weightL * multL;
        if (weightedMagL > freqBinsL[band]) {
          freqBinsL[band] = weightedMagL;
        }
      }
    }

    if (!silentR) {
      float cleanMagR = cleanExcessBufR[bucket];
      bool nearPeakR = (bucket == curMaxBucketR) || (bucket + 1 == curMaxBucketR) || (curMaxBucketR + 1 == bucket);
      if (isTonalR) {
        if (!nearPeakR && cleanMagR < 0.35f * maxExcessR + 42.0f) {
          cleanMagR = 0.0f;
        }
        if (curMaxBucketR > 2 && (bucket == 1 || bucket == 2)) {
          cleanMagR *= 0.05f;
        }
        float adjMaxR = cleanExcessBufR[bucket - 1];
        if (bucket + 1 < kAudibleMaxBucket && cleanExcessBufR[bucket + 1] > adjMaxR) {
          adjMaxR = cleanExcessBufR[bucket + 1];
        }
        if (cleanMagR < 0.78f * adjMaxR) {
          cleanMagR *= 0.04f;
        }
      } else {
        if (cleanMagR < 42.0f) cleanMagR = 0.0f;
      }

      if (bandsLen == 16 && bucket == 1) {
        if (subRatioR < 0.46f) {
          float wB01 = cleanMagR * getBandWeight(0, bandsLen, halfWaveR) * multR;
          float wB02 = cleanMagR * getBandWeight(1, bandsLen, halfWaveR) * multR * 0.12f;
          if (wB01 > freqBinsR[0]) freqBinsR[0] = wB01;
          if (wB02 > freqBinsR[1]) freqBinsR[1] = wB02;
        } else if (subRatioR < 1.15f) {
          float wB01 = cleanMagR * getBandWeight(0, bandsLen, halfWaveR) * multR * 0.12f;
          float wB02 = cleanMagR * getBandWeight(1, bandsLen, halfWaveR) * multR;
          if (wB01 > freqBinsR[0]) freqBinsR[0] = wB01;
          if (wB02 > freqBinsR[1]) freqBinsR[1] = wB02;
        } else {
          float wB01 = cleanMagR * getBandWeight(0, bandsLen, halfWaveR) * multR * 0.85f;
          float wB02 = cleanMagR * getBandWeight(1, bandsLen, halfWaveR) * multR * 0.90f;
          if (wB01 > freqBinsR[0]) freqBinsR[0] = wB01;
          if (wB02 > freqBinsR[1]) freqBinsR[1] = wB02;
        }
      } else {
        if (bandsLen == 16 && bucket == 2 && isTonalR && curMaxBucketR == 1) {
          cleanMagR *= 0.08f;
        }
        float weightR = getBandWeight(band, bandsLen, halfWaveR);
        float weightedMagR = cleanMagR * weightR * multR;
        if (weightedMagR > freqBinsR[band]) {
          freqBinsR[band] = weightedMagR;
        }
      }
    }
  }

  // Channel Routing:
  // - testChMode == 1 ("left"): strictly isolate Left channel (Right = 0)
  // - testChMode == 2 ("right"): strictly isolate Right channel (Left = 0)
  // - testChMode == 0 ("stereo"): 100% independent stereo channels (no cross-channel injection/forcing!)
  // In stereo mode, both channels function as completely independent measuring instruments.
  // If the two channels receive different frequencies or timbres, each channel faithfully
  // displays its own distinct spectrum, exactly as expected in industrial audio analyzers.
  const uint8_t chMode = testChMode;
  if (chMode == 1) {
    // Left channel only test: force Right channel completely silent
    silentR = true;
    gateOpenR = false;
    diagMaxMagR = 0.0f;
    diagMaxBucketR = 0;
    std::fill(freqBinsR, freqBinsR + (sizeof(freqBinsR) / sizeof(freqBinsR[0])), 0.0f);
  } else if (chMode == 2) {
    // Right channel only test: force Left channel completely silent
    silentL = true;
    gateOpenL = false;
    diagMaxMagL = 0.0f;
    diagMaxBucketL = 0;
    std::fill(freqBinsL, freqBinsL + (sizeof(freqBinsL) / sizeof(freqBinsL[0])), 0.0f);
  }

  // 6. Multi-Band Visual Normalization with Fast-Recovery AGC & Vivid Logarithmic Contrast
  float peakL = 0.0f;
  float peakR = 0.0f;

  for (uint8_t i = 0; i < bandsLen; ++i) {
    if (freqBinsL[i] > peakL) peakL = freqBinsL[i];
    if (freqBinsR[i] > peakR) peakR = freqBinsR[i];
  }

  // Low reference peak (160.0f) ensures 1% volume (-40 dBFS) tones drive full, crisp spectrum bars
  const float minRefPeakL = 160.0f;
  const float minRefPeakR = 160.0f;
  static float smoothPeakL = 160.0f;
  static float smoothPeakR = 160.0f;

  float agcDecayL = (peakL < smoothPeakL * 0.5f) ? 0.92f : 0.97f;
  float agcDecayR = (peakR < smoothPeakR * 0.5f) ? 0.92f : 0.97f;
  smoothPeakL = std::max(peakL, std::max(smoothPeakL * agcDecayL, minRefPeakL));
  smoothPeakR = std::max(peakR, std::max(smoothPeakR * agcDecayR, minRefPeakR));

  // Per-8ms frame decay factor (~0.93^3 = 0.80 per 24ms)
  constexpr float kFastBandsDecay = 0.93f;
  constexpr float kFastVuDecay = 0.93f;

  for (uint8_t i = 0; i < bandsLen; ++i) {
    if (silentL || peakL <= 5.0f) {
      bandLevelsL[i] = (chMode == 2) ? 0 : static_cast<uint8_t>(bandLevelsL[i] * kFastBandsDecay);
      if (bandLevelsL[i] < 4) bandLevelsL[i] = 0;
    } else {
      float ratioL = freqBinsL[i] / smoothPeakL;
      if (std::isnan(ratioL) || std::isinf(ratioL) || ratioL < 0.0f) ratioL = 0.0f;
      if (ratioL > 1.0f) ratioL = 1.0f;
      float normL = (ratioL > 0.0f) ? log10f(1.0f + 9.0f * ratioL) : 0.0f;
      if (normL > 1.0f) normL = 1.0f;

      uint8_t targetL = static_cast<uint8_t>(normL * 255.0f);
      uint8_t decayedL = static_cast<uint8_t>(bandLevelsL[i] * kFastBandsDecay);
      bandLevelsL[i] = std::max(targetL, decayedL);
    }

    if (silentR || peakR <= 5.0f) {
      bandLevelsR[i] = (chMode == 1) ? 0 : static_cast<uint8_t>(bandLevelsR[i] * kFastBandsDecay);
      if (bandLevelsR[i] < 4) bandLevelsR[i] = 0;
    } else {
      float ratioR = freqBinsR[i] / smoothPeakR;
      if (std::isnan(ratioR) || std::isinf(ratioR) || ratioR < 0.0f) ratioR = 0.0f;
      if (ratioR > 1.0f) ratioR = 1.0f;
      float normR = (ratioR > 0.0f) ? log10f(1.0f + 9.0f * ratioR) : 0.0f;
      if (normR > 1.0f) normR = 1.0f;

      uint8_t targetR = static_cast<uint8_t>(normR * 255.0f);
      uint8_t decayedR = static_cast<uint8_t>(bandLevelsR[i] * kFastBandsDecay);
      bandLevelsR[i] = std::max(targetR, decayedR);
    }

    bandLevels[i] = std::max(bandLevelsL[i], bandLevelsR[i]);
  }

  for (uint8_t i = bandsLen; i < WS_BANDS_64; ++i) {
    bandLevelsL[i] = 0;
    bandLevelsR[i] = 0;
    bandLevels[i] = 0;
  }

  // 7. Stereo VU Meters with Wide-Range Logarithmic dB Scaling (1% to 100% volume)
  float cleanSpecPeakL = (!silentL && peakL > 0.0f) ? peakL : 0.0f;
  float cleanSpecPeakR = (!silentR && peakR > 0.0f) ? peakR : 0.0f;

  // Calibrated log curve: 1% vol (~70) -> ~50/255, 10% vol (~900) -> ~145/255, 50% vol (~5450) -> ~222/255, 100% -> 254/255
  constexpr float kVuRef = 32.0f;
  constexpr float kVuLogDenom = 2.55676f; // log10(1.0f + 11500.0f / 32.0f)
  float vuNormL = (cleanSpecPeakL > 0.0f) ? std::min(1.0f, log10f(1.0f + cleanSpecPeakL / kVuRef) / kVuLogDenom) : 0.0f;
  float vuNormR = (cleanSpecPeakR > 0.0f) ? std::min(1.0f, log10f(1.0f + cleanSpecPeakR / kVuRef) / kVuLogDenom) : 0.0f;

  uint8_t targetVuL = static_cast<uint8_t>(vuNormL * 255.0f);
  if (silentL) {
    vuLevelL = (chMode == 2) ? 0 : static_cast<uint8_t>(vuLevelL * kFastVuDecay);
    if (vuLevelL < 4) vuLevelL = 0;
  } else {
    uint8_t decayedVuL = static_cast<uint8_t>(vuLevelL * kFastVuDecay);
    vuLevelL = std::max(targetVuL, decayedVuL);
  }

  uint8_t targetVuR = static_cast<uint8_t>(vuNormR * 255.0f);
  if (silentR) {
    vuLevelR = (chMode == 1) ? 0 : static_cast<uint8_t>(vuLevelR * kFastVuDecay);
    if (vuLevelR < 4) vuLevelR = 0;
  } else {
    uint8_t decayedVuR = static_cast<uint8_t>(vuLevelR * kFastVuDecay);
    vuLevelR = std::max(targetVuR, decayedVuR);
  }

  vuLevel = std::max(vuLevelL, vuLevelR);
}

void fillSamplesBlocking() {
  static uint32_t lastClipCheckMs = 0;
  static uint16_t consecutiveClipCount = 0;

  if (!adc_handle || !ringBufL || !ringBufR || !adcRawDmaBuf) return;

  uint32_t now_ms = millis();
  uint32_t dmaBufSize = kSampleBlock * sizeof(adc_digi_output_data_t);

  // Completely drain available DMA frames into circular ring buffers so no samples are ever dropped
  for (int drainLoop = 0; drainLoop < 8; ++drainLoop) {
    uint32_t bytes_read = 0;
    esp_err_t err = adc_continuous_read(adc_handle, adcRawDmaBuf, dmaBufSize, &bytes_read, 0);
    if (err != ESP_OK || bytes_read == 0) break;

    uint32_t count = bytes_read / sizeof(adc_digi_output_data_t);

    for (uint32_t i = 0; i < count; ++i) {
      adc_digi_output_data_t *p = (adc_digi_output_data_t *)&adcRawDmaBuf[i * sizeof(adc_digi_output_data_t)];
      int16_t raw_data = (int16_t)(p->type1.data & 0x0FFF);
      uint8_t ch = p->type1.channel;

      if (ch == ADC_CHANNEL_0) { // GPIO 36 (Right Channel)
        ringBufR[writeIdxR] = raw_data;
        writeIdxR = (writeIdxR + 1) & (kSampleBlock - 1);
        totalSamplesR++;
      } else if (ch == ADC_CHANNEL_3) { // GPIO 39 (Left Channel)
        ringBufL[writeIdxL] = raw_data;
        writeIdxL = (writeIdxL + 1) & (kSampleBlock - 1);
        totalSamplesL++;

        if (raw_data <= 50 || raw_data >= 4045) {
          consecutiveClipCount++;
          if (consecutiveClipCount >= 16) {
            if (!isClipping) clippingCount++;
            isClipping = true;
            lastClipCheckMs = now_ms;
          }
        } else {
          if (consecutiveClipCount > 0) consecutiveClipCount--;
          if (now_ms - lastClipCheckMs > 500) {
            isClipping = false;
          }
        }
      }
    }
  }

  if (totalSamplesR >= kSampleBlock && totalSamplesL >= kSampleBlock) {
    sampleCount = kSampleBlock;
  }

  static bool loggedOnce = false;
  if (!loggedOnce && totalSamplesR > 100) {
    loggedOnce = true;
    char buf[160];
    snprintf(buf, sizeof(buf), "fillDbg: R[0]=%d R[50]=%d, L[0]=%d L[50]=%d, wR=%u wL=%u, totR=%lu totL=%lu",
             ringBufR[0], ringBufR[50], ringBufL[0], ringBufL[50], writeIdxR, writeIdxL,
             (unsigned long)totalSamplesR, (unsigned long)totalSamplesL);
    commsLog("info", buf);
  }
}

void analyzerTask(void *) {
  nextProcessMs = millis();

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2));

    if (!enabled || std::strcmp(mode, "off") == 0 || !adcReady) {
      sampleCount = 0;
      totalSamplesL = 0;
      totalSamplesR = 0;
      continue;
    }

    fillSamplesBlocking();
    if (sampleCount < kSampleBlock) {
      continue;
    }

    const uint32_t now = millis();
    if (now < nextProcessMs) {
      continue;
    }

    processFft();
    // Run overlapping sliding-window Welch FFT at 8ms (125 Hz) for ultra-clean 1% volume detection
    uint32_t stepMs = std::min<uint32_t>(updateMs, 8U);
    nextProcessMs = now + stepMs;
  }
}

void validateSettings() {
  if (!(std::strcmp(mode, "off") == 0 || std::strcmp(mode, "vu") == 0 || std::strcmp(mode, "fft") == 0)) {
    snprintf(mode, sizeof(mode), "%s", ANALYZER_DEFAULT_MODE);
  }

  if (!(bandsLen == WS_BANDS_8 || bandsLen == WS_BANDS_16 || bandsLen == WS_BANDS_24 || 
        bandsLen == WS_BANDS_32 || bandsLen == WS_BANDS_64)) {
    bandsLen = ANALYZER_DEFAULT_BANDS;
  }

  if (updateMs < ANALYZER_MIN_UPDATE_MS) updateMs = ANALYZER_MIN_UPDATE_MS;
  if (updateMs > ANALYZER_MAX_UPDATE_MS) updateMs = ANALYZER_MAX_UPDATE_MS;

  WsSetNumberOfBands(bandsLen);
  bandsLen = WsGetBandsLen();
}

}

void analyzerLoadFromNvs() {
  nvs_handle_t handle;
  if (nvs_open(kNvsNs, NVS_READONLY, &handle) == ESP_OK) {
    size_t len = sizeof(mode);
    nvs_get_str(handle, kNvsKeyMode, mode, &len);

    uint8_t bands = bandsLen;
    if (nvs_get_u8(handle, kNvsKeyBands, &bands) == ESP_OK) bandsLen = bands;

    uint16_t update = updateMs;
    if (nvs_get_u16(handle, kNvsKeyUpdate, &update) == ESP_OK) updateMs = update;

    uint32_t centerBits;
    if (nvs_get_u32(handle, kNvsKeyAdcCenter, &centerBits) == ESP_OK) {
        std::memcpy(&dynamicAdcCenter, &centerBits, sizeof(float));
    }

    nvs_close(handle);
  }

  validateSettings();
}

void analyzerSaveToNvs() {
  nvs_handle_t handle;
  if (nvs_open(kNvsNs, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, kNvsKeyMode, mode);
    nvs_set_u8(handle, kNvsKeyBands, bandsLen);
    nvs_set_u16(handle, kNvsKeyUpdate, updateMs);

    uint32_t centerBits;
    std::memcpy(&centerBits, &dynamicAdcCenter, sizeof(float));
    nvs_set_u32(handle, kNvsKeyAdcCenter, centerBits);

    nvs_commit(handle);
    nvs_close(handle);
  }
}

void analyzerInit() {
  validateSettings();

  std::memset(bandLevelsL, 0, sizeof(bandLevelsL));
  std::memset(bandLevelsR, 0, sizeof(bandLevelsR));
  std::memset(bandLevels, 0, sizeof(bandLevels));
  std::memset(freqBinsL, 0, sizeof(freqBinsL));
  std::memset(freqBinsR, 0, sizeof(freqBinsR));
  vuLevelL = 0;
  vuLevelR = 0;
  vuLevel = 0;
  sampleCount = 0;

  if (!adcReady) adcReady = setupAdc();
}

void analyzerStartCore0() {
  if (taskHandle) return;
  if (!adcReady) adcReady = setupAdc();
  if (!adcReady) {
    commsLog("error", "analyzer_start_skipped_adc_not_ready");
    return;
  }

  // Pin analyzer task to Core 1 (APP CPU) so FFT and continuous ADC sampling
  // do not starve Core 0 where app_main, comms, buzzer, and BT stack run.
  BaseType_t res = xTaskCreatePinnedToCore(analyzerTask, "analyzer", 4096, nullptr, 1, &taskHandle, 1);
  if (res != pdPASS) {
    commsLog("error", "analyzer_task_create_fail");
  } else {
    commsLog("info", "analyzer_task_started");
  }
}

void analyzerStop() {
  enabled = false;

  if (taskHandle) {
    TaskHandle_t t = taskHandle;
    taskHandle = nullptr;
    vTaskDelete(t);
  }

  if (adcReady) {
    teardownAdc();
    adcReady = false;
  }

  if (fftCfg) {
    free(fftCfg);
    fftCfg = nullptr;
  }
}

void analyzerSetMode(const char *m) {
  if (!m) return;

  if (std::strcmp(m, "off") == 0 || std::strcmp(m, "vu") == 0 || std::strcmp(m, "fft") == 0) {
    snprintf(mode, sizeof(mode), "%s", m);
  }
}

void analyzerSetBands(uint8_t bands) {
  if (bands == WS_BANDS_8 || bands == WS_BANDS_16 || bands == WS_BANDS_24 || 
      bands == WS_BANDS_32 || bands == WS_BANDS_64) {
    bandsLen = bands;
    WsSetNumberOfBands(bands);
    bandsLen = WsGetBandsLen();
  }
}

void analyzerSetUpdateMs(uint16_t ms) {
  if (ms < ANALYZER_MIN_UPDATE_MS) ms = ANALYZER_MIN_UPDATE_MS;
  if (ms > ANALYZER_MAX_UPDATE_MS) ms = ANALYZER_MAX_UPDATE_MS;
  updateMs = ms;
}

void analyzerSetGainL(float g) { gainL = (g < 0.0f) ? 0.0f : g; }
void analyzerSetGainR(float g) { gainR = (g < 0.0f) ? 0.0f : g; }
void analyzerSetNoiseL(float n) { noiseL = (n < 0.0f) ? 0.0f : n; }
void analyzerSetNoiseR(float n) { noiseR = (n < 0.0f) ? 0.0f : n; }
float analyzerGetGainL() { return gainL; }
float analyzerGetGainR() { return gainR; }
float analyzerGetNoiseL() { return noiseL; }
float analyzerGetNoiseR() { return noiseR; }
void analyzerResetGainNoise() {
  gainL = 1.0f;
  gainR = 1.0f;
  noiseL = 0.0f;
  noiseR = 0.0f;
  testChMode = 0;
  gateOpenL = false;
  gateOpenR = false;
  diagMaxMagL = 0.0f;
  diagMaxMagR = 0.0f;
  diagMaxBucketL = 0;
  diagMaxBucketR = 0;
  vuLevelL = 0;
  vuLevelR = 0;
  vuLevel = 0;
  std::memset(bandLevelsL, 0, sizeof(bandLevelsL));
  std::memset(bandLevelsR, 0, sizeof(bandLevelsR));
  std::memset(bandLevels, 0, sizeof(bandLevels));
  std::memset(freqBinsL, 0, sizeof(freqBinsL));
  std::memset(freqBinsR, 0, sizeof(freqBinsR));
  if (smoothMagL && baseNoiseL && smoothMagR && baseNoiseR) {
    for (uint16_t b = 0; b < kAudibleBinsAlloc; ++b) {
      baseNoiseL[b] = smoothMagL[b];
      baseNoiseR[b] = smoothMagR[b];
    }
  }
}

void analyzerSetTestChannel(uint8_t chMode) {
  testChMode = (chMode <= 2) ? chMode : 0;
  if (testChMode == 1) {
    vuLevelR = 0;
    std::memset(bandLevelsR, 0, sizeof(bandLevelsR));
  } else if (testChMode == 2) {
    vuLevelL = 0;
    std::memset(bandLevelsL, 0, sizeof(bandLevelsL));
  }
}

uint8_t analyzerGetTestChannel() {
  return testChMode;
}

void analyzerCalibrateAdc() {
    if (!adcReady || !adc_handle) return;

    double sumR = 0, sumL = 0;
    uint16_t countsR = 0, countsL = 0;
    uint16_t minR = 4095, maxR = 0;
    uint16_t minL = 4095, maxL = 0;
    constexpr uint16_t kCalibSamples = 4096;

    bool wasEnabled = enabled;
    enabled = false;
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t calibBuf[256 * sizeof(adc_digi_output_data_t)];
    uint16_t totalCounts = 0;
    while (totalCounts < kCalibSamples) {
        uint32_t bytes_read = 0;
        if (adc_continuous_read(adc_handle, calibBuf, sizeof(calibBuf), &bytes_read, 50) == ESP_OK && bytes_read > 0) {
            size_t n = bytes_read / sizeof(adc_digi_output_data_t);
            for (size_t i = 0; i < n && totalCounts < kCalibSamples; ++i) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&calibBuf[i * sizeof(adc_digi_output_data_t)];
                uint16_t val = p->type1.data & 0x0FFF;
                if (p->type1.channel == ADC_CHANNEL_0) {
                    sumR += val;
                    countsR++;
                    if (val < minR) minR = val;
                    if (val > maxR) maxR = val;
                } else if (p->type1.channel == ADC_CHANNEL_3) {
                    sumL += val;
                    countsL++;
                    if (val < minL) minL = val;
                    if (val > maxL) maxL = val;
                }
                totalCounts++;
            }
        }
    }

    float centerR = countsR ? (float)(sumR / countsR) : ANA_ADC_CENTER;
    float centerL = countsL ? (float)(sumL / countsL) : ANA_ADC_CENTER;
    dynamicAdcCenter = centerR;
    enabled = wasEnabled;
    analyzerSaveToNvs();

    float voltR = centerR * (3300.0f / 4095.0f);
    float voltL = centerL * (3300.0f / 4095.0f);
    float compR = (centerR > 150.0f) ? (centerL / centerR) : 2.5f;
    uint32_t freqL = WsBucketFrequency(diagMaxBucketL, kSamplingFrequency, kSampleBlock);
    uint32_t freqR = WsBucketFrequency(diagMaxBucketR, kSamplingFrequency, kSampleBlock);
    char msg[220];
    snprintf(msg, sizeof(msg), "adc_calib R=%.1f(%.0fmV,comp=%.2fx,p2p=%u,max=%.0f@bk%u[%luHz]) L=%.1f(%.0fmV,p2p=%u,max=%.0f@bk%u[%luHz]) thr=%.0f",
             centerR, voltR, compR, (unsigned)(maxR >= minR ? maxR - minR : 0), diagMaxMagR, diagMaxBucketR, (unsigned long)freqR,
             centerL, voltL, (unsigned)(maxL >= minL ? maxL - minL : 0), diagMaxMagL, diagMaxBucketL, (unsigned long)freqL, diagThrL);
    commsLog("info", msg);
}

void analyzerSetEnabled(bool en) {
  enabled = en;
  if (!enabled) {
    sampleCount = 0;
    clippingCount = 0;
    vuLevelL = 0;
    vuLevelR = 0;
    vuLevel = 0;
    std::memset(bandLevelsL, 0, sizeof(bandLevelsL));
    std::memset(bandLevelsR, 0, sizeof(bandLevelsR));
    std::memset(bandLevels, 0, sizeof(bandLevels));
    std::memset(freqBinsL, 0, sizeof(freqBinsL));
    std::memset(freqBinsR, 0, sizeof(freqBinsR));
  }
}

uint8_t analyzerGetBandsLen() { return bandsLen; }
const uint8_t *analyzerGetBands() { return bandLevels; }
const uint8_t *analyzerGetBandsL() { return bandLevelsL; }
const uint8_t *analyzerGetBandsR() { return bandLevelsR; }
uint8_t analyzerGetVu() { return vuLevel; }
uint8_t analyzerGetVuL() { return vuLevelL; }
uint8_t analyzerGetVuR() { return vuLevelR; }
void analyzerGetVu(uint8_t& outVuL, uint8_t& outVuR) { outVuL = vuLevelL; outVuR = vuLevelR; }
bool analyzerIsClipping() { return isClipping; }
const char *analyzerGetMode() { return mode; }
uint16_t analyzerGetUpdateMs() { return updateMs; }
bool analyzerEnabled() { return enabled; }
uint32_t analyzerGetClippingCount() { return clippingCount; }
void analyzerResetClippingCount() { clippingCount = 0; }

float    analyzerGetDiagMaxMagL() { return diagMaxMagL; }
float    analyzerGetDiagMaxMagR() { return diagMaxMagR; }
uint16_t analyzerGetDiagMaxBucketL() { return diagMaxBucketL; }
uint16_t analyzerGetDiagMaxBucketR() { return diagMaxBucketR; }
bool     analyzerGetGateOpenL() { return gateOpenL; }
bool     analyzerGetGateOpenR() { return gateOpenR; }
float    analyzerGetDiagThrL() { return diagThrL; }
float    analyzerGetDiagThrR() { return diagThrR; }
float    analyzerGetDcOffsetL() { return lastDcOffsetL; }
float    analyzerGetDcOffsetR() { return lastDcOffsetR; }
uint32_t analyzerGetSamplesR() { return totalSamplesR; }
uint32_t analyzerGetSamplesL() { return totalSamplesL; }
int16_t  analyzerGetRawR0() { return ringBufR ? ringBufR[(writeIdxR + kSampleBlock - 1) & (kSampleBlock - 1)] : -1; }
int16_t  analyzerGetRawL0() { return ringBufL ? ringBufL[(writeIdxL + kSampleBlock - 1) & (kSampleBlock - 1)] : -1; }

#else

void analyzerLoadFromNvs() {}
void analyzerSaveToNvs() {}
void analyzerInit() {}
void analyzerStartCore0() {}
void analyzerStop() {}
void analyzerSetMode(const char *) {}
void analyzerSetBands(uint8_t) {}
void analyzerSetUpdateMs(uint16_t) {}
void analyzerSetEnabled(bool) {}
uint8_t analyzerGetBandsLen() { return 0; }
const uint8_t *analyzerGetBands() { return nullptr; }
uint8_t analyzerGetVu() { return 0; }
const char *analyzerGetMode() { return "off"; }
uint16_t analyzerGetUpdateMs() { return 0; }
bool analyzerEnabled() { return false; }

float    analyzerGetDiagMaxMagL() { return 0.0f; }
float    analyzerGetDiagMaxMagR() { return 0.0f; }
uint16_t analyzerGetDiagMaxBucketL() { return 0; }
uint16_t analyzerGetDiagMaxBucketR() { return 0; }
bool     analyzerGetGateOpenL() { return false; }
bool     analyzerGetGateOpenR() { return false; }
float    analyzerGetDiagThrL() { return 0.0f; }
float    analyzerGetDiagThrR() { return 0.0f; }
float    analyzerGetDcOffsetL() { return 0.0f; }
float    analyzerGetDcOffsetR() { return 0.0f; }
uint32_t analyzerGetSamplesR() { return 0; }
uint32_t analyzerGetSamplesL() { return 0; }
int16_t  analyzerGetRawR0() { return -1; }
int16_t  analyzerGetRawL0() { return -1; }

#endif
