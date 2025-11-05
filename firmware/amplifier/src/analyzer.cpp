#include "analyzer.h"
#include "FFT.h"
#include "config.h"

#if ANALYZER_WS_ENABLE

#include <arduinoFFT.h>
#include <driver/adc.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr uint16_t kSampleBlock = 1024;
constexpr uint32_t kSamplingFrequency = 44100;
constexpr uint16_t kI2sChunk = 256;
constexpr float    kMinAllBandsPeak = 80000.0f;

TaskHandle_t gTaskHandle = nullptr;
bool gEnabled = true;
bool gI2sReady = false;

char gMode[4] = ANALYZER_DEFAULT_MODE;
uint8_t gBandsLen = ANALYZER_DEFAULT_BANDS;
uint16_t gUpdateMs = ANALYZER_UPDATE_MS;

constexpr const char *kNvsNs = "dev/an";
constexpr const char *kNvsKeyMode = "mode";
constexpr const char *kNvsKeyBands = "bands";
constexpr const char *kNvsKeyUpdate = "update_ms";

double gReal[kSampleBlock];
double gImag[kSampleBlock];
ArduinoFFT<double> gFft(gReal, gImag, kSampleBlock, kSamplingFrequency);
uint16_t gSampleCount = 0;
float gLastAllBandsPeak = kMinAllBandsPeak;
uint8_t gBandLevels[WS_BANDS_64];
uint8_t gVuLevel = 0;
float gFreqBins[WS_BANDS_64 + 1];

uint32_t gNextProcessMs = 0;

bool setupI2S() {
  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
      .sample_rate = static_cast<int>(kSamplingFrequency),
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      .dma_buf_len = kI2sChunk,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_12);
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_0) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  if (i2s_adc_enable(I2S_NUM_0) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  return true;
}

void teardownI2S() {
  i2s_adc_disable(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_0);
}

void resetBins() {
  std::fill(std::begin(gFreqBins), std::end(gFreqBins), 0.0f);
}

void normaliseBands() {
  float allBandsPeak = 0.0f;
  for (uint8_t i = 0; i < gBandsLen; ++i) {
    if (gFreqBins[i] > allBandsPeak) {
      allBandsPeak = gFreqBins[i];
    }
  }

  float damped = ((gLastAllBandsPeak * (WS_GAIN_DAMPEN - 1.0f)) + allBandsPeak) / WS_GAIN_DAMPEN;
  if (damped < allBandsPeak) {
    damped = allBandsPeak;
  }
  allBandsPeak = std::max(damped, kMinAllBandsPeak);
  gLastAllBandsPeak = allBandsPeak;

  for (uint8_t i = 0; i < gBandsLen; ++i) {
    float ratio = (allBandsPeak > 0.0f) ? (gFreqBins[i] / allBandsPeak) : 0.0f;
    if (ratio < 0.0f) {
      ratio = 0.0f;
    } else if (ratio > 1.0f) {
      ratio = 1.0f;
    }
    gBandLevels[i] = static_cast<uint8_t>(std::lround(ratio * 255.0f));
  }
  for (uint8_t i = gBandsLen; i < WS_BANDS_64; ++i) {
    gBandLevels[i] = 0;
  }

  uint32_t sum = 0;
  for (uint8_t i = 0; i < gBandsLen; ++i) {
    sum += gBandLevels[i];
  }
  gVuLevel = (gBandsLen == 0) ? 0 : static_cast<uint8_t>((sum + (gBandsLen / 2)) / gBandsLen);
}

void processFft() {
  gFft.dcRemoval();
  gFft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  gFft.compute(FFT_FORWARD);
  gFft.complexToMagnitude();
  gFft.majorPeak();

  resetBins();
  for (uint16_t bucket = 2; bucket < (kSampleBlock / 2); ++bucket) {
    if (gReal[bucket] <= WS_NOISE_THRESHOLD) {
      continue;
    }
    const uint32_t freq = WsBucketFrequency(bucket, kSamplingFrequency, kSampleBlock);
    uint8_t band = 0;
    while (band < gBandsLen) {
      if (freq < WsGetCutoff(band)) {
        break;
      }
      ++band;
    }
    if (band > gBandsLen) {
      band = gBandsLen;
    }
    gFreqBins[band] += static_cast<float>(gReal[bucket]);
  }

  normaliseBands();
}

void fillSamplesBlocking() {
  while (gSampleCount < kSampleBlock) {
    uint16_t buffer[kI2sChunk];
    size_t bytesRead = 0;
    if (i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY) != ESP_OK) {
      return;
    }
    const uint16_t samples = static_cast<uint16_t>(bytesRead / sizeof(uint16_t));
    for (uint16_t i = 0; i < samples && gSampleCount < kSampleBlock; ++i) {
      const uint16_t raw = buffer[i] & 0x0FFFu;
      const double sample = static_cast<double>(0x0FFF) - static_cast<double>(raw);
      gReal[gSampleCount] = sample;
      gImag[gSampleCount] = 0.0;
      ++gSampleCount;
    }
    if (!gEnabled || std::strcmp(gMode, "off") == 0) {
      gSampleCount = 0;
      return;
    }
  }
}

void analyzerTask(void *) {
  gNextProcessMs = millis();
  while (true) {
    if (!gEnabled || std::strcmp(gMode, "off") == 0 || !gI2sReady) {
      gSampleCount = 0;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    fillSamplesBlocking();
    if (gSampleCount < kSampleBlock) {
      continue;
    }

    const uint32_t now = millis();
    if (now < gNextProcessMs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    processFft();
    gSampleCount = 0;
    gNextProcessMs = now + gUpdateMs;
  }
}

void validateSettings() {
  if (!(std::strcmp(gMode, "off") == 0 || std::strcmp(gMode, "vu") == 0 || std::strcmp(gMode, "fft") == 0)) {
    std::strncpy(gMode, ANALYZER_DEFAULT_MODE, sizeof(gMode) - 1);
    gMode[sizeof(gMode) - 1] = '\0';
  }
  if (!(gBandsLen == WS_BANDS_8 || gBandsLen == WS_BANDS_16 || gBandsLen == WS_BANDS_32 || gBandsLen == WS_BANDS_64)) {
    gBandsLen = ANALYZER_DEFAULT_BANDS;
  }
  if (gUpdateMs < ANALYZER_MIN_UPDATE_MS) {
    gUpdateMs = ANALYZER_MIN_UPDATE_MS;
  }
  if (gUpdateMs > ANALYZER_MAX_UPDATE_MS) {
    gUpdateMs = ANALYZER_MAX_UPDATE_MS;
  }
}

}  // namespace

void analyzerLoadFromNvs() {
  nvs_handle handle;
  if (nvs_open(kNvsNs, NVS_READONLY, &handle) == ESP_OK) {
    size_t len = sizeof(gMode);
    nvs_get_str(handle, kNvsKeyMode, gMode, &len);
    uint8_t bands = gBandsLen;
    if (nvs_get_u8(handle, kNvsKeyBands, &bands) == ESP_OK) {
      gBandsLen = bands;
    }
    uint16_t update = gUpdateMs;
    if (nvs_get_u16(handle, kNvsKeyUpdate, &update) == ESP_OK) {
      gUpdateMs = update;
    }
    nvs_close(handle);
  }
  validateSettings();
}

void analyzerSaveToNvs() {
  nvs_handle handle;
  if (nvs_open(kNvsNs, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, kNvsKeyMode, gMode);
    nvs_set_u8(handle, kNvsKeyBands, gBandsLen);
    nvs_set_u16(handle, kNvsKeyUpdate, gUpdateMs);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void analyzerInit() {
  analyzerLoadFromNvs();
  WsSetNumberOfBands(gBandsLen);
  gBandsLen = WsGetBandsLen();
  std::fill(std::begin(gBandLevels), std::end(gBandLevels), 0);
  gVuLevel = 0;
  gSampleCount = 0;
  gLastAllBandsPeak = kMinAllBandsPeak;
  gI2sReady = setupI2S();
  gNextProcessMs = millis() + gUpdateMs;
}

void analyzerStartCore0() {
  if (!gI2sReady || gTaskHandle) {
    return;
  }
  xTaskCreatePinnedToCore(analyzerTask, "analyzer", 4096, nullptr, 5, &gTaskHandle, 0);
}

void analyzerStop() {
  if (gTaskHandle) {
    TaskHandle_t handle = gTaskHandle;
    gTaskHandle = nullptr;
    vTaskDelete(handle);
  }
  if (gI2sReady) {
    teardownI2S();
    gI2sReady = false;
  }
}

void analyzerSetMode(const char *mode) {
  if (!mode) return;
  if (std::strcmp(mode, "off") == 0 || std::strcmp(mode, "vu") == 0 || std::strcmp(mode, "fft") == 0) {
    std::strncpy(gMode, mode, sizeof(gMode) - 1);
    gMode[sizeof(gMode) - 1] = '\0';
  }
}

void analyzerSetBands(uint8_t bands) {
  if (bands == WS_BANDS_8 || bands == WS_BANDS_16 || bands == WS_BANDS_32 || bands == WS_BANDS_64) {
    gBandsLen = bands;
    WsSetNumberOfBands(bands);
    gBandsLen = WsGetBandsLen();
    gLastAllBandsPeak = kMinAllBandsPeak;
  }
}

void analyzerSetUpdateMs(uint16_t ms) {
  if (ms < ANALYZER_MIN_UPDATE_MS) {
    ms = ANALYZER_MIN_UPDATE_MS;
  }
  if (ms > ANALYZER_MAX_UPDATE_MS) {
    ms = ANALYZER_MAX_UPDATE_MS;
  }
  gUpdateMs = ms;
}

void analyzerSetEnabled(bool enabled) {
  gEnabled = enabled;
  if (!gEnabled) {
    gSampleCount = 0;
  }
}

uint8_t analyzerGetBandsLen() { return gBandsLen; }
const uint8_t *analyzerGetBands() { return gBandLevels; }
uint8_t analyzerGetVu() { return gVuLevel; }
const char *analyzerGetMode() { return gMode; }
uint16_t analyzerGetUpdateMs() { return gUpdateMs; }
bool analyzerEnabled() { return gEnabled; }

#else  // ANALYZER_WS_ENABLE

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

#endif  // ANALYZER_WS_ENABLE
