#pragma once

#include <Arduino.h>

// --- Feature toggles
#define FEAT_PANEL_CLI         1
#define FEAT_FORWARD_JSON_DEF  1

#define PANEL_FW_NAME           "Jacktor Audio Panel (ESP32-S3 Bridge)"
#define PANEL_FW_VERSION        "panel-s3-2.0.0"

// --- Mapping Pin (ESP32-S3)
// UART ke Amplifier (Serial1)
#define PIN_UART_AMP_TX       17
#define PIN_UART_AMP_RX       18

// Touchscreen XPT2046 & SD Card (VSPI / SPI3)
#define PIN_TOUCH_MOSI        35
#define PIN_TOUCH_MISO        37
#define PIN_TOUCH_SCK         36
#define PIN_TOUCH_CS          38
#define PIN_TOUCH_IRQ         39

#define PIN_SD_CS             40

// --- Konfigurasi Serial
#define HOST_SERIAL_BAUD      115200 // Native USB otomatis pakai kecepatan maximum
#define AMP_SERIAL_BAUD       921600
#define BRIDGE_MAX_FRAME      2048

// Waktu tunggu deteksi PC Sleep via USB Suspend
#define PC_SLEEP_TIMEOUT_MS   3000
