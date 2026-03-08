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

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr uint16_t kSampleBlock = 1024;
constexpr uint32_t kSamplingFrequency = 44100;
constexpr uint16_t kI2sChunk = 256;
constexpr float kMinAllBandsPeak = 80000.0f;

TaskHandle_t taskHandle = nullptr;
bool enabled = true;
bool i2sReady = false;

char mode[4] = ANALYZER_DEFAULT_MODE;
uint8_t bandsLen = ANALYZER_DEFAULT_BANDS;
uint16_t updateMs = ANALYZER_UPDATE_MS;

constexpr const char *kNvsNs = "dev/an";
constexpr const char *kNvsKeyMode = "mode";
constexpr const char *kNvsKeyBands = "bands";
constexpr const char *kNvsKeyUpdate = "update_ms";

double realBuf[kSampleBlock];
double imagBuf[kSampleBlock];
ArduinoFFT<double> fft(realBuf, imagBuf, kSampleBlock, kSamplingFrequency);

uint16_t sampleCount = 0;
float lastAllBandsPeak = kMinAllBandsPeak;

uint8_t bandLevels[WS_BANDS_64];
uint8_t vuLevel = 0;
float freqBins[WS_BANDS_64 + 1];

static float vuSmooth = 0.0f;

uint32_t nextProcessMs = 0;

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

  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
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
  std::fill(std::begin(freqBins), std::end(freqBins), 0.0f);
}

void normaliseBands() {
  float allBandsPeak = 0.0f;
  for (uint8_t i = 0; i < bandsLen; ++i) {
    if (freqBins[i] > allBandsPeak) allBandsPeak = freqBins[i];
  }

  float damped = ((lastAllBandsPeak * (WS_GAIN_DAMPEN - 1.0f)) + allBandsPeak) / WS_GAIN_DAMPEN;
  if (damped < allBandsPeak) damped = allBandsPeak;
  allBandsPeak = std::max(damped, kMinAllBandsPeak);
  lastAllBandsPeak = allBandsPeak;

  for (uint8_t i = 0; i < bandsLen; ++i) {
    float ratio = (allBandsPeak > 0.0f) ? (freqBins[i] / allBandsPeak) : 0.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    else if (ratio > 1.0f) ratio = 1.0f;
    bandLevels[i] = static_cast<uint8_t>(std::lround(ratio * 255.0f));
  }
  for (uint8_t i = bandsLen; i < WS_BANDS_64; ++i) {
    bandLevels[i] = 0;
  }
}

void processFft() {
  fft.dcRemoval();
  fft.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  fft.compute(FFT_FORWARD);
  fft.complexToMagnitude();
  fft.majorPeak();

  resetBins();
  double peak = 0.0;

  for (uint16_t bucket = 2; bucket < (kSampleBlock / 2); ++bucket) {
    const double mag = realBuf[bucket];
    if (mag > peak) peak = mag;
    if (mag <= WS_NOISE_THRESHOLD) continue;

    const uint32_t freq = WsBucketFrequency(bucket, kSamplingFrequency, kSampleBlock);
    uint8_t band = 0;
    while (band < bandsLen) {
      if (freq < WsGetCutoff(band)) break;
      ++band;
    }
    if (band > bandsLen) band = bandsLen;
    freqBins[band] += static_cast<float>(mag);
  }

  normaliseBands();

  const float noiseThreshold = 650.0f;
  const float maxRef = 3000.0f;

  if (peak <= static_cast<double>(noiseThreshold)) {
    vuSmooth *= 0.8f;
  } else {
    const float alpha = 0.2f;
    vuSmooth = alpha * static_cast<float>(peak) + (1.0f - alpha) * vuSmooth;
  }

  float vuNorm = vuSmooth / maxRef;
  if (vuNorm < 0.0f) vuNorm = 0.0f;
  if (vuNorm > 1.0f) vuNorm = 1.0f;

  vuLevel = static_cast<uint8_t>(std::lround(vuNorm * 255.0f));
}

void fillSamplesBlocking() {
  while (sampleCount < kSampleBlock) {
    uint16_t buffer[kI2sChunk];
    size_t bytesRead = 0;

    if (i2s_read(I2S_NUM_0, buffer, sizeof(buffer), &bytesRead, portMAX_DELAY) != ESP_OK) return;

    const uint16_t samples = static_cast<uint16_t>(bytesRead / sizeof(uint16_t));
    for (uint16_t i = 0; i < samples && sampleCount < kSampleBlock; ++i) {
      const uint16_t raw = buffer[i] & 0x0FFFu;
      const double sample = static_cast<double>(0x0FFF) - static_cast<double>(raw);

      realBuf[sampleCount] = sample;
      imagBuf[sampleCount] = 0.0;
      ++sampleCount;
    }

    if (!enabled || std::strcmp(mode, "off") == 0) {
      sampleCount = 0;
      return;
    }
  }
}

void analyzerTask(void *) {
  nextProcessMs = millis();

  for (;;) {
    if (!enabled || std::strcmp(mode, "off") == 0 || !i2sReady) {
      sampleCount = 0;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    fillSamplesBlocking();
    if (sampleCount < kSampleBlock) continue;

    const uint32_t now = millis();
    if (now < nextProcessMs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    processFft();
    sampleCount = 0;
    nextProcessMs = now + updateMs;
  }
}

void validateSettings() {
  if (!(std::strcmp(mode, "off") == 0 || std::strcmp(mode, "vu") == 0 || std::strcmp(mode, "fft") == 0)) {
    std::strncpy(mode, ANALYZER_DEFAULT_MODE, sizeof(mode) - 1);
    mode[sizeof(mode) - 1] = '\0';
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
  nvs_handle handle;
  if (nvs_open(kNvsNs, NVS_READONLY, &handle) == ESP_OK) {
    size_t len = sizeof(mode);
    nvs_get_str(handle, kNvsKeyMode, mode, &len);

    uint8_t bands = bandsLen;
    if (nvs_get_u8(handle, kNvsKeyBands, &bands) == ESP_OK) bandsLen = bands;

    uint16_t update = updateMs;
    if (nvs_get_u16(handle, kNvsKeyUpdate, &update) == ESP_OK) updateMs = update;

    nvs_close(handle);
  }

  validateSettings();
}

void analyzerSaveToNvs() {
  nvs_handle handle;
  if (nvs_open(kNvsNs, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_set_str(handle, kNvsKeyMode, mode);
    nvs_set_u8(handle, kNvsKeyBands, bandsLen);
    nvs_set_u16(handle, kNvsKeyUpdate, updateMs);
    nvs_commit(handle);
    nvs_close(handle);
  }
}

void analyzerInit() {
  validateSettings();

  std::memset(bandLevels, 0, sizeof(bandLevels));
  std::memset(freqBins, 0, sizeof(freqBins));
  vuLevel = 0;
  vuSmooth = 0.0f;
  sampleCount = 0;

  if (!i2sReady) i2sReady = setupI2S();
}

void analyzerStartCore0() {
  if (taskHandle || !i2sReady) return;

  xTaskCreatePinnedToCore(analyzerTask, "analyzer", 4096, nullptr, 1, &taskHandle, 0);
}

void analyzerStop() {
  enabled = false;

  if (taskHandle) {
    TaskHandle_t t = taskHandle;
    taskHandle = nullptr;
    vTaskDelete(t);
  }

  if (i2sReady) {
    teardownI2S();
    i2sReady = false;
  }
}

void analyzerSetMode(const char *m) {
  if (!m) return;

  if (std::strcmp(m, "off") == 0 || std::strcmp(m, "vu") == 0 || std::strcmp(m, "fft") == 0) {
    std::strncpy(mode, m, sizeof(mode) - 1);
    mode[sizeof(mode) - 1] = '\0';
  }
}

void analyzerSetBands(uint8_t bands) {
  if (bands == WS_BANDS_8 || bands == WS_BANDS_16 || bands == WS_BANDS_24 || 
      bands == WS_BANDS_32 || bands == WS_BANDS_64) {
    bandsLen = bands;
    WsSetNumberOfBands(bands);
    bandsLen = WsGetBandsLen();
    lastAllBandsPeak = kMinAllBandsPeak;
  }
}

void analyzerSetUpdateMs(uint16_t ms) {
  if (ms < ANALYZER_MIN_UPDATE_MS) ms = ANALYZER_MIN_UPDATE_MS;
  if (ms > ANALYZER_MAX_UPDATE_MS) ms = ANALYZER_MAX_UPDATE_MS;
  updateMs = ms;
}

void analyzerSetEnabled(bool en) {
  enabled = en;
  if (!enabled) {
    sampleCount = 0;
    vuLevel = 0;
    vuSmooth = 0.0f;
    std::memset(bandLevels, 0, sizeof(bandLevels));
    std::memset(freqBins, 0, sizeof(freqBins));
  }
}

uint8_t analyzerGetBandsLen() { return bandsLen; }
const uint8_t *analyzerGetBands() { return bandLevels; }
uint8_t analyzerGetVu() { return vuLevel; }
const char *analyzerGetMode() { return mode; }
uint16_t analyzerGetUpdateMs() { return updateMs; }
bool analyzerEnabled() { return enabled; }

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

#endif