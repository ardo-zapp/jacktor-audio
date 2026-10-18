#include "comms.h"
#include "config.h"
#include "sensors.h"
#include "comms_internal.h"
#include "comms_handlers.h"
#include "comms_telemetry.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "gaia.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

EspIdfUart linkSerial(UART_NUM_2);   // UART2 (RX16/TX17) - Primary Panel Interface (ESP32-S3)
EspIdfUart debugSerial(UART_NUM_0);  // USB CDC - Secondary/Service Port (Android/Debug)

static std::string rxLineLink;
static std::string rxLineDebug;
static uint32_t lastRtMs = 0, lastHz1Ms = 0;
bool otaReady = true, forceTel = false;

static inline uint32_t ms() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void ledActivityTick(uint32_t now) {
  static uint32_t ledStateChangeMs = 0;
  static bool ledIsOn = false;

  if (ledIsOn) {
    if (now - ledStateChangeMs >= 10) {
      gpio_set_level((gpio_num_t)LED_UART_PIN, 0);
      ledIsOn = false;
      ledStateChangeMs = now;
    }
  } else {
    if (now - ledStateChangeMs >= 25) {
      bool activeRx = (now - lastRxBlink <= 50);
      bool activeTx = (now - lastTxBlink <= 50);

      if (activeRx || activeTx) {
        gpio_set_level((gpio_num_t)LED_UART_PIN, 1);
        ledIsOn = true;
        ledStateChangeMs = now;
      }
    }
  }
}

void commsInit() {
  gpio_config_t led_conf = {};
  led_conf.intr_type = GPIO_INTR_DISABLE;
  led_conf.mode = GPIO_MODE_OUTPUT;
  led_conf.pin_bit_mask = (1ULL << LED_UART_PIN);
  led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  led_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&led_conf);
  gpio_set_level((gpio_num_t)LED_UART_PIN, 0);

  uart_config_t uart_config2 = {};
  uart_config2.baud_rate = SERIAL_BAUD_LINK; // 921600
  uart_config2.data_bits = UART_DATA_8_BITS;
  uart_config2.parity = UART_PARITY_DISABLE;
  uart_config2.stop_bits = UART_STOP_BITS_1;
  uart_config2.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config2.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(UART_NUM_2, &uart_config2);
  uart_set_pin(UART_NUM_2, UART2_TX_PIN, UART2_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_2, 2048, 2048, 0, NULL, 0);

  uart_config_t uart_config0 = {};
  uart_config0.baud_rate = SERIAL_BAUD_USB; // 921600
  uart_config0.data_bits = UART_DATA_8_BITS;
  uart_config0.parity = UART_PARITY_DISABLE;
  uart_config0.stop_bits = UART_STOP_BITS_1;
  uart_config0.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_config0.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(UART_NUM_0, &uart_config0);
  uart_driver_install(UART_NUM_0, 2048, 4096, 0, NULL, 0);

  rxLineLink.reserve(1024);
  rxLineDebug.reserve(1024);
  lastRtMs = 0;
  lastHz1Ms = 0;
  otaReady = true;
  forceTel = true;

  sendDebugLog("[DEBUG] UART2 initialized natively as Primary Panel (921600 bps)");
  sendDebugLog("[DEBUG] UART0 initialized natively as Secondary/Service Port (921600 bps)");

  vTaskDelay(pdMS_TO_TICKS(100));
  sendFeaturesSnapshot();
}

void commsTick(uint32_t now, bool sqwTick) {
  ledActivityTick(now);

  uint8_t rx_data2[512];
  int rx_len2 = 0;
  while ((rx_len2 = uart_read_bytes(UART_NUM_2, rx_data2, sizeof(rx_data2), 0)) > 0) {
    for (int i = 0; i < rx_len2; i++) {
      char c = (char)rx_data2[i];

      if (c == '\n' || c == '\r') {
        if (!rxLineLink.empty()) {
          handleJsonLine(rxLineLink, UART_NUM_2);
          rxLineLink.clear();
        }
      } else {
        if (rxLineLink.length() < 16000) rxLineLink += c;
        else rxLineLink.clear();
      }
    }
  }

  uint8_t rx_data0[512];
  int rx_len0 = 0;
  while ((rx_len0 = uart_read_bytes(UART_NUM_0, rx_data0, sizeof(rx_data0), 0)) > 0) {
    for (int i = 0; i < rx_len0; i++) {
      char c = (char)rx_data0[i];

      if (c == '\n' || c == '\r') {
        if (!rxLineDebug.empty()) {
          while (!rxLineDebug.empty() && (rxLineDebug.back() == ' ' || rxLineDebug.back() == '\r' || rxLineDebug.back() == '\n' || rxLineDebug.back() == '\t')) {
            rxLineDebug.pop_back();
          }
          if (!rxLineDebug.empty() && rxLineDebug[0] == '{') {
              handleJsonLine(rxLineDebug, UART_NUM_0);
          } else if (!rxLineDebug.empty()) {
              handleShellCommand(rxLineDebug);
          }
          rxLineDebug.clear();
        }
      } else {
        if (rxLineDebug.length() < 16000) rxLineDebug += c;
        else rxLineDebug.clear();
      }
    }
  }

  if (TELEM_REALTIME_ENABLE) {
    uint32_t intervalRt = (TELEM_HZ_REALTIME > 0) ? (1000UL / TELEM_HZ_REALTIME) : 50;
    if (intervalRt == 0 || now - lastRtMs >= intervalRt) {
      sendRealtimeTelemetry(now);
      lastRtMs = now;
    }
  }

  bool shouldSendSlow = forceTel;
  uint32_t slowInterval = (TELEM_SLOW_HZ > 0) ? (1000UL / TELEM_SLOW_HZ) : 1000UL;
  if (!shouldSendSlow) {
    if (sqwTick) shouldSendSlow = true;
    else if (now - lastHz1Ms >= slowInterval) shouldSendSlow = true;
  }

  if (shouldSendSlow) {
    sendSlowTelemetry(now);
    lastHz1Ms = now;
    forceTel = false;
  }

  if (stateSelfTestGetStatus() == SelfTestStatus::FINISHED) {
    SelfTestResults res;
    stateSelfTestGetResults(res);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "setup_report");

    bool btPass = !stateBtEnabled() || res.gaia_bt_ok;
    bool spkPass = !stateSpeakerPowerOn() || res.spk_fault_ok;
    bool overallPass = res.ads1115_ok && res.smps_fault_ok && spkPass && btPass;
    cJSON_AddStringToObject(root, "status", overallPass ? "PASS" : "FAIL");

    cJSON *results = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "results", results);
    cJSON_AddStringToObject(results, "rtc", res.rtc_ok ? "pass" : "fail");
    cJSON_AddStringToObject(results, "ads1115", res.ads1115_ok ? "pass" : "fail");
    cJSON_AddStringToObject(results, "ds18b20", res.ds18b20_ok ? "pass" : "fail");
    cJSON_AddStringToObject(results, "smps", res.smps_fault_ok ? "pass" : "fail");
    cJSON_AddStringToObject(results, "spk_protect", !stateSpeakerPowerOn() ? "disabled" : (res.spk_fault_ok ? "pass" : "fail"));
    cJSON_AddStringToObject(results, "bluetooth_gaia", !stateBtEnabled() ? "disabled" : (res.gaia_bt_ok ? "pass" : "fail"));
    if (res.gaia_bt_ok && stateBtEnabled()) {
        int8_t rssiVal = (res.gaia_rssi != 0) ? res.gaia_rssi : gaiaGetLastRssi();
        cJSON_AddNumberToObject(results, "bluetooth_gaia_rssi_dbm", rssiVal);
    }

    sendTelemetry(root);

    stateSelfTestAcknowledge();
  }
}

void commsForceTelemetry() { forceTel = true; }

void commsSetOtaReady(bool ready) {
  if (otaReady != ready) {
    otaReady = ready;
    forceTel = true;
  }
}

void commsLog(const char* level, const char* msg) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ver", "1");
  cJSON_AddStringToObject(root, "type", "log");
  cJSON_AddStringToObject(root, "lvl", level ? level : "info");
  cJSON_AddStringToObject(root, "msg", msg ? msg : "");
  cJSON_AddStringToObject(root, "origin", "AMPLIFIER");
  char timeIso[24];
  if (sensorsGetTimeISO(timeIso, sizeof(timeIso))) {
    cJSON_AddStringToObject(root, "time", timeIso);
  }
  sendTelemetry(root);
}

void commsLogFactoryReset(const char* src) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "ver", "1");
  cJSON_AddStringToObject(root, "type", "log");
  cJSON_AddStringToObject(root, "lvl", "info");
  cJSON_AddStringToObject(root, "msg", "factory_reset_executed");
  if (src && src[0] != '\0') cJSON_AddStringToObject(root, "src", src);
  char timeIso[24];
  if (sensorsGetTimeISO(timeIso, sizeof(timeIso))) {
    cJSON_AddStringToObject(root, "time", timeIso);
  }
  sendTelemetry(root);
}
