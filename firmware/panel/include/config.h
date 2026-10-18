#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

// =========================================================================
// JACKTOR AUDIO — ESP32-S3 MAIN PANEL HARDWARE CONFIGURATION
// All user-customizable hardware pins, timing, and peripheral parameters.
// =========================================================================

// -------------------------------------------------------------------------
// 1. Display & Shared SPI Bus Pin Mapping
// -------------------------------------------------------------------------
// Shared SPI Bus (LCD, Touch, SD Card)
#define CONFIG_S_SCLK             12    // Shared SPI Clock pin (SCK / T_CLK / SD_SCK)
#define CONFIG_S_MISO             13    // Shared SPI MISO pin (SDO / T_DO / SD_MISO)
#define CONFIG_S_MOSI             11    // Shared SPI MOSI pin (SDI / T_DIN / SD_MOSI)

#define CONFIG_TFT_CS_PIN         10    // ILI9341 Display Chip Select (CS)
#define CONFIG_TFT_DC_PIN          9    // ILI9341 Data / Command pin (DC)
#define CONFIG_TFT_RST_PIN         8    // ILI9341 Reset pin (RESET)
#define CONFIG_TFT_BL_PIN         38    // LCD Backlight PWM pin (LED)

#define CONFIG_TOUCH_CS_PIN       15    // XPT2046 Touch Screen Chip Select (T_CS)
#define CONFIG_TOUCH_IRQ_PIN      16    // XPT2046 Touch IRQ Interrupt pin (T_IRQ)

#define CONFIG_SD_CS_PIN           4    // SD Card Reader Chip Select (SD_CS)

// -------------------------------------------------------------------------
// 2. GY-PCM5102 32-Bit I2S Audio DAC Pin Mapping & Audio Parameters
// -------------------------------------------------------------------------
#define CONFIG_I2S_DAC_WS_PIN     48    // LCK / LRCK (Word Select / Left-Right Clock)
#define CONFIG_I2S_DAC_DOUT_PIN   47    // DIN (Data Output from ESP32-S3 / Data In on DAC)
#define CONFIG_I2S_DAC_BCK_PIN    21    // BCK (Bit Clock)
// Note: SCK MUST be connected directly to GND on the PCB to enable internal PLL.

#define CONFIG_I2S_DAC_SAMPLE_RATE    384000  // High-fidelity DAC sample rate (384 kHz)
#define CONFIG_I2S_DAC_BITS_PER_SAMPLE 32     // 32-bit resolution

// -------------------------------------------------------------------------
// 3. I2C Bus Pin Mapping for LCD 1602 Karaoke Lyrics Display
// -------------------------------------------------------------------------
#define CONFIG_I2C_LCD_SDA_PIN     1    // I2C Data pin for LCD 1602 (PCF8574)
#define CONFIG_I2C_LCD_SCL_PIN     2    // I2C Clock pin for LCD 1602 (PCF8574)
#define CONFIG_I2C_LCD_ADDR     0x27    // Standard PCF8574 I2C bus address (0x27 / 0x3F)

// -------------------------------------------------------------------------
// 4. UART2 Serial Bridge to ESP32 Amplifier
// -------------------------------------------------------------------------
#define CONFIG_AMP_TX_PIN         41    // Transmit pin to Amplifier RX
#define CONFIG_AMP_RX_PIN         42    // Receive pin from Amplifier TX
#define CONFIG_AMP_BAUD_RATE      921600 // Standard communication baud rate

// -------------------------------------------------------------------------
// 4. Screen, Backlight & Power Parameters
// -------------------------------------------------------------------------
#define CONFIG_TFT_WIDTH          320   // Display horizontal resolution
#define CONFIG_TFT_HEIGHT         240   // Display vertical resolution
#define CONFIG_BACKLIGHT_PWM_FREQ 5000  // Backlight PWM frequency in Hz
#define CONFIG_DEFAULT_BACKLIGHT   255   // Default backlight duty cycle (0-255)
#define CONFIG_SLEEP_TIMEOUT_MS   120000 // Screen auto-sleep timeout in ms (2 mins)

#endif // PANEL_CONFIG_H
