# Jacktor Audio — Main Panel (ESP32-S3)

This directory contains the firmware source code and technical documentation for the **Jacktor Audio Main Panel** powered by **ESP32-S3 (16MB Flash, 8MB PSRAM)** utilizing the **LVGL (v8.4.0)** GUI framework and **ESP-IDF** native drivers on a 3.2" LCD screen (ILI9341 + XPT2046 Touch + SD Card).

---

## 1. GPIO Mapping & Wiring Pinout

The SPI bus is shared across the TFT Display, Touch Screen, and SD Card Reader on the ESP32-S3 to optimize GPIO utilization, while individual Chip Select (CS) pins are allocated uniquely to each peripheral.

```
                  ┌──────────────────────┐
                  │      ESP32-S3        │
                  │ (FLASH 16M, PSRAM 8M)│
                  └──────────┬───────────┘
                             │
       ┌─────────────────────┼─────────────────────┐
       ▼ [Shared SPI Bus]    ▼ [Unique CS Pins]    ▼ [UART & Control]
  SCLK ──► GPIO 12      TFT_CS  ──► GPIO 10    TX ──► GPIO 41 (to Amp RX16)
  MISO ──► GPIO 13      TOUCH_CS ─► GPIO 15    RX ──► GPIO 42 (to Amp TX17)
  MOSI ──► GPIO 11      SD_CS   ──► GPIO 4     BL ──► GPIO 38 (Backlight PWM)
                        TOUCH_IRQ ─► GPIO 16
```

### Complete Wiring Specifications:

#### A. 3.2" TFT ILI9341 Display
*   `VCC` -> 3.3V / 5V
*   `GND` -> GND
*   `CS` -> **GPIO 10** (`TFT_CS`)
*   `RESET / RST` -> **GPIO 8** (`TFT_RST`)
*   `DC / RS` -> **GPIO 9** (`TFT_DC`)
*   `SDI / MOSI` -> **GPIO 11** [Shared SPI MOSI]
*   `SCK / CLK` -> **GPIO 12** [Shared SPI SCK]
*   `LED / BL` -> **GPIO 38** [PWM Backlight Brightness Control]
*   `SDO / MISO` -> **GPIO 13** [Shared SPI MISO]

#### B. Touch Screen (XPT2046)
*   `T_CLK` -> **GPIO 12** [Shared SPI SCK]
*   `T_CS` -> **GPIO 15** (`TOUCH_CS` - Unique)
*   `T_DIN` -> **GPIO 11** [Shared SPI MOSI]
*   `T_DO` -> **GPIO 13** [Shared SPI MISO]
*   `T_IRQ` -> **GPIO 16** (`TOUCH_IRQ`)

#### C. SD Card Reader
*   `SD_CS` -> **GPIO 4** (`SD_CS` - Unique)
*   `SD_MOSI` -> **GPIO 11** [Shared SPI MOSI]
*   `SD_MISO` -> **GPIO 13** [Shared SPI MISO]
*   `SD_SCK` -> **GPIO 12** [Shared SPI SCK]

---

## 2. Hybrid Asynchronous Serial Bridge Architecture

The ESP32-S3 Main Panel is engineered using an ultra-efficient **Hybrid Bridge** architecture:
1.  **S3 to Amplifier (UART2):** Connected to UART2 of the ESP32 Amplifier (`RX16/TX17`) running at **921,600 bps**.
2.  **S3 to Host Computer (USB CDC):** The native USB port of the ESP32-S3 connects directly to a PC or Mobile Host.
3.  **Data Transparency (Relay Mode):**
    *   Any binary or text command sent by the host computer via the curses terminal monitor (`tools/cmd.py`) to S3's USB port is forwarded directly and in real-time to the Amplifier.
    *   All telemetry frames (JSON formatted) and logging messages emitted by the Amplifier are relayed directly to the PC's USB port, while being **concurrently decoded locally by the S3** utilizing `cJSON` to asynchronously render audio spectrum animations and status displays on the panel!

---

## 3. Core Applications (Launcher OS)

On boot, the panel executes initial setup diagnostics and transitions to the Standby Screen. Waking it via the touch screen loads the **Menu Launcher** featuring core onboard utilities:

1.  **🎧 USB DAC App ("Jacktor Audio"):** USB Audio Class 2.0 interface supporting PCM up to 32-bit / 384 kHz, interactive rate controls, mono spectrum analyzer, stereo L/R VU meters, and Relay Audio activation.
2.  **🎮 NES Emulator App:** Pure C/C++ NES emulation core running at 60 FPS with PSRAM double-buffering, 2-player DualShock 2 gamepad support, and GY-PCM5102 I2S DAC audio.
3.  **🔧 System Settings App:** Multi-tab manager for Amplifier settings (Power, Fan Presets, BT Auto-off, SMPS Cutoff/Recovery), Panel display, SD Card formatting, and Gamepad remapping.
4.  **📊 System Home Screen:** Real-time system status bar, mono spectrum analyzer, stereo VU meters (L/R sampled from GPIO 36 & 39), GAIA media playback controls, and Bluetooth power toggle button.

---

## 4. Getting Started (Compilation & Flashing)

This firmware repository is managed using **PlatformIO Core**.

1.  Open a terminal at the repository root.
2.  Ensure PlatformIO is installed.
3.  Compile the panel firmware:
    ```bash
    pio run -d firmware/panel
    ```
4.  Connect the ESP32-S3 via a USB cable to your computer.
5.  Upload the firmware to the S3 board:
    ```bash
    pio run -d firmware/panel -t upload
    ```
6.  Launch the terminal monitor to interact with the device:
    ```bash
    python3 tools/cmd.py
    ```
