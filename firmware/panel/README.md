# Main Panel Unit (ESP32-S3)

This firmware serves as the primary User Interface & Control Panel of the Jacktor Audio smart ecosystem. It is built on the **ESP-IDF** framework using FreeRTOS, driving a 3.2" ILI9341 TFT Display with XPT2046 Touch Control via **LVGL v8.4.0**, an integrated **GY-PCM5102 32-Bit 384 kHz I2S Audio DAC**, and an onboard **2-Player NES Nofrendo Emulator**.

## Dependencies & Component Libraries

### External Managed Components (`idf_component.yml`)
- **`espressif/cjson: ^1.7.18`**: Lightweight JSON parsing engine for UART2 bridge telemetry.
- **`lvgl/lvgl: ^8.4.0`**: Embedded GUI engine driving the 3.2" ILI9341 display.

### Embedded Internal Components (`components/`)
- **`micro_flac` Component** (`components/micro_flac`): Standalone ESP-IDF FLAC decoder component (`esphome-libs/micro-flac`).
- **`micro_mp3` Component** (`components/micro_mp3`): Standalone ESP-IDF MP3 frame decoder component (`esphome-libs/micro-mp3`).
- **`nofrendo` Component**: Standalone ESP-IDF NES emulator component running at 60 FPS.
- **`saf_manager` (Storage Access Framework)**: Unified storage abstraction (SD Card, USB MSC, SMB).
- **`psram_cache`**: 32KB PSRAM ring-buffer stream and metadata cache.
- **`utf8_utils`**: Universal multi-byte UTF-8 string sanitizer.

## System Mechanisms & Professional Features

- **GY-PCM5102 32-Bit 384 kHz I2S Audio DAC**: High-fidelity digital-to-analog conversion driven via native ESP-IDF `driver/i2s_std.h` (BCK = GPIO 21, DOUT = GPIO 47, WS = GPIO 48, SCK = GND). Operates at 32-bit resolution and supports up to 384 kHz sample rate for NES emulator APU playback, USB DAC mode, and global audio playback.
- **USB DAC Mode App (`ScreenState::USB_DAC`)**: High-performance USB Audio Class interface named **"Jacktor Audio"** supporting PC & Mobile plug-and-play. Features dynamic sample rate switching (up to 32-bit / 384 kHz PCM metadata display), automated screen launch/close on USB attach/detach, modern disconnected fallback view, real-time Mono Audio Spectrum Analyzer, Stereo L/R VU Meter bars, automated Relay Audio activation, and TM1637 `"usb"` display override on the main amplifier unit.
- **Ported `micro-flac` C++ FLAC Decoder & Music Player App (`ScreenState::MUSIC_PLAYER`)**: Ported `micro-flac` C++ decoder library for playing `.flac` and `.wav` tracks directly from SD Card (`/sdcard/MUSIC`). Features track metadata display, playback progress, mono spectrum analyzer, stereo L/R VU meters, and Relay Audio activation.
- **System Home / Operational Interface (`ScreenState::OPERATIONAL`)**: Complete system UI featuring a top Status Bar (clock, voltage rails, heatsink/RTC temps, input mode badge), a central Audio Visualizer (mono spectrum analyzer and stereo L/R VU meters sampled from GPIO 36 & GPIO 39), a GAIA Media Control bar (Play, Pause, Prev, Next, Vol-, Vol+, and a Bluetooth Power toggle button with dimmed/crossed state), Launcher app buttons (NES, USB DAC, Settings), and a bottom System Navigation bar (Power OFF and Standby controls).
- **Deep Sleep Standby & Independent Bypass Boot Mode**: When switching to Standby/Power OFF, the panel enters ESP32-S3 Deep Sleep (`esp_deep_sleep_start()`) with BOOT button (GPIO 0) RTC ext0 wakeup enabled. Pressing the BOOT button boots the panel into **Independent Bypass Mode** (`isIndependentBypassMode = true`), displaying an overlay warning badge, graying out amplifier-dependent controls, while maintaining 100% full functionality for independent panel applications (USB DAC, NES Emulator, SD Card Manager, Display, Gamepad Mapping).
- **Onboard 2-Player NES Nofrendo Emulator**: Pure C/C++ NES emulation engine running on FreeRTOS with PSRAM frame buffering (256x240 @ 60 FPS), direct ILI9341 SPI DMA streaming (zero screen flicker), 2-player DualShock 2 gamepad input with persistent NVS button remapping, SD card ROM scanner (`/sdcard/NES`), synthesized 8-bit NES menu BGM via GY-PCM5102 I2S DAC, semi-transparent 5-second touch overlay bar, and `SELECT + START` 1-second exit hotkey.
- **System Settings App**: Categorized multi-tab configuration manager (`ScreenState::SETTINGS_APP`):
  1. **Amplifier Settings**: Relay Power, Speaker Power, Fan Presets (Quiet, Normal, Performance), BT Auto-off, Auto-Sense.
  2. **Panel Display Settings**: Backlight PWM brightness slider (0..255) and LCD Sleep Timeout selector.
  3. **SD Card Management**: Storage usage progress bar (calculated via POSIX `statvfs`), file counter, and FAT Format SD Card utility.
  4. **Gamepad Mapping**: Player 1 & Player 2 DualShock 2 controller button remapping and NVS flash storage.
  5. **Device Info & Diagnostics**: MCU specs, Flash/PSRAM memory usage, OS/Firmware version, and real-time sensor diagnostics.
- **Hybrid Asynchronous Serial Bridge**: Connects to the ESP32 Amplifier over UART2 at **921,600 bps** while simultaneously providing pass-through USB CDC bridging to the host computer monitor.
- **Shared SPI Bus Management**: Efficient bus sharing between TFT Display, XPT2046 Touch Controller, and SD Card Reader using dedicated Chip Select (`CS`) pins.
- **Ambient WS2812 Status Visualizer**: Multi-state RGB indicator providing ambient feedback for Standby breathing, active playback, audio clipping, and system alarms.

## GPIO Mapping (Pinout & Wiring)

GPIO mappings are configured in [`include/panel_main.h`](include/panel_main.h). Understanding this pin layout is critical for proper physical wiring and system assembly.

| Component / Function | GPIO (ESP32-S3) | Specification & Details |
| :--- | :---: | :--- |
| **Shared SPI SCLK** | `12` | Clock line shared between LCD (`SCK`), Touch (`T_CLK`), and SD Card (`SD_SCK`). |
| **Shared SPI MISO** | `13` | Master-In-Slave-Out line shared between LCD (`SDO`), Touch (`T_DO`), and SD Card (`SD_MISO`). |
| **Shared SPI MOSI** | `11` | Master-Out-Slave-In line shared between LCD (`SDI`), Touch (`T_DIN`), and SD Card (`SD_MOSI`). |
| **TFT Chip Select (CS)** | `10` | Output (*Active LOW*). ILI9341 Display CS (`CS`). |
| **TFT Data/Command (DC)** | `9` | Output (*Data = HIGH, Command = LOW*). ILI9341 DC (`DC`). |
| **TFT Reset (RST)** | `8` | Output (*Active LOW*). Hardware reset pin for ILI9341 (`RESET`). |
| **TFT Backlight (PWM)** | `38` | LEDC PWM channel controlling LCD backlight brightness (`LED`). |
| **Touch Chip Select (T_CS)** | `15` | Output (*Active LOW*). XPT2046 Touch Controller CS (`T_CS`). |
| **Touch Interrupt (T_IRQ)** | `16` | Input (*Active LOW*). XPT2046 touch event detection pin (`T_IRQ`). |
| **SD Card Chip Select (SD_CS)** | `4` | Output (*Active LOW*). SD Card SPI CS pin (`SD_CS`). |
| **I2S DAC LRCK / WS** | `48` | Output. GY-PCM5102 Word Select / Left-Right Clock (`I2S_DAC_WS_PIN`). |
| **I2S DAC DIN / DOUT** | `47` | Output. GY-PCM5102 Data Out from ESP32-S3 (`I2S_DAC_DOUT_PIN`). |
| **I2S DAC BCK / BCLK** | `21` | Output. GY-PCM5102 Bit Clock (`I2S_DAC_BCK_PIN`). |
| **I2S DAC SCK** | `GND` | **IMPORTANT**: MUST be connected directly to **GND** to enable internal PLL. |
| **UART TX (Bridge)** | `41` | Transmit line connected to ESP32 Amplifier RX (921,600 bps). |
| **UART RX (Bridge)** | `42` | Receive line connected to ESP32 Amplifier TX (921,600 bps). |

> [!WARNING]
> **GY-PCM5102 Hardware Requirements & Warnings**
> 1. **System Clock (SCK)**: The PCM5102 module generates its own master clock via internal PLL. **SCK MUST be connected directly to GND**; do not leave it floating.
> 2. **Signal Noise Sensitivity**: High-frequency I2S lines (BCK, DOUT, WS) are sensitive to EMI and crosstalk. Keep wiring connections as short as possible.
> 3. **Configuration Jumpers**: Pads on the back of the GY-PCM5102 PCB (FLT, DEMP, XSMT, FMT) are pre-configured by default; no extra GPIO connections are required.

## JSON Communication Protocol

The panel communicates with the main amplifier unit using a full-duplex asynchronous JSON protocol at **921,600 bps**, handling real-time telemetry, hardware setup diagnostic progress streams, and system state synchronization.

> [!IMPORTANT]
> **Initial Manual Flash Requirement for Upload Scripts**
> To use `tools/upload.py`, the ESP32-S3 Panel MUST be manually flashed via USB cable AT LEAST ONCE using `pio run -t upload`. Manual initial flashing places the Jacktor S3 Panel firmware signature onto the microcontroller, enabling `tools/upload.py` to automatically verify device identity via serial and prevent cross-flashing when both microcontrollers are plugged in simultaneously over USB.
