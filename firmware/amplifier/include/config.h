#pragma once
/*
  Jacktor Audio Amplifier — config.h
  --------------------------------
  Central firmware configuration. Modify only this file to adjust:
   - Pin mapping
   - Voltage divider calibration (R1/R2)
   - Fan curves & default modes
   - Bluetooth auto-off behaviors
   - Telemetry update rates
   - Audio Analyzer parameters (FFT/I2S)
   - OTA limits, etc.

  Important Notes:
  - All modules must include config.h first to maintain macro consistency.
  - SMPS & Trafo relays always default to OFF on cold boot; not stored in NVS.
  - R1/R2 resistor values should use ACTUAL measured values for accuracy.
*/


// ============================================================================
//  DS18B20 (heatsink sensor)
#define DS18B20_PIN            27

//  Firmware meta
// ============================================================================
#define FW_NAME                  "JACKTOR AUDIO"
#define FW_VERSION               "amp-1.0.0"


// ============================================================================
//  Diagnostic feature toggles & global policy
// ============================================================================
#define FEAT_BT_AUTOSWITCH_AUX       1   // 1=manage AUX↔BT (LOW≥3s, etc.)
#define FEAT_FAN_BOOT_TEST           0
#define FEAT_FACTORY_RESET_COMBO     0
#define FEAT_RTC_TEMP_TELEMETRY      1   // send rtc_c in telemetry
#define FEAT_SMPS_PROTECT_ENABLE     1
#define FEAT_SPK_PROTECT_ENABLE      1
#define FEAT_FILTER_DS18B20_SOFT     0   // optional software filter

#define ANALYZER_WS_ENABLE            1
#define ANALYZER_DEFAULT_MODE         "fft"
#define ANALYZER_DEFAULT_BANDS        16
#define ANALYZER_UPDATE_MS            33
#define ANALYZER_MIN_UPDATE_MS        16
#define ANALYZER_MAX_UPDATE_MS        100
#define WS_NOISE_THRESHOLD            0
#define WS_GAIN_DAMPEN                2

#define TELEM_REALTIME_ENABLE         1
#define TELEM_HZ_REALTIME             20
#define TELEM_SLOW_HZ                 1

#define BTN_POWER_PIN                13      // main power button (input), active LOW
#define BTN_BOOT_PIN                 0
#define BTN_POWER_ACTIVE_LOW         1
#define BT_STATUS_ACTIVE_LOW         0

#define BT_AUTO_OFF_IDLE_MS          300000

#define SMPS_CUT_V                   50.0f
#define SMPS_REC_V                   52.0f

#define SAFE_MODE_SOFT               0

// ============================================================================
//  Serial / UART
// ============================================================================
#define SERIAL_BAUD_USB          921600      // USB-CDC (Android Panel & Debug)
#define SERIAL_BAUD_LINK         921600      // UART2 (Service/Debug Port)

// Internal UART logging (Serial) -- active by default
#define LOG_ENABLE               1
#define LOG_BAUD                 SERIAL_BAUD_USB

// UART2 (Service Port)
#define UART2_RX_PIN             16
#define UART2_TX_PIN             17

// UART activity LED (TX/RX indication). Hardcoded on GPIO 2 (LED only).
#define LED_UART_PIN             2


// ============================================================================
//  Physical buttons
// ============================================================================
#define BTN_POWER_INPUT_MODE     INPUT_PULLUP
#define BTN_FACTORY_RESET_HOLD_MS 5000
#define BTN_POWER_DEBOUNCE_MS     150


// ============================================================================
//  I2C Pins
// ============================================================================
#define I2C_SDA                  21
#define I2C_SCL                  22


// ============================================================================
//  RTC DS3231
//  - The panel synchronizes time with the amplifier (rate-limited and only if offset is large)
//  - 1 Hz SQW is used for pacing telemetry during STANDBY
// ============================================================================
#define RTC_I2C_ADDR             0x68
#define RTC_SQW_PIN              35     // Input SQW 1 Hz
#define RTC_SYNC_MIN_OFFS_SEC    2      // Write to RTC only if offset > 2s
#define RTC_SYNC_MIN_INTERVAL_H  24     // Sync rate limit: 24h


// ============================================================================
//  PCF8574 I2C I/O Expander
// ============================================================================
#define PCF_I2C_ADDR             0x20


// ============================================================================
//  Voltmeter: ADS1115 (EXTERNAL) — NO smoothing
//  
//  Channel 0 (A0): SMPS 65V
//  - Divider R1 to Vin (high) and R2 to GND (low)
//  - Use ACTUAL measured resistor values for accuracy
//
//  Channel 1 (A1): Standby Power Rail
//  - Divider R1_STBY and R2_STBY (actual measured physical resistor values)
//  - Calibrated via software offset (V_STBY_OFFSET_V)
// ============================================================================
#define ADS_I2C_ADDR             0x48

// SMPS 65V (Channel 0)
#define ADS_CHANNEL_SMPS         0
#define R1_OHMS                  201200.0f  // 201.2 kΩ
#define R2_OHMS                  10030.0f   // 10.03 kΩ

// Standby Power Rail (Channel 1)
#define ADS_CHANNEL_STBY         1
#define R1_STBY_OHMS             100400.0f  // 100.4 kΩ
#define R2_STBY_OHMS             9870.0f    // 9.87 kΩ

// 3.3V VDD Power Rail (Channel 2 / A2) — Direct connection (no voltage divider)
#define ADS_CHANNEL_VDD          2

// ESP32 Power Rail (Channel 3)
#define ADS_CHANNEL_ESP32        3
#define R1_ESP32_OHMS            100300.0f  // 100.3 kΩ
#define R2_ESP32_OHMS            9940.0f    // 9.94 kΩ

// Software calibration offset for Standby Power
#define V_STBY_OFFSET_V          (-0.010f)    // Software calibration offset

// Below this threshold is considered "no power" (noise floor)
#define VOLT_MIN_VALID_V         2.0f


// ============================================================================
//  SMPS 65V Protection
//  - cutoff  : if ON and V < cutoff -> trip to STANDBY
//  - recovery: threshold to safely turn ON again
//  - bypass  : ignore protection (for testing)
// ============================================================================
#define SMPS_CUTOFF_V            SMPS_CUT_V
#define SMPS_RECOVERY_V          SMPS_REC_V
#define SMPS_SOFTSTART_MS        3000
#define SMPS_PROTECT_BYPASS      (!FEAT_SMPS_PROTECT_ENABLE)


// ============================================================================
//  Bluetooth module
//  - BT_ENABLE_PIN: power control pin for BT module
//  - BT_STATUS_PIN: from module LED, ACTIVE HIGH during BT mode (real-time)
//  - Auto-off: if staying on AUX (no BT connection) > 5 minutes -> turn off module
//  - AUX -> BT requires stable LOW >= 3 seconds; if already on BT and goes HIGH -> transition to AUX instantly
// ============================================================================
#define BT_ENABLE_PIN            4
#define QCC_PWR_DETECT_PIN       5      // QCC power detect pin (active HIGH)
#define BT_STATUS_PIN            23
#define BT_AUTO_OFF_MS           BT_AUTO_OFF_IDLE_MS
#define BT_AUX_TO_BT_MS          2000
#define BT_TO_AUX_LOSS_MS        5000
#define FEAT_BT_BUTTONS_ENABLE   0


// ============================================================================
//  Relay & Speaker control
//  - Relays default to OFF on cold boot
// ============================================================================
#define RELAY_SMPS_PIN           14
#define RELAY_SMPS_ACTIVE_HIGH   1

#define RELAY_AUDIO_PIN          26     // Relay Audio (Active ON for NES/USB DAC/DAP audio programs)
#define RELAY_AUDIO_ACTIVE_HIGH  1

#define SPEAKER_POWER_SWITCH_PIN 25


// ============================================================================
//  Fan PWM (without tachometer) + AUTO 3-point curve
//  - Mode: AUTO / CUSTOM / FAILSAFE (persisted in NVS)
//  - Optional boot test to roll the fan briefly
// ============================================================================
#define FAN_PWM_PIN              32
#define FAN_PWM_CH               0
#define FAN_PWM_FREQ             25000
#define FAN_PWM_RES_BITS         10     // Duty 0..1023

// Optional fan boot test
#define FAN_BOOT_TEST_MS         5000
#define FAN_BOOT_TEST_DUTY       1023    // 0..1023

// Default mode & custom duty
// 0=AUTO, 1=CUSTOM, 2=FAILSAFE
#define FAN_DEFAULT_MODE         0
#define FAN_CUSTOM_DUTY          1023    // used if mode is CUSTOM

// AUTO Curve (temperature point -> duty)
// Duty 0..1023; configure based on fan & heatsink characteristics
#define FAN_AUTO_T1_C            40.0f
#define FAN_AUTO_T2_C            60.0f
#define FAN_AUTO_T3_C            80.0f
#define FAN_AUTO_MIN             300
#define FAN_AUTO_D1              550
#define FAN_AUTO_D2              750
#define FAN_AUTO_D3              1023

// Fallback duty if in FAILSAFE mode
#define FAN_FALLBACK_DUTY        900


// ============================================================================
//  Buzzer (LEDC)
//  - Short click for each UI action from the panel
//  - Long warning & error tones when a fault is latched
// ============================================================================
#define BUZZER_PIN               33
#define BUZZER_PWM_CH            1
#define BUZZER_PWM_RES_BITS      10
#define BUZZER_PWM_BASE_FREQ     2000
#define BUZZER_DUTY_DEFAULT      360    // ~35% duty (0..1023)
#define BUZZER_DUTY_STRONG       480
#define BUZZER_WARNING_REPEAT_MS 2500
#define BUZZER_ERROR_REPEAT_MS   1200


// ============================================================================
//  SMPS Hardware Fault Detect (opto)
//  - Replaces the legacy PC Detect function. Opto reads the SMPS Limit/Short LED indicator.
//  - LOW = FAULT OCCURRED (Opto turns on because the SMPS error LED is active)
//  - Will trigger instant Force Shutdown.
// ============================================================================
#define SMPS_FAULT_PIN            15
#define SMPS_FAULT_ACTIVE_LOW     1


// ============================================================================
//  Speaker Protector LED monitor (opto, MONITOR only)
//  - Do not power off the protector supply from firmware; only report the fault
//  - Fault if the LED remains off >= SPK_PROTECT_FAULT_MS
// ============================================================================
#define SPK_PROTECT_LED_PIN      34
#define SPK_PROTECT_FAULT_MS     2000
#define SPK_PROTECT_ACTIVE_HIGH  0
#define SPK_PROTECT_ARM_MS       6000


// ============================================================================
//  Analyzer (Jacktor Audio FFT) — internal I2S ADC
//  - Internal ADC via I2S driver (GPIO36/ADC1_CH0)
//  - Sample block 1024 @ 44.1 kHz -> 8/16/24/32/64 log-cutoff bands
//  - Automatically disabled during STANDBY via analyzerSetEnabled(false)
// ============================================================================
#define I2S_PORT                 I2S_NUM_0
#define I2S_USE_BUILTIN_ADC      1            // use internal ADC
#define I2S_ADC_GPIO_R           36           // ADC1_CH0 (Audio In R / GPIO36)
#define I2S_ADC_GPIO_L           39           // ADC1_CH3 (Audio In L / GPIO39)
#define I2S_ADC_GPIO             36           // Primary Channel default

// FFT/Analyzer parameters (reference; runtime uses ANALYZER_DEFAULT_*)
#define ANA_N                    1024
#define ANA_FS_HZ                44100
#define ANA_UPDATE_MS            33           // ~30 FPS
#define ANA_F_LO_HZ              30
#define ANA_F_HI_HZ              18000
#define ANA_BANDS                16

// Analyzer & VU Tuning
#define ANA_ADC_CENTER           2048.0f
#define ANA_NOISE_THRESHOLD      540.0f
#define ANA_BANDS_SENSITIVITY    30000.0f
#define ANA_VU_SENSITIVITY       6000.0f
#define ANA_BANDS_DECAY          0.75f
#define ANA_VU_DECAY             0.70f


// ============================================================================
//  Telemetry pacing
//  - TELEM_REALTIME_ENABLE configures the ~30 Hz (rt) channel for Analyzer/VU/link
//  - TELEM_SLOW_HZ configures the slow (hz1) channel containing full status & NVS snapshot
//  - JSON structure: {type:"telemetry", rt:{...}, hz1:{...}}
// ============================================================================


// ============================================================================
//  TM1637 4-Digit 7-Segment Display
// ============================================================================
#define TM1637_CLK_PIN           18
#define TM1637_DIO_PIN           19


// ============================================================================
//  OTA via UART (Panel) — maximum .bin file size
//  - Verify file size & checksum before switching partitions
// ============================================================================
#define OTA_MAX_BIN_SIZE         (1024*1024)  // 1 MiB safe limit
#ifndef OTA_ENABLE
#define OTA_ENABLE               1
#endif


/*
Quick checklist when changing hardware:
- [ ] Ensure no pin conflicts exist (see all #define PIN_*)
- [ ] Adjust R1_OHMS / R2_OHMS based on actual measurements
- [ ] Adjust fan curve if fan characteristics differ
- [ ] Default analyzer configured via ANALYZER_DEFAULT_* and TELEM_HZ_REALTIME
- [ ] OTA_MAX_BIN_SIZE must be <= OTA partition size in partitions.csv
*/
