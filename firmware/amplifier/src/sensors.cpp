#include "sensors.h"
#include "config.h"
#include "analyzer.h"

#include <Wire.h>
#include <RTClib.h>

// ====== ADS1115 (voltmeter) ======
#include <Adafruit_ADS1X15.h>
static Adafruit_ADS1115 ads;

// Helper konversi ADC→Volt riil via divider
static inline float adcToRealVolt(float vAdc) {
  // vReal = vAdc * (R1+R2)/R2
  return vAdc * ((R1_OHMS + R2_OHMS) / R2_OHMS);
}

// Nilai terakhir (langsung, tanpa smoothing)
static float gVoltInstant = 0.0f;

// ====== DS18B20 (heatsink) ======
#include <OneWire.h>
#include <DallasTemperature.h>

static OneWire         oneWire(DS18B20_PIN);
static DallasTemperature dallas(&oneWire);
static float           gHeatC = NAN;
static uint32_t        lastTempMs = 0;

// ====== RTC DS3231 ======
static RTC_DS3231 rtc;
static bool       rtcReady = false;
static volatile bool rtcSqwTick = false;
static float      rtcTempC = NAN;

static void IRAM_ATTR onRtcSqw() {
  rtcSqwTick = true;
}

// ====== Public API ======
void sensorsInit() {
  // I2C backbone
  Wire.begin(I2C_SDA, I2C_SCL);

  // ADS1115 (gain ±4.096 V → cocok untuk divider 65V → ~3V di ADC)
  ads.begin(ADS_I2C_ADDR, &Wire);
  ads.setGain(GAIN_ONE); // ±4.096 V

  // DS18B20
  dallas.begin();

  // RTC DS3231
  rtcReady = rtc.begin(&Wire);
  if (rtcReady) {
    rtc.disable32K();
    rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }
  pinMode(RTC_SQW_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RTC_SQW_PIN), onRtcSqw, RISING);

  // Analyzer (Jacktor FFT task di Core 0)
  analyzerInit();
  analyzerStartCore0();
  analyzerSetEnabled(true);

  gVoltInstant = 0.0f;
  gHeatC = NAN;
  lastTempMs = 0;
  rtcTempC = NAN;
  rtcSqwTick = false;
}

void sensorsTick(uint32_t now) {
  // --- Baca voltmeter (boleh 10–20 Hz; di sini kita jalankan setiap tick) ---
  int16_t raw = ads.readADC_SingleEnded(ADS_CHANNEL);
  float   vAdc = ads.computeVolts(raw);   // Volt di pin ADS
  float   vReal = adcToRealVolt(vAdc);
  gVoltInstant = (vReal >= VOLT_MIN_VALID_V) ? vReal : 0.0f;

  // --- Heatsink temp (1 Hz cukup) ---
  if (now - lastTempMs >= 1000) {
    lastTempMs = now;
    dallas.requestTemperatures();
    float t = dallas.getTempCByIndex(0);
    // DallasTemperature kembalikan 85.0 / DEVICE_DISCONNECTED_C saat gagal
    if (t <= -127.0f || t >= 125.0f) {
      // invalid → pertahankan nilai lama (biarkan NAN jika belum pernah valid)
    } else {
      if (FEAT_FILTER_DS18B20_SOFT && !isnan(gHeatC)) {
        gHeatC = 0.7f * gHeatC + 0.3f * t;
      } else {
        gHeatC = t;
      }
    }

    if (rtcReady && FEAT_RTC_TEMP_TELEMETRY) {
      rtcTempC = rtc.getTemperature();
    } else {
      rtcTempC = NAN;
    }
  }

}

// Voltmeter instant (tanpa smoothing)
float getVoltageInstant() {
  return gVoltInstant;
}

// Heatsink temp (Celsius)
float getHeatsinkC() {
  return gHeatC; // bisa NAN jika belum valid
}

float sensorsGetRtcTempC() {
  if (!FEAT_RTC_TEMP_TELEMETRY) {
    return NAN;
  }
  return rtcTempC;
}

bool sensorsGetTimeISO(char* out, size_t n) {
  if (!out || n == 0) return false;
  if (!rtcReady) {
    out[0] = '\0';
    return false;
  }
  DateTime now = rtc.now();
  snprintf(out, n, "%04u-%02u-%02uT%02u:%02u:%02uZ",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return true;
}

bool sensorsSqwConsumeTick() {
  if (rtcSqwTick) {
    rtcSqwTick = false;
    return true;
  }
  return false;
}

// Salin band analyzer (0..255)
void analyzerGetBytes(uint8_t outBands[], size_t nBands) {
  if (!outBands || nBands == 0) {
    return;
  }
  const uint8_t *bands = analyzerGetBands();
  uint8_t len = analyzerGetBandsLen();
  size_t copy = nBands < static_cast<size_t>(len) ? nBands : static_cast<size_t>(len);
  size_t i = 0;
  for (; i < copy; ++i) {
    outBands[i] = bands[i];
  }
  for (; i < nBands; ++i) {
    outBands[i] = 0;
  }
}

// VU mono 0..255
void analyzerGetVu(uint8_t &monoVu) {
  monoVu = analyzerGetVu();
}

// Enable/disable analyzer (hemat beban saat STANDBY)
void sensorsSetAnalyzerEnabled(bool en) {
  analyzerSetEnabled(en);
}

bool sensorsGetUnixTime(uint32_t& epochOut) {
  if (!rtcReady) return false;
  DateTime now = rtc.now();
  epochOut = now.unixtime();
  return true;
}

bool sensorsSetUnixTime(uint32_t epoch) {
  if (!rtcReady) return false;
  rtc.adjust(DateTime(epoch));
  return true;
}
