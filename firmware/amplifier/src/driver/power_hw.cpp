#include "power_hw.h"
#include "config.h"
#include "comms_internal.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile uint32_t s_btLastLowMs = 0;

static void IRAM_ATTR onBtStatusIsr(void* arg) {
  int lvl = gpio_get_level((gpio_num_t)BT_STATUS_PIN);
#if BT_STATUS_ACTIVE_LOW
  if (lvl == 1) {
    s_btLastLowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  }
#else
  if (lvl == 0) {
    s_btLastLowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  }
#endif
}

void powerHwInit() {
  // Configure Output Relay and Enable Pins
  gpio_config_t out_conf = {};
  out_conf.intr_type = GPIO_INTR_DISABLE;
  out_conf.mode = GPIO_MODE_OUTPUT;
  out_conf.pin_bit_mask = (1ULL << RELAY_SMPS_PIN) |
                          (1ULL << RELAY_AUDIO_PIN) |
                          (1ULL << SPEAKER_POWER_SWITCH_PIN) |
                          (1ULL << BT_ENABLE_PIN);
  out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&out_conf);

  // Configure Fault Pins (SMPS & Speaker Protect)
  // SMPS_FAULT_PIN (GPIO 15) has internal pull-up enabled to prevent floating LOW on loud vibration/EMI
  gpio_config_t smps_fault_conf = {};
  smps_fault_conf.intr_type = GPIO_INTR_DISABLE;
  smps_fault_conf.mode = GPIO_MODE_INPUT;
  smps_fault_conf.pin_bit_mask = (1ULL << SMPS_FAULT_PIN);
  smps_fault_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  smps_fault_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&smps_fault_conf);

  // SPK_PROTECT_LED_PIN (GPIO 34) is an input-only pin on ESP32 without internal pull-up.
  gpio_config_t spk_in_conf = {};
  spk_in_conf.intr_type = GPIO_INTR_DISABLE;
  spk_in_conf.mode = GPIO_MODE_INPUT;
  spk_in_conf.pin_bit_mask = (1ULL << SPK_PROTECT_LED_PIN);
  spk_in_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  spk_in_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&spk_in_conf);

  // Configure BT Status Pin (GPIO 23) (Pull-down enabled to prevent floating HIGH)
  gpio_config_t bt_status_conf = {};
  bt_status_conf.intr_type = GPIO_INTR_ANYEDGE;
  bt_status_conf.mode = GPIO_MODE_INPUT;
  bt_status_conf.pin_bit_mask = (1ULL << BT_STATUS_PIN);
  bt_status_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  bt_status_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&bt_status_conf);

  gpio_install_isr_service(0);
  gpio_isr_handler_add((gpio_num_t)BT_STATUS_PIN, onBtStatusIsr, NULL);
  s_btLastLowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);

  // Configure QCC Power Detect Pin (GPIO 5) - Active HIGH (Pull-down enabled to prevent floating HIGH)
  gpio_config_t qcc_conf = {};
  qcc_conf.intr_type = GPIO_INTR_DISABLE;
  qcc_conf.mode = GPIO_MODE_INPUT;
  qcc_conf.pin_bit_mask = (1ULL << QCC_PWR_DETECT_PIN);
  qcc_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  qcc_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&qcc_conf);

  // Configure LEDC Timer for Cooling Fan PWM
  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_timer.duty_resolution = LEDC_TIMER_10_BIT;
  ledc_timer.timer_num = LEDC_TIMER_0;
  ledc_timer.freq_hz = FAN_PWM_FREQ;
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&ledc_timer);

  // Configure LEDC Channel for Cooling Fan PWM
  ledc_channel_config_t ledc_channel = {};
  ledc_channel.speed_mode = LEDC_LOW_SPEED_MODE;
  ledc_channel.channel = LEDC_CHANNEL_0;
  ledc_channel.timer_sel = LEDC_TIMER_0;
  ledc_channel.gpio_num = FAN_PWM_PIN;
  ledc_channel.duty = 0;
  ledc_channel.hpoint = 0;
  ledc_channel_config(&ledc_channel);

  // Ensure all power pins are off initially
  powerHwSetRelaySmps(false);
  powerHwSetRelayAudio(false);
  powerHwSetSpeakerPins(false);
  powerHwSetBtPins(false);
}

static void safeRelaySwitch(gpio_num_t pin, uint32_t level) {
  // CRITICAL SAFETY: When turning OFF (level == 0), IMMEDIATELY set pin low with zero delay!
  if (level == 0) {
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(40));
    return;
  }

  // When turning ON (level == 1), use quiet sequence to protect CP2102 against inrush/EMI
  bool locked = takeUartTxMutex(pdMS_TO_TICKS(500));
  if (uart_is_driver_installed(UART_NUM_0)) {
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(150));
  }
  if (uart_is_driver_installed(UART_NUM_2)) {
    uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(150));
  }
  // Allow CP2102 USB transceiver to finish any in-flight USB packets and enter idle state
  vTaskDelay(pdMS_TO_TICKS(80));

  // Switch the hardware relay ON
  gpio_set_level(pin, 1);

  // Allow coil back-EMF / relay contact bounce to settle on idle bus
  vTaskDelay(pdMS_TO_TICKS(250));

  if (locked) {
    giveUartTxMutex();
  }
}

void powerHwSetRelaySmps(bool on) {
  uint32_t lvl = on ? (RELAY_SMPS_ACTIVE_HIGH ? 1 : 0) : (RELAY_SMPS_ACTIVE_HIGH ? 0 : 1);
  safeRelaySwitch((gpio_num_t)RELAY_SMPS_PIN, lvl);
}

void powerHwSetRelayAudio(bool on) {
  uint32_t lvl = on ? (RELAY_AUDIO_ACTIVE_HIGH ? 1 : 0) : (RELAY_AUDIO_ACTIVE_HIGH ? 0 : 1);
  safeRelaySwitch((gpio_num_t)RELAY_AUDIO_PIN, lvl);
}

void powerHwSetSpeakerPins(bool powerOn) {
  safeRelaySwitch((gpio_num_t)SPEAKER_POWER_SWITCH_PIN, powerOn ? 1 : 0);
}

void powerHwSetBtPins(bool enable) {
  gpio_set_level((gpio_num_t)BT_ENABLE_PIN, enable ? 1 : 0);
}

bool powerHwReadBtStatus() {
#if BT_STATUS_ACTIVE_LOW
  return gpio_get_level((gpio_num_t)BT_STATUS_PIN) == 0;
#else
  return gpio_get_level((gpio_num_t)BT_STATUS_PIN) == 1;
#endif
}

bool powerHwReadBtReady() {
  return gpio_get_level((gpio_num_t)QCC_PWR_DETECT_PIN) == 1;
}

uint32_t powerHwGetBtLastLowMs() {
  if (!powerHwReadBtStatus()) {
    s_btLastLowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
  }
  return s_btLastLowMs;
}

void powerHwResetBtLastLowMs() {
  s_btLastLowMs = (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void powerHwSetFanDuty(uint16_t duty) {
  static int32_t lastDuty = -1;
  if (duty != lastDuty) {
    if (duty > 1023) duty = 1023;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    lastDuty = duty;
  }
}

uint16_t powerHwGetFanDuty() {
  return (uint16_t)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

bool powerHwReadSmpsFault() {
#if SMPS_FAULT_ACTIVE_LOW
  return gpio_get_level((gpio_num_t)SMPS_FAULT_PIN) == 0;
#else
  return gpio_get_level((gpio_num_t)SMPS_FAULT_PIN) == 1;
#endif
}

bool powerHwReadSpkProtect() {
#if SPK_PROTECT_ACTIVE_HIGH
  return gpio_get_level((gpio_num_t)SPK_PROTECT_LED_PIN) == 1;
#else
  return gpio_get_level((gpio_num_t)SPK_PROTECT_LED_PIN) == 0;
#endif
}
