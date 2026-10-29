#pragma once
#include <cstdint>
#include <cstddef>
#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define TM1637_MAX_DIGITS 4
#define TM1637_BRIGHTNESS_MAX 7

#define TM1637_SEG_A 0x01
#define TM1637_SEG_B 0x02
#define TM1637_SEG_C 0x04
#define TM1637_SEG_D 0x08
#define TM1637_SEG_E 0x10
#define TM1637_SEG_F 0x20
#define TM1637_SEG_G 0x40
#define TM1637_SEG_DP 0x80

struct tm1637_config_t {
    gpio_num_t clk_pin;
    gpio_num_t dio_pin;
    uint32_t bit_delay_us;
};

class TM1637 {
public:
    TM1637();
    ~TM1637();

    esp_err_t init(const tm1637_config_t &config);
    esp_err_t deinit();
    esp_err_t setBrightness(uint8_t brightness, bool on = true);
    esp_err_t displayOn();
    esp_err_t displayOff();
    esp_err_t setSegments(const uint8_t *segments, uint8_t length, uint8_t position = 0);
    esp_err_t clear();
    esp_err_t showNumber(int32_t number, bool leading_zeros = false, uint8_t length = TM1637_MAX_DIGITS, uint8_t position = 0);
    esp_err_t showNumberHex(uint32_t number, bool leading_zeros = false, uint8_t length = TM1637_MAX_DIGITS, uint8_t position = 0);
    esp_err_t writeString(const char *text);
    esp_err_t scrollText(const char *text, uint32_t delay_ms = 300);
    esp_err_t scrollStart(const char *text, uint32_t delay_ms = 300, bool loop = true);
    esp_err_t scrollStop();

    static uint8_t encodeDigit(uint8_t digit);

private:
    void delayUs();
    void protocolStart();
    void protocolStop();
    bool protocolTransmitByte(uint8_t byte_data);
    esp_err_t hardwareUpdate();

    gpio_num_t m_clkPin;
    gpio_num_t m_dioPin;
    uint32_t m_delayUs;
    uint8_t m_brightness;
    bool m_isOn;
    uint8_t m_buffer[TM1637_MAX_DIGITS];
    SemaphoreHandle_t m_mutex;

    TaskHandle_t m_scrollTask;
    char *m_scrollText;
    uint32_t m_scrollDelayMs;
    bool m_scrollLoop;
    bool m_scrollRunning;

    static void scrollTaskFunction(void *pvParameters);
};

// Global High-Level Interface used by Application
void tm1637Init();
void tm1637Tick();

void tm1637ShowText(const char* text, uint32_t holdMs = 0);
void tm1637SetOverrideText(const char* text);
void tm1637ShowTime(uint8_t hours, uint8_t minutes, bool showColon);
void tm1637SetBrightness(uint8_t brightness);
void tm1637Clear();
