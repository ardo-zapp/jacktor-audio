#include "tm1637.h"
#include "config.h"
#include "sensors.h"
#include "power.h"
#include "state.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include "rom/ets_sys.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG __attribute__((unused)) = "TM1637";

#define CMD_SET_DATA 0x40
#define CMD_SET_ADDR 0xC0
#define CMD_DISPLAY_CTRL 0x88

#define DATA_WRITE_MODE 0x00
#define DATA_AUTO_INC 0x00
#define DATA_FIXED_ADDR 0x04

#define DISPLAY_ON_BIT 0x08
#define BRIGHTNESS_MASK 0x07

#define DEFAULT_BIT_DELAY_US 5
#define TIMING_START_US 2
#define TIMING_STOP_US 2

#define MINUS_SEGMENT 0x40
#define SPACE_SEGMENT 0x00

static const uint8_t SEGMENT_LOOKUP[16] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F, /* 9 */
    0x77, /* A */
    0x7C, /* b */
    0x39, /* C */
    0x5E, /* d */
    0x79, /* E */
    0x71  /* F */
};

static const uint8_t CHAR_LOOKUP[26] = {
    0x77, /* A */ 0x7C, /* b */ 0x39, /* C */ 0x5E, /* d */
    0x79, /* E */ 0x71, /* F */ 0x3D, /* G */ 0x76, /* H */
    0x30, /* I */ 0x1E, /* J */ 0x00, /* K */ 0x38, /* L */
    0x00, /* M */ 0x37, /* n */ 0x3F, /* O */ 0x73, /* P */
    0x67, /* q */ 0x50, /* r */ 0x6D, /* S */ 0x78, /* t */
    0x3E, /* U */ 0x00, /* V */ 0x00, /* W */ 0x76, /* X */
    0x6E, /* y */ 0x5B                              /* Z */
};

static inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

TM1637::TM1637()
    : m_clkPin(GPIO_NUM_NC)
    , m_dioPin(GPIO_NUM_NC)
    , m_delayUs(DEFAULT_BIT_DELAY_US)
    , m_brightness(TM1637_BRIGHTNESS_MAX)
    , m_isOn(true)
    , m_mutex(nullptr)
    , m_scrollTask(nullptr)
    , m_scrollText(nullptr)
    , m_scrollDelayMs(300)
    , m_scrollLoop(false)
    , m_scrollRunning(false)
{
    std::memset(m_buffer, 0, sizeof(m_buffer));
}

TM1637::~TM1637() {
    deinit();
}

void TM1637::delayUs() {
    ets_delay_us(m_delayUs);
}

void TM1637::protocolStart() {
    gpio_set_direction(m_dioPin, GPIO_MODE_OUTPUT);
    gpio_set_direction(m_clkPin, GPIO_MODE_OUTPUT);
    gpio_set_level(m_dioPin, 1);
    gpio_set_level(m_clkPin, 1);
    delayUs();

    gpio_set_level(m_dioPin, 0);
    ets_delay_us(TIMING_START_US);
    gpio_set_level(m_clkPin, 0);
    delayUs();
}

void TM1637::protocolStop() {
    gpio_set_direction(m_clkPin, GPIO_MODE_OUTPUT);
    gpio_set_direction(m_dioPin, GPIO_MODE_OUTPUT);
    gpio_set_level(m_clkPin, 0);
    gpio_set_level(m_dioPin, 0);
    delayUs();

    gpio_set_level(m_clkPin, 1);
    delayUs();

    gpio_set_level(m_dioPin, 1);
    ets_delay_us(TIMING_STOP_US);
}

bool TM1637::protocolTransmitByte(uint8_t byte_data) {
    gpio_set_direction(m_clkPin, GPIO_MODE_OUTPUT);

    for (uint8_t bit_idx = 0; bit_idx < 8; bit_idx++) {
        gpio_set_level(m_clkPin, 0);
        delayUs();

        uint8_t bit_val = (byte_data >> bit_idx) & 0x01;
        if (bit_val) {
            gpio_set_direction(m_dioPin, GPIO_MODE_INPUT);
        } else {
            gpio_set_direction(m_dioPin, GPIO_MODE_OUTPUT);
            gpio_set_level(m_dioPin, 0);
        }
        delayUs();

        gpio_set_level(m_clkPin, 1);
        delayUs();
    }

    gpio_set_level(m_clkPin, 0);
    gpio_set_direction(m_dioPin, GPIO_MODE_INPUT);
    delayUs();

    gpio_set_level(m_clkPin, 1);
    delayUs();

    bool ack_received = (gpio_get_level(m_dioPin) == 0);

    gpio_set_level(m_clkPin, 0);
    delayUs();

    return ack_received;
}

esp_err_t TM1637::hardwareUpdate() {
    protocolStart();
    protocolTransmitByte(CMD_SET_DATA | DATA_AUTO_INC);
    protocolStop();

    protocolStart();
    protocolTransmitByte(CMD_SET_ADDR | 0x00);

    for (uint8_t i = 0; i < TM1637_MAX_DIGITS; i++) {
        protocolTransmitByte(m_buffer[i]);
    }
    protocolStop();

    uint8_t ctrl_byte = CMD_DISPLAY_CTRL;
    ctrl_byte |= (m_brightness & BRIGHTNESS_MASK);
    if (m_isOn) {
        ctrl_byte |= DISPLAY_ON_BIT;
    }

    protocolStart();
    protocolTransmitByte(ctrl_byte);
    protocolStop();

    return ESP_OK;
}

esp_err_t TM1637::init(const tm1637_config_t &config) {
    m_clkPin = config.clk_pin;
    m_dioPin = config.dio_pin;
    m_delayUs = (config.bit_delay_us > 0) ? config.bit_delay_us : DEFAULT_BIT_DELAY_US;
    m_brightness = TM1637_BRIGHTNESS_MAX;
    m_isOn = true;

    if (!m_mutex) {
        m_mutex = xSemaphoreCreateMutex();
        if (!m_mutex) return ESP_ERR_NO_MEM;
    }

    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << m_clkPin) | (1ULL << m_dioPin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_cfg);
    if (ret != ESP_OK) return ret;

    gpio_set_level(m_clkPin, 0);
    gpio_set_level(m_dioPin, 0);

    std::memset(m_buffer, 0, sizeof(m_buffer));
    return hardwareUpdate();
}

esp_err_t TM1637::deinit() {
    scrollStop();
    clear();

    if (m_mutex) {
        vSemaphoreDelete(m_mutex);
        m_mutex = nullptr;
    }
    return ESP_OK;
}

esp_err_t TM1637::setBrightness(uint8_t brightness, bool on) {
    if (brightness > TM1637_BRIGHTNESS_MAX) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;

    m_brightness = brightness;
    m_isOn = on;
    esp_err_t ret = hardwareUpdate();

    xSemaphoreGive(m_mutex);
    return ret;
}

esp_err_t TM1637::displayOn() {
    return setBrightness(m_brightness, true);
}

esp_err_t TM1637::displayOff() {
    return setBrightness(m_brightness, false);
}

esp_err_t TM1637::setSegments(const uint8_t *segments, uint8_t length, uint8_t position) {
    if (!segments || length == 0 || position >= TM1637_MAX_DIGITS || (position + length) > TM1637_MAX_DIGITS) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;

    std::memcpy(&m_buffer[position], segments, length);
    esp_err_t ret = hardwareUpdate();

    xSemaphoreGive(m_mutex);
    return ret;
}

esp_err_t TM1637::clear() {
    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;

    std::memset(m_buffer, 0, sizeof(m_buffer));
    esp_err_t ret = hardwareUpdate();

    xSemaphoreGive(m_mutex);
    return ret;
}

uint8_t TM1637::encodeDigit(uint8_t digit) {
    return SEGMENT_LOOKUP[digit & 0x0F];
}

esp_err_t TM1637::showNumber(int32_t number, bool leading_zeros, uint8_t length, uint8_t position) {
    if (length == 0 || length > TM1637_MAX_DIGITS || position >= TM1637_MAX_DIGITS || (position + length) > TM1637_MAX_DIGITS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digit_buffer[TM1637_MAX_DIGITS] = {0};
    bool is_negative = (number < 0);
    uint32_t abs_number = is_negative ? -number : number;

    uint32_t temp = abs_number;
    for (int8_t i = length - 1; i >= 0; i--) {
        uint8_t digit = temp % 10;
        if (temp == 0 && i < (length - 1) && !leading_zeros) {
            digit_buffer[i] = SPACE_SEGMENT;
        } else {
            digit_buffer[i] = SEGMENT_LOOKUP[digit];
        }
        temp /= 10;
    }

    if (is_negative) {
        for (int8_t i = 0; i < length; i++) {
            if (digit_buffer[i] == SPACE_SEGMENT || i == 0) {
                digit_buffer[i] = MINUS_SEGMENT;
                break;
            }
        }
    }

    return setSegments(digit_buffer, length, position);
}

esp_err_t TM1637::showNumberHex(uint32_t number, bool leading_zeros, uint8_t length, uint8_t position) {
    if (length == 0 || length > TM1637_MAX_DIGITS || position >= TM1637_MAX_DIGITS || (position + length) > TM1637_MAX_DIGITS) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t digit_buffer[TM1637_MAX_DIGITS] = {0};

    for (int8_t i = length - 1; i >= 0; i--) {
        uint8_t nibble = number & 0x0F;
        if (number == 0 && i < (length - 1) && !leading_zeros) {
            digit_buffer[i] = SPACE_SEGMENT;
        } else {
            digit_buffer[i] = SEGMENT_LOOKUP[nibble];
        }
        number >>= 4;
    }

    return setSegments(digit_buffer, length, position);
}

static uint8_t charToSegmentHelper(char ch) {
    if (ch >= '0' && ch <= '9') {
        return SEGMENT_LOOKUP[ch - '0'];
    } else if (ch >= 'A' && ch <= 'Z') {
        return CHAR_LOOKUP[ch - 'A'];
    } else if (ch >= 'a' && ch <= 'z') {
        return CHAR_LOOKUP[ch - 'a'];
    } else if (ch == '-') {
        return MINUS_SEGMENT;
    } else if (ch == ' ') {
        return SPACE_SEGMENT;
    }
    return SPACE_SEGMENT;
}

esp_err_t TM1637::writeString(const char *text) {
    if (!text) return ESP_ERR_INVALID_ARG;

    if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;

    std::memset(m_buffer, 0, sizeof(m_buffer));

    uint8_t display_pos = 0;
    size_t text_len = std::strlen(text);

    for (size_t i = 0; i < text_len && display_pos < TM1637_MAX_DIGITS; i++) {
        char ch = text[i];
        bool has_dp = (i + 1 < text_len && text[i + 1] == '.');

        if (ch == '.') continue;

        uint8_t segment_data = charToSegmentHelper(ch);
        if (has_dp) {
            segment_data |= TM1637_SEG_DP;
            i++;
        }
        m_buffer[display_pos++] = segment_data;
    }

    esp_err_t ret = hardwareUpdate();
    xSemaphoreGive(m_mutex);
    return ret;
}

esp_err_t TM1637::scrollText(const char *text, uint32_t delay_ms) {
    if (!text || std::strlen(text) == 0) return ESP_ERR_INVALID_ARG;

    size_t text_len = std::strlen(text);
    size_t total_len = text_len + TM1637_MAX_DIGITS + TM1637_MAX_DIGITS;
    char *padded_text = (char*)std::malloc(total_len + 1);
    if (!padded_text) return ESP_ERR_NO_MEM;

    std::memset(padded_text, ' ', total_len);
    std::memcpy(padded_text + TM1637_MAX_DIGITS, text, text_len);
    padded_text[total_len] = '\0';

    for (size_t pos = 0; pos <= text_len + TM1637_MAX_DIGITS; pos++) {
        uint8_t display_buffer[TM1637_MAX_DIGITS];

        for (uint8_t i = 0; i < TM1637_MAX_DIGITS; i++) {
            char ch = padded_text[pos + i];
            display_buffer[i] = charToSegmentHelper(ch);
            if (pos + i + 1 < total_len && padded_text[pos + i + 1] == '.') {
                display_buffer[i] |= TM1637_SEG_DP;
            }
        }

        if (xSemaphoreTake(m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            std::memcpy(m_buffer, display_buffer, TM1637_MAX_DIGITS);
            hardwareUpdate();
            xSemaphoreGive(m_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    std::free(padded_text);
    return ESP_OK;
}

void TM1637::scrollTaskFunction(void *pvParameters) {
    TM1637 *self = static_cast<TM1637*>(pvParameters);
    if (!self || !self->m_scrollText) {
        vTaskDelete(NULL);
        return;
    }

    size_t text_len = std::strlen(self->m_scrollText);
    size_t total_len = text_len + TM1637_MAX_DIGITS + TM1637_MAX_DIGITS;

    char *padded_text = (char*)std::malloc(total_len + 1);
    if (!padded_text) {
        self->m_scrollRunning = false;
        vTaskDelete(NULL);
        return;
    }

    std::memset(padded_text, ' ', total_len);
    std::memcpy(padded_text + TM1637_MAX_DIGITS, self->m_scrollText, text_len);
    padded_text[total_len] = '\0';

    while (self->m_scrollRunning) {
        for (size_t pos = 0; pos <= text_len + TM1637_MAX_DIGITS && self->m_scrollRunning; pos++) {
            uint8_t display_buffer[TM1637_MAX_DIGITS];

            for (uint8_t i = 0; i < TM1637_MAX_DIGITS; i++) {
                char ch = padded_text[pos + i];
                display_buffer[i] = charToSegmentHelper(ch);
                if (pos + i + 1 < total_len && padded_text[pos + i + 1] == '.') {
                    display_buffer[i] |= TM1637_SEG_DP;
                }
            }

            if (xSemaphoreTake(self->m_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                std::memcpy(self->m_buffer, display_buffer, TM1637_MAX_DIGITS);
                self->hardwareUpdate();
                xSemaphoreGive(self->m_mutex);
            }

            vTaskDelay(pdMS_TO_TICKS(self->m_scrollDelayMs));
        }

        if (!self->m_scrollLoop) {
            self->m_scrollRunning = false;
        }
    }

    std::free(padded_text);
    self->m_scrollTask = NULL;
    vTaskDelete(NULL);
}

esp_err_t TM1637::scrollStart(const char *text, uint32_t delay_ms, bool loop) {
    if (!text || std::strlen(text) == 0) return ESP_ERR_INVALID_ARG;

    scrollStop();

    m_scrollText = strdup(text);
    if (!m_scrollText) return ESP_ERR_NO_MEM;

    m_scrollDelayMs = delay_ms;
    m_scrollLoop = loop;
    m_scrollRunning = true;

    BaseType_t ret = xTaskCreate(
        scrollTaskFunction,
        "tm1637_scroll",
        2048,
        this,
        5,
        &m_scrollTask);

    if (ret != pdPASS) {
        std::free(m_scrollText);
        m_scrollText = nullptr;
        m_scrollRunning = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t TM1637::scrollStop() {
    if (m_scrollRunning) {
        m_scrollRunning = false;

        int wait_count = 0;
        while (m_scrollTask != NULL && wait_count < 200) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait_count++;
        }

        if (m_scrollTask != NULL) {
            vTaskDelete(m_scrollTask);
            m_scrollTask = NULL;
        }
    }

    if (m_scrollText) {
        std::free(m_scrollText);
        m_scrollText = nullptr;
    }

    return ESP_OK;
}


// ============================================================================
// Global High-Level Interface Implementation for Jacktor Audio
// ============================================================================
static TM1637 g_tm1637;
static uint32_t s_holdStartMs = 0;
static uint32_t s_holdDurationMs = 0;
static char s_lastHeldText[8] = "";
static char s_overrideText[8] = "";

// Cache of what is currently on the display. Every TM1637 hardware update is a
// bit-banged transfer using ets_delay_us() (~200 x 100 us = ~20 ms busy-wait),
// so redundant updates from the main loop must be avoided or they starve the CPU.
static constexpr uint32_t TM1637_REFRESH_MS = 5000;
static uint8_t  s_curBrightness = 0xFF;
static char     s_writtenText[16] = "";
static uint32_t s_lastWriteMs = 0;

static void applyBrightnessCached(uint8_t brightness) {
    brightness &= 0x07;
    if (brightness == s_curBrightness) return;
    if (g_tm1637.setBrightness(brightness, true) == ESP_OK) {
        s_curBrightness = brightness;
    }
}

static void writeTextCached(const char* text) {
    const uint32_t now = millis();
    if (std::strncmp(s_writtenText, text, sizeof(s_writtenText)) == 0 &&
        (now - s_lastWriteMs) < TM1637_REFRESH_MS) {
        return;
    }
    if (g_tm1637.writeString(text) == ESP_OK) {
        std::strncpy(s_writtenText, text, sizeof(s_writtenText) - 1);
        s_writtenText[sizeof(s_writtenText) - 1] = '\0';
        s_lastWriteMs = now;
    }
}

void tm1637Init() {
    tm1637_config_t cfg = {
        .clk_pin = (gpio_num_t)TM1637_CLK_PIN,
        .dio_pin = (gpio_num_t)TM1637_DIO_PIN,
        .bit_delay_us = DEFAULT_BIT_DELAY_US,
    };
    g_tm1637.init(cfg);
    applyBrightnessCached(7);
    tm1637ShowText("----", 3000);
}

void tm1637SetOverrideText(const char* text) {
    if (text) {
        std::strncpy(s_overrideText, text, sizeof(s_overrideText) - 1);
        s_overrideText[sizeof(s_overrideText) - 1] = '\0';
    } else {
        s_overrideText[0] = '\0';
    }
}

void tm1637ShowText(const char* text, uint32_t holdMs) {
    if (!text) return;
    std::strncpy(s_lastHeldText, text, sizeof(s_lastHeldText) - 1);
    s_lastHeldText[sizeof(s_lastHeldText) - 1] = '\0';

    if (holdMs > 0) {
        s_holdStartMs = millis();
        s_holdDurationMs = holdMs;
    } else {
        s_holdDurationMs = 0;
    }

    writeTextCached(text);
}

void tm1637ShowTime(uint8_t hours, uint8_t minutes, bool showColon) {
    char tbuf[16];
    snprintf(tbuf, sizeof(tbuf), "%02u%s%02u", (unsigned int)hours, showColon ? "." : "", (unsigned int)minutes);
    writeTextCached(tbuf);
}

void tm1637SetBrightness(uint8_t brightness) {
    applyBrightnessCached(brightness);
}

void tm1637Clear() {
    if (g_tm1637.clear() == ESP_OK) {
        s_writtenText[0] = '\0';
        s_lastWriteMs = millis();
    }
}

void tm1637Tick() {
    uint32_t now = millis();

    if (s_holdDurationMs > 0) {
        if (now - s_holdStartMs < s_holdDurationMs) {
            // When setup/selftest is actively running steps 1-6, clear leftover hold so step numbers display immediately
            if (stateSelfTestGetStatus() == SelfTestStatus::RUNNING && stateSelfTestGetCurrentStep() > 0) {
                s_holdDurationMs = 0;
            } else {
                return;
            }
        } else {
            s_holdDurationMs = 0;

            if (std::strcmp(s_lastHeldText, "err") == 0) {
                tm1637ShowText("off", 2000);
                return;
            } else if (std::strcmp(s_lastHeldText, "pass") == 0) {
                tm1637ShowText(powerInputModeStr());
                return;
            }
        }
    }

    static uint32_t standbyStartMs = 0;
    if (!powerIsOn()) {
        if (standbyStartMs == 0) {
            standbyStartMs = now;
        }

        // Previously setBrightness() was called unconditionally on every loop
        // iteration (~20 ms busy-wait each); now only applied on change.
        applyBrightnessCached((now - standbyStartMs >= 60000) ? 1 : 7);

        static bool lastColonState = false;
        static uint32_t lastTmSqwCount = 0;
        uint32_t currentSqwCount = sensorsSqwGetTickCount();
        bool sqwTick = (currentSqwCount != lastTmSqwCount);
        if (sqwTick) {
            lastTmSqwCount = currentSqwCount;
        }

        bool colonState = sensorsGetColonState();
        bool colonChanged = (colonState != lastColonState);

        static uint32_t lastClockUpdate = 0;
        if (sqwTick || colonChanged || (now - lastClockUpdate >= 1000)) {
            lastClockUpdate = now;
            lastColonState = colonState;

            uint8_t h = 0, m = 0;
            sensorsGetRtcTime(h, m);
            tm1637ShowTime(h, m, colonState);
        }
        return;
    }

    standbyStartMs = 0;
    applyBrightnessCached(7);

    static uint32_t lastStateUpdate = 0;
    static uint8_t lastSelfTestStep = 0;
    static SelfTestStatus lastSelfTestStatus = SelfTestStatus::IDLE;

    SelfTestStatus curStatus = stateSelfTestGetStatus();
    uint8_t curStep = (curStatus == SelfTestStatus::RUNNING) ? stateSelfTestGetCurrentStep() : 0;
    bool stepChanged = (curStep != lastSelfTestStep) || (curStatus != lastSelfTestStatus);

    if (stepChanged || (now - lastStateUpdate >= 500)) {
        lastStateUpdate = now;
        lastSelfTestStep = curStep;
        lastSelfTestStatus = curStatus;

        if (s_overrideText[0] != '\0') {
            tm1637ShowText(s_overrideText);
        } else if (curStatus == SelfTestStatus::RUNNING) {
            if (curStep == 0) {
                tm1637ShowText("on");
            } else {
                char sBuf[8];
                snprintf(sBuf, sizeof(sBuf), "%u- 6", curStep);
                tm1637ShowText(sBuf);
            }
        } else if (powerIsInitializing()) {
            tm1637ShowText("on");
        } else {
            tm1637ShowText(powerInputModeStr());
        }
    }
}
