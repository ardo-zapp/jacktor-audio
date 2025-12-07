#include "sensors.h"
#include "config.h"
#include "analyzer.h"

#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>

static Adafruit_ADS1115 ads;

static inline float adcToRealVolt(float vAdc, float r1, float r2) {
  return vAdc * ((r1 + r2) / r2);
}

static float voltInstant = 0.0f;
static float volt12V = 0.0f;

static OneWire oneWire(DS18B20_PIN);
static DallasTemperature dallas(&oneWire);
static float heatC = NAN;
static uint32_t lastTempMs = 0;

static RTC_DS3231 rtc;
static bool rtcReady = false;
static volatile bool rtcSqwTick = false;
static float rtcTempC = NAN;

static void IRAM_ATTR onRtcSqw() {
  rtcSqwTick = true;
}

void sensorsInit() {
  Wire.begin(I2C_SDA, I2C_SCL);

  ads.begin(ADS_I2C_ADDR, &Wire);
  ads.setGain(GAIN_ONE);

  dallas.begin();

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

  analyzerInit();
  analyzerStartCore0();
  analyzerSetEnabled(true);

  voltInstant = 0.0f;
  volt12V = 0.0f;
  heatC = NAN;
  lastTempMs = 0;
  rtcTempC = NAN;
  rtcSqwTick = false;
}

void sensorsTick(uint32_t now) {
  // Read SMPS 65V (Channel 0)
  int16_t rawSmps = ads.readADC_SingleEnded(ADS_CHANNEL_SMPS);
  float vAdcSmps = ads.computeVolts(rawSmps);
  float vRealSmps = adcToRealVolt(vAdcSmps, R1_OHMS, R2_OHMS);
  voltInstant = (vRealSmps >= VOLT_MIN_VALID_V) ? vRealSmps : 0.0f;

  // Read 12V rail (Channel 1)
  int16_t raw12V = ads.readADC_SingleEnded(ADS_CHANNEL_12V);
  float vAdc12V = ads.computeVolts(raw12V);
  float vReal12V = adcToRealVolt(vAdc12V, R1_12V_OHMS, R2_12V_OHMS);
  volt12V = (vReal12V >= VOLT_MIN_VALID_V) ? vReal12V : 0.0f;

  if (now - lastTempMs >= 1000) {
    lastTempMs = now;
    dallas.requestTemperatures();
    float t = dallas.getTempCByIndex(0);
    if (t <= -127.0f || t >= 125.0f) {
    } else {
      if (FEAT_FILTER_DS18B20_SOFT && !isnan(heatC)) {
        heatC = 0.7f * heatC + 0.3f * t;
      } else {
        heatC = t;
      }
    }

    if (rtcReady && FEAT_RTC_TEMP_TELEMETRY) {
      rtcTempC = rtc.getTemperature();
    } else {
      rtcTempC = NAN;
    }
  }
}

float getVoltageInstant() {
  return voltInstant;
}

float getVoltage12V() {
  return volt12V;
}

float getHeatsinkC() {
  return heatC;
}

float sensorsGetRtcTempC() {
  if (!FEAT_RTC_TEMP_TELEMETRY) return NAN;
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

void analyzerGetBytes(uint8_t outBands[], size_t nBands) {
  if (!outBands || nBands == 0) return;
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

void analyzerGetVu(uint8_t &monoVu) {
  monoVu = analyzerGetVu();
}

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