#include "sensors.h"
#include "config.h"
#include "analyzer.h"
#include "state.h"
#include "comms.h"
#include <ads111x.h>
#include "esp_log.h"
#include "onewire_bus.h"
#include "onewire_cmd.h"
#include "ds18b20.h"
#include "ds3231.h"

#include <cstring>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sys/time.h>
#include "driver/gpio.h"
#include "i2cdev.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

static onewire_bus_handle_t ds18b20_bus = NULL;
static ds18b20_device_handle_t ds18b20_device = NULL;

static inline void delayUs(uint32_t us) {
  ets_delay_us(us);
}

static inline float adcToRealVolt(float vAdc, float r1, float r2) {
  return vAdc * ((r1 + r2) / r2);
}


static float voltInstant = 0.0f;
static float voltStandby = 0.0f;
static float voltVdd = 0.0f;
static float voltEsp32 = 0.0f;

static float heatC = NAN;
static float lastValidTempC = NAN;
static uint32_t lastTempMs = 0;

static bool rtcReady = false;
static bool s_rtcColdBootDetected = false;
static volatile bool rtcHwSqwOccurred = false;
static time_t s_sqwEpoch = 0;
static uint32_t s_sqwTickCounter = 0;
static uint32_t s_lastSqwTickMs = 0;
static float rtcTempC = NAN;

static void IRAM_ATTR onRtcSqwIsr(void* arg) {
  rtcHwSqwOccurred = true;
}

static i2c_dev_t ads_dev;
static bool ads_ready = false;
static bool ads_desc_valid = false;

static bool ads1115Init() {
    ads_ready = false;
    // Release the previous descriptor (mutex + bus device handle) before re-init.
    if (ads_desc_valid) {
        ads111x_free_desc(&ads_dev);
        ads_desc_valid = false;
    }
    memset(&ads_dev, 0, sizeof(i2c_dev_t));
    esp_err_t err = ads111x_init_desc(&ads_dev, ADS_I2C_ADDR, I2C_NUM_0, (gpio_num_t)I2C_SDA, (gpio_num_t)I2C_SCL);
    if (err != ESP_OK) return false;
    ads_desc_valid = true;

    ads_dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    ads_dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;

    // Non-intrusive probe: check if ADS1115 is physically present on I2C
    if (i2c_dev_probe(&ads_dev, I2C_DEV_WRITE) != ESP_OK) {
        ads111x_free_desc(&ads_dev);
        ads_desc_valid = false;
        return false;
    }

    if (ads111x_set_gain(&ads_dev, ADS111X_GAIN_6V144) != ESP_OK) return false;
    if (ads111x_set_mode(&ads_dev, ADS111X_MODE_SINGLE_SHOT) != ESP_OK) return false;
    if (ads111x_set_data_rate(&ads_dev, ADS111X_DATA_RATE_860) != ESP_OK) return false;

    ads_ready = true;
    return true;
}

static float ads1115ReadVolts(uint8_t channel) {
    if (channel > 3) return 0.0f;

    static uint8_t adsFailures = 0;
    static uint32_t lastRecoveryAttemptMs = 0;

    if (!ads_ready) {
        uint32_t nowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (nowMs - lastRecoveryAttemptMs >= 2000) {
            lastRecoveryAttemptMs = nowMs;
            if (ads1115Init()) {
                adsFailures = 0;
            }
        }
        if (!ads_ready) return 0.0f;
    }

    ads111x_mux_t mux;
    switch (channel) {
        case 0: mux = ADS111X_MUX_0_GND; break;
        case 1: mux = ADS111X_MUX_1_GND; break;
        case 2: mux = ADS111X_MUX_2_GND; break;
        case 3: mux = ADS111X_MUX_3_GND; break;
        default: return 0.0f;
    }

    if (ads111x_set_input_mux(&ads_dev, mux) != ESP_OK) {
        if (++adsFailures >= 5) ads_ready = false;
        return 0.0f;
    }
    if (ads111x_start_conversion(&ads_dev) != ESP_OK) {
        if (++adsFailures >= 5) ads_ready = false;
        return 0.0f;
    }

    ets_delay_us(1200);
    bool busy = true;
    for (int retry = 0; retry < 5; ++retry) {
        if (ads111x_is_busy(&ads_dev, &busy) == ESP_OK && !busy) break;
        ets_delay_us(200);
    }

    int16_t raw = 0;
    if (ads111x_get_value(&ads_dev, &raw) != ESP_OK) {
        if (++adsFailures >= 5) ads_ready = false;
        return 0.0f;
    }
    adsFailures = 0;
    if (raw < 0) raw = 0;

    return (raw * 0.0001875f);
}

static i2c_dev_t rtc_dev;
static bool rtc_desc_valid = false;

static bool ds3231Init() {
  if (rtc_desc_valid) {
      ds3231_free_desc(&rtc_dev);
      rtc_desc_valid = false;
  }
  memset(&rtc_dev, 0, sizeof(i2c_dev_t));
  if (ds3231_init_desc(&rtc_dev, I2C_NUM_0, (gpio_num_t)I2C_SDA, (gpio_num_t)I2C_SCL) != ESP_OK) {
      return false;
  }
  rtc_desc_valid = true;
  rtc_dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
  rtc_dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;

  // Probe hardware presence on I2C bus before claiming ready
  if (i2c_dev_probe(&rtc_dev, I2C_DEV_WRITE) != ESP_OK) {
      ds3231_free_desc(&rtc_dev);
      rtc_desc_valid = false;
      return false;
  }
  return true;
}

static float ds3231ReadTemp() {
  if (!rtcReady || !rtc_desc_valid) return NAN;
  float t = 0.0f;
  if (ds3231_get_temp_float(&rtc_dev, &t) == ESP_OK) return t;
  static uint8_t rtcTempFailures = 0;
  if (++rtcTempFailures >= 3) {
      rtcReady = false;
  }
  return NAN;
}

static bool ds3231GetTime(uint8_t &sec, uint8_t &min, uint8_t &hour, uint8_t &day, uint8_t &month, uint16_t &year) {
  if (!rtcReady || !rtc_desc_valid) return false;
  struct tm t;
  if (ds3231_get_time(&rtc_dev, &t) == ESP_OK) {
      sec = t.tm_sec; min = t.tm_min; hour = t.tm_hour;
      day = t.tm_mday; month = t.tm_mon + 1; year = t.tm_year + 1900;
      return true;
  }
  static uint8_t rtcTimeFailures = 0;
  if (++rtcTimeFailures >= 3) {
      rtcReady = false;
  }
  return false;
}

static bool ds3231SetTime(uint8_t sec, uint8_t min, uint8_t hour, uint8_t day, uint8_t month, uint16_t year) {
  if (!rtcReady || !rtc_desc_valid) return false;
  struct tm t = {};
  t.tm_sec = sec; t.tm_min = min; t.tm_hour = hour;
  t.tm_mday = day; t.tm_mon = month - 1; t.tm_year = year >= 1900 ? year - 1900 : year;
  return ds3231_set_time(&rtc_dev, &t) == ESP_OK;
}

static void syncSystemTimeFromRtc() {
  if (!rtcReady || !rtc_desc_valid) return;
  uint8_t sec = 0, min = 0, hour = 0, day = 0, month = 0;
  uint16_t year = 0;
  if (ds3231GetTime(sec, min, hour, day, month, year)) {
    struct tm tm_val = {};
    tm_val.tm_sec = sec;
    tm_val.tm_min = min;
    tm_val.tm_hour = hour;
    tm_val.tm_mday = day;
    tm_val.tm_mon = month - 1;
    tm_val.tm_year = year >= 1900 ? year - 1900 : year;
    time_t t_epoch = mktime(&tm_val);
    if (t_epoch > 0) {
      s_sqwEpoch = t_epoch;
      struct timeval tv = { .tv_sec = t_epoch, .tv_usec = 0 };
      settimeofday(&tv, NULL);
    }
  }
}

static void ds18b20StartConversion() {
  if (!ds18b20_device || !ds18b20_bus) return;
  if (onewire_bus_reset(ds18b20_bus) != ESP_OK) return;

  onewire_device_address_t addr = 0;
  if (ds18b20_get_device_address(ds18b20_device, &addr) != ESP_OK) return;

  if (addr == 0) {
    uint8_t tx[2] = { ONEWIRE_CMD_SKIP_ROM, 0x44 };
    onewire_bus_write_bytes(ds18b20_bus, tx, sizeof(tx));
  } else {
    uint8_t tx[10] = { 0 };
    tx[0] = ONEWIRE_CMD_MATCH_ROM;
    memcpy(&tx[1], &addr, sizeof(addr));
    tx[sizeof(addr) + 1] = 0x44;
    onewire_bus_write_bytes(ds18b20_bus, tx, sizeof(tx));
  }
}

static float ds18b20ReadTempC() {
  if (!ds18b20_device) return NAN;
  float temp = 0.0f;
  for (int retry = 0; retry < 3; retry++) {
    esp_err_t res = ds18b20_get_temperature(ds18b20_device, &temp);
    if (res == ESP_OK) {
      if (temp == 85.0f || temp <= -40.0f || temp >= 125.0f) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      return temp;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return NAN;
}

void sensorsInit() {
  esp_log_level_set("ds18b20", ESP_LOG_NONE);
  esp_log_level_set("onewire_bus", ESP_LOG_NONE);
  esp_log_level_set("i2cdev", ESP_LOG_NONE);
  esp_log_level_set("i2c", ESP_LOG_NONE);
  esp_log_level_set("i2c.master", ESP_LOG_NONE);

  onewire_bus_config_t bus_config = {};
  bus_config.bus_gpio_num = (gpio_num_t)DS18B20_PIN;
  onewire_bus_rmt_config_t rmt_fallback_config = {};
  rmt_fallback_config.max_rx_bytes = 10;

  if (onewire_new_bus_rmt(&bus_config, &rmt_fallback_config, &ds18b20_bus) == ESP_OK) {
      onewire_device_iter_handle_t iter = NULL;
      if (onewire_new_device_iter(ds18b20_bus, &iter) == ESP_OK) {
          onewire_device_t next_onewire_device;
          if (onewire_device_iter_get_next(iter, &next_onewire_device) == ESP_OK) {
ds18b20_config_t ds_cfg = {};

              ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &ds18b20_device);
              ds18b20_set_resolution(ds18b20_device, DS18B20_RESOLUTION_12B);
          }
          onewire_del_device_iter(iter);
      }
  }

  i2cdev_init();

  if (!ads1115Init()) {
    commsLog("error", "ADS1115 (Voltmeter) initialization failed");
  }

  rtcReady = ds3231Init();
  if (rtcReady) {
    s_rtcColdBootDetected = true;
    syncSystemTimeFromRtc();
    if (FEAT_RTC_TEMP_TELEMETRY) {
      rtcTempC = ds3231ReadTemp();
    }
    ds3231_set_squarewave_freq(&rtc_dev, DS3231_SQWAVE_1HZ);

    gpio_config_t sqw_conf = {};
    sqw_conf.intr_type = GPIO_INTR_POSEDGE;
    sqw_conf.mode = GPIO_MODE_INPUT;
    sqw_conf.pin_bit_mask = (1ULL << RTC_SQW_PIN);
    sqw_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sqw_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&sqw_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)RTC_SQW_PIN, onRtcSqwIsr, NULL);
    commsLog("info", "DS3231 (RTC) detected at cold boot. Hardware SQW 1Hz pacing active.");
  } else {
    s_rtcColdBootDetected = false;
    commsLog("info", "DS3231 (RTC) not detected - using system clock fallback");
    rtcTempC = NAN;
    rtcHwSqwOccurred = false;

    // Ensure floating GPIO 35 does not generate interrupts when RTC module is missing
    gpio_config_t sqw_conf = {};
    sqw_conf.intr_type = GPIO_INTR_DISABLE;
    sqw_conf.mode = GPIO_MODE_INPUT;
    sqw_conf.pin_bit_mask = (1ULL << RTC_SQW_PIN);
    sqw_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sqw_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&sqw_conf);
  }

  analyzerInit();
  analyzerStartCore0();
  analyzerSetEnabled(true);

  voltInstant = 0.0f;
  voltStandby = 0.0f;
  voltVdd = 0.0f;
  voltEsp32 = 0.0f;
  heatC = NAN;
  lastTempMs = 0;
  rtcTempC = NAN;
  rtcHwSqwOccurred = false;
  s_sqwTickCounter = 0;
  s_lastSqwTickMs = (uint32_t)(esp_timer_get_time() / 1000ULL);

  ds18b20StartConversion();
}

void sensorsTick(uint32_t now) {
  static uint32_t lastVoltMs = 0;
  static uint8_t adsSlot = 0;

  // Round-robin: one ADS1115 conversion per 50ms. Full refresh of all 4 rails every ~200 ms.
  if (now - lastVoltMs >= 50) {
      lastVoltMs = now;

      switch (adsSlot) {
        case 0: {
          float vAdcSmps = ads1115ReadVolts(ADS_CHANNEL_SMPS);
          float vRealSmps = adcToRealVolt(vAdcSmps, R1_OHMS, R2_OHMS);
          voltInstant = (vRealSmps >= 5.0f) ? vRealSmps : 0.0f;
          break;
        }
        case 1: {
          float vAdcStby = ads1115ReadVolts(ADS_CHANNEL_STBY);
          float vRealStby = adcToRealVolt(vAdcStby, R1_STBY_OHMS, R2_STBY_OHMS);
          vRealStby += stateStbyOffset();
          voltStandby = (vRealStby >= VOLT_MIN_VALID_V) ? vRealStby : 0.0f;
          break;
        }
        case 2: {
          float vAdcVdd = ads1115ReadVolts(ADS_CHANNEL_VDD);
          voltVdd = (vAdcVdd >= 0.5f) ? vAdcVdd : 0.0f;
          break;
        }
        default: {
          float vAdcEsp = ads1115ReadVolts(ADS_CHANNEL_ESP32);
          float vRealEsp = adcToRealVolt(vAdcEsp, R1_ESP32_OHMS, R2_ESP32_OHMS);
          voltEsp32 = (vRealEsp >= VOLT_MIN_VALID_V) ? vRealEsp : 0.0f;
          break;
        }
      }
      adsSlot = (adsSlot + 1) & 0x03;
  }

  if (now - lastTempMs >= 1000) {
    lastTempMs = now;

    static uint8_t nanFailCount = 0;
    static uint8_t spikeCount = 0;

    float t = ds18b20ReadTempC();
    ds18b20StartConversion();

    if (std::isnan(t) || t <= -40.0f || t >= 125.0f) {
      nanFailCount++;
      if (nanFailCount >= 5) {
        heatC = NAN;
      }
    } else {
      nanFailCount = 0;
      if (!std::isnan(heatC)) {
        float diff = std::fabs(t - heatC);
        if (diff > 3.0f) {
          spikeCount++;
          if (spikeCount < 3) {
            t = heatC;
          } else {
            spikeCount = 0;
          }
        } else {
          spikeCount = 0;
        }
        heatC = 0.8f * heatC + 0.2f * t;
      } else {
        heatC = t;
        spikeCount = 0;
      }
    }


    // 1 Hz SQW pulse handling (Hardware DS3231 SQW or software fallback)
    bool sqwPulsed = false;
    if (s_rtcColdBootDetected && rtcReady && rtcHwSqwOccurred) {
      rtcHwSqwOccurred = false;
      s_sqwTickCounter++;
      s_lastSqwTickMs = now;
      sqwPulsed = true;

      // Update system time strictly paced by 1 Hz SQW hardware pulse
      if (s_sqwEpoch > 0) {
        s_sqwEpoch++;
        struct timeval tv = { .tv_sec = s_sqwEpoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
      } else {
        syncSystemTimeFromRtc();
      }
    } else {
      // Fallback: If RTC was not detected at cold boot, or hardware SQW missed pulses (>= 1200 ms)
      if (now - s_lastSqwTickMs >= 1000) {
        s_sqwTickCounter++;
        s_lastSqwTickMs = now;
        sqwPulsed = true;
        if (!s_rtcColdBootDetected && s_sqwEpoch > 0) {
          s_sqwEpoch++;
          struct timeval tv = { .tv_sec = s_sqwEpoch, .tv_usec = 0 };
          settimeofday(&tv, NULL);
        }
      }
    }

    // Periodic non-blocking background RTC temp read (every 30s)
    static uint32_t lastRtcTempMs = 0;
    if (now - lastRtcTempMs >= 30000 || lastRtcTempMs == 0) {
      lastRtcTempMs = now;
      if (rtcReady && FEAT_RTC_TEMP_TELEMETRY) {
        rtcTempC = ds3231ReadTemp();
      } else {
        rtcTempC = NAN;
      }
    }

    // Periodic background RTC time resync to guarantee zero drift (every 60s, aligned with SQW tick)
    static uint32_t lastRtcSyncMs = 0;
    if (rtcReady && sqwPulsed && (now - lastRtcSyncMs >= 60000)) {
      lastRtcSyncMs = now;
      uint8_t sec = 0, min = 0, hour = 0, day = 0, month = 0;
      uint16_t year = 0;
      if (ds3231GetTime(sec, min, hour, day, month, year)) {
        struct tm tm_val = {};
        tm_val.tm_sec = sec;
        tm_val.tm_min = min;
        tm_val.tm_hour = hour;
        tm_val.tm_mday = day;
        tm_val.tm_mon = month - 1;
        tm_val.tm_year = year >= 1900 ? year - 1900 : year;
        time_t dsEpoch = mktime(&tm_val);
        if (dsEpoch > 0) {
          s_sqwEpoch = dsEpoch;
          struct timeval tv = { .tv_sec = dsEpoch, .tv_usec = 0 };
          settimeofday(&tv, NULL);
        }
      }
    }
  }
}

float getVoltageInstant() { return voltInstant; }
float sensorsReadSmpsDirect() {
  return voltInstant;
}
float getVoltageStandby() { return voltStandby; }
float getVoltageVdd() { return voltVdd; }
float getVoltageEsp32() { return voltEsp32; }
float getHeatsinkC() {
  if (!std::isnan(heatC) && heatC > -40.0f && heatC < 125.0f) {
    lastValidTempC = heatC;
    return heatC;
  }
  if (!std::isnan(rtcTempC) && rtcTempC > -40.0f && rtcTempC < 125.0f) {
    lastValidTempC = rtcTempC;
    return rtcTempC;
  }
  return lastValidTempC;
}

bool sensorsRecheckAds1115() {
  return ads1115Init();
}

bool sensorsRecheckRtc() {
  bool prevReady = rtcReady;
  rtcReady = ds3231Init();
  if (rtcReady) {
    syncSystemTimeFromRtc();
    if (FEAT_RTC_TEMP_TELEMETRY) {
      rtcTempC = ds3231ReadTemp();
    }
    if (!prevReady) {
      ds3231_set_squarewave_freq(&rtc_dev, DS3231_SQWAVE_1HZ);
      gpio_set_intr_type((gpio_num_t)RTC_SQW_PIN, GPIO_INTR_POSEDGE);
      gpio_isr_handler_add((gpio_num_t)RTC_SQW_PIN, onRtcSqwIsr, NULL);
    }
  } else {
    rtcTempC = NAN;
    rtcHwSqwOccurred = false;
    if (prevReady) {
      gpio_isr_handler_remove((gpio_num_t)RTC_SQW_PIN);
      gpio_set_intr_type((gpio_num_t)RTC_SQW_PIN, GPIO_INTR_DISABLE);
    }
  }
  return rtcReady;
}

float sensorsGetRtcTempC() {
  if (!FEAT_RTC_TEMP_TELEMETRY) return NAN;
  return rtcTempC;
}

// 100% non-blocking runtime getters reading ESP-IDF synchronized system time
bool sensorsGetTimeISO(char* out, size_t n) {
  if (!out || n == 0) return false;
  time_t now_sec;
  time(&now_sec);
  struct tm t;
  localtime_r(&now_sec, &t);
  snprintf(out, n, "%04d-%02d-%02dT%02d:%02d:%02dZ",
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
           t.tm_hour, t.tm_min, t.tm_sec);
  return true;
}

uint32_t sensorsSqwGetTickCount() {
  return s_sqwTickCounter;
}

bool sensorsSqwConsumeTick() {
  static uint32_t s_consumedTickCount = 0;
  if (s_consumedTickCount != s_sqwTickCounter) {
    s_consumedTickCount = s_sqwTickCounter;
    return true;
  }
  return false;
}

bool sensorsGetColonState() {
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  uint32_t elapsed = (now >= s_lastSqwTickMs) ? (now - s_lastSqwTickMs) : 0;
  // Standard digital clock colon: 500 ms ON (0..499 ms), 500 ms OFF (500..999 ms)
  return (elapsed < 500);
}

void analyzerGetBytes(uint8_t outBands[], size_t nBands) {
  if (!outBands || nBands == 0) return;
  const uint8_t *bands = analyzerGetBands();
  uint8_t len = analyzerGetBandsLen();
  size_t copy = nBands < static_cast<size_t>(len) ? nBands : static_cast<size_t>(len);

  if (bands) {
    std::memcpy(outBands, bands, copy);
  }

  if (nBands > copy) {
    std::memset(outBands + copy, 0, nBands - copy);
  }
}

void sensorsSetAnalyzerEnabled(bool en) {
  analyzerSetEnabled(en);
}

bool sensorsGetRtcTime(uint8_t& hours, uint8_t& minutes) {
  time_t now_sec;
  time(&now_sec);
  struct tm t;
  localtime_r(&now_sec, &t);
  hours = (uint8_t)t.tm_hour;
  minutes = (uint8_t)t.tm_min;
  return true;
}

bool sensorsGetUnixTime(uint32_t& epochOut) {
  time_t now_sec;
  time(&now_sec);
  epochOut = (uint32_t)now_sec;
  return true;
}

bool sensorsSetUnixTime(uint32_t epoch) {
  // Always update ESP-IDF system clock and SQW epoch tracker
  s_sqwEpoch = (time_t)epoch;
  struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  if (!rtcReady) return true; // System clock successfully synced

  uint32_t sec = epoch % 60; epoch /= 60;
  uint32_t min = epoch % 60; epoch /= 60;
  uint32_t hour = epoch % 24; epoch /= 24;

  uint32_t year = 1970;
  while (true) {
    uint32_t daysInYear = (year % 4 == 0) ? 366 : 365;
    if (epoch < daysInYear) break;
    epoch -= daysInYear;
    year++;
  }
  static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  uint8_t month = 1;
  for (int i = 0; i < 12; i++) {
    uint8_t mDays = monthDays[i];
    if (i == 1 && (year % 4 == 0)) mDays = 29;
    if (epoch < mDays) break;
    epoch -= mDays;
    month++;
  }
  uint8_t day = (uint8_t)epoch + 1;
  return ds3231SetTime(sec, min, hour, day, month, year);
}

bool sensorsIsRtcReady() {
  return rtcReady;
}

bool sensorsIsRtcColdBootDetected() {
  return s_rtcColdBootDetected;
}

bool sensorsHasDs18b20() {
  return ds18b20_device != NULL;
}
