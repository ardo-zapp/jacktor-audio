# Jacktor Audio - Smart Amplifier Ecosystem

Jacktor Audio is a smart amplifier ecosystem using a *Single-Node* architecture. This system relies on a centralized control and audio signal processing engine on the ESP32 microcontroller built on the **ESP-IDF** framework, communicating directly with an Android application and ESP32-S3 control panel visual interface. This repository covers all integrated ESP32 firmware components, documentation, and supporting utilities.

## System Architecture

- **Amplifier Unit (ESP32)**: Serves as the low-level Electronic Control Unit (ECU). Built on ESP-IDF & FreeRTOS. Handles I2S Analog-to-Digital conversion for Fast Fourier Transform (FFT) across GPIO 36 (Audio In R) and GPIO 39 (Audio In L), thermal Pulse Width Modulation (PWM) fan control, over-temperature protection (OTP 85°C), power and audio relay management (Relay SMPS & Relay Audio), TM1637 7-segment display status feedback, and high-precision DS3231 1 Hz hardware SQW-paced timing. Communicates asynchronously via built-in USB Serial and UART2 (921,600 bps) with JSON format payloads straight to the S3 panel bridge and mobile applications.
- **Control Panel (ESP32-S3)**: Serves as the primary User Interface & Control Panel. Features a 3.2" ILI9341 display with XPT2046 touch, an integrated **GY-PCM5102 32-Bit 384 kHz I2S DAC**, an onboard **2-Player NES Nofrendo Emulator** with PSRAM double-buffering, a **USB DAC Mode App ("Jacktor Audio")**, an **LCD 1602 I2C Karaoke Lyrics Display (SDA GPIO 1, SCL GPIO 2)**, and a **System Settings App**.

## Dependencies & Integrated Libraries

The Jacktor Audio system incorporates production-grade external packages and zero-overhead internal component libraries optimized for pure ESP-IDF execution:

### External Dependencies (Managed via ESP Component Registry & PlatformIO)
1. **ESP-IDF v6+ Native Framework**: Base operating system and hardware abstraction layer (FreeRTOS, I2S, LEDC PWM, SPI DMA, UART, NVS, I2C Master drivers).
2. **cJSON (`espressif/cjson: ^1.7.18`)**: High-performance C JSON parser/serializer used across Amplifier telemetry and S3 Panel communication streams.
3. **LVGL Graphics Engine (`lvgl/lvgl: ^8.4.0`)**: Lightweight GUI graphics library powering the 3.2" TFT LCD panel UI with custom Android-style design tokens.

### Embedded Internal Component Libraries (`firmware/panel/components/`)
1. **`micro_flac` Component** (`firmware/panel/components/micro_flac/`): Standalone ESP-IDF FLAC decoder component (`esphome-libs/micro-flac`).
2. **`micro_mp3` Component** (`firmware/panel/components/micro_mp3/`): Standalone ESP-IDF MP3 frame decoder component (`esphome-libs/micro-mp3`).
3. **`nofrendo` Component** (`firmware/panel/components/nofrendo/`): Standalone ESP-IDF NES emulator component optimized for 60 FPS rendering with PSRAM frame buffering.
4. **KissFFT (v131.2.0)** (`firmware/amplifier/include/kissfft/`): Real-to-complex Fast Fourier Transform DSP library.
5. **Storage Access Framework (SAF) Manager** (`driver/saf_manager.h`): Unified file system abstraction supporting SD Card (SDSPI), USB Mass Storage, and SMB network shares with FAT32/exFAT formatting.
6. **PSRAM Stream & Metadata Cache Engine** (`driver/psram_cache.h`): 32KB ring-buffer caching mechanism for zero-buffering network audio streams, album covers, and lyrics.
7. **PCF8574 LCD 1602 I2C Driver** (`driver/lcd1602_i2c.h`): Native HD44780 4-bit initialization and rendering driver for external karaoke lyrics display over I2C (SDA GPIO 1, SCL GPIO 2).
8. **UTF-8 Sanitizer Utility** (`driver/utf8_utils.h`): Multi-byte string validator handling CJK, Cyrillic, Arabic, Latin-Extended, symbols, and emojis across LVGL components.

## Feature Highlights

1. **USB DAC Mode App ("Jacktor Audio")**:
   - **Plug-and-Play USB Audio Class 2.0**: Recognized as **"Jacktor Audio"** on PC and mobile devices, supporting up to 32-bit / 384 kHz PCM playback.
   - **Automated Lifecycle**: Automatically launches the USB DAC app on USB host connection and closes upon disconnection.
   - **Audio Visualizers**: Features a real-time Mono Audio Spectrum Analyzer and Stereo L/R VU Meter bars with visualizer source selection (USB DAC vs GPIO ADC).
   - **Hardware Interlocks**: Automatically triggers Relay Audio (GPIO 26) and overrides the main amplifier TM1637 display to show `"usb"`.

2. **2-Player NES Emulator with PSRAM Double-Buffering**:
   - **Nofrendo Core**: Pure ESP-IDF C/C++ NES emulation running on FreeRTOS at 60 FPS.
   - **SD Card ROM Loader**: Automatically creates `/sdcard/NES` and scans `.nes` games with friendly empty-state prompts.
   - **2-Player DualShock 2 Support**: Supports two simultaneous players with custom button remapping stored persistently in NVS.
   - **Zero Screen Flicker**: Uses PSRAM frame buffering and direct ILI9341 SPI DMA pixel transfers.
   - **NES Menu BGM & Audio Relay**: Synthesizes 8-bit NES chiptune menu music via GY-PCM5102 I2S DAC and activates Relay Audio.
   - **In-Game Exit Shortcuts**: Semi-transparent 5-second touch overlay bar and `SELECT + START` 1-second gamepad hotkey.

3. **System Settings App & Home Interface**:
   - **System Status Bar**: Live time, supply voltage rails (STBY 12V, VDD 3.3V, ESP 5V), heatsink/RTC temperatures, and input mode badge (`[ BT ]` / `[ AUX ]`).
   - **Categorized Settings Manager**: Multi-tab interface for Amplifier Settings (Power, Fan Presets, BT Auto-off, SMPS Cutoff/Recovery), Panel Display (Brightness slider, Sleep timeout), SD Card Management, Gamepad Mapping, and Device Info.

4. **Professional Audio & Amplifier ECU**:
   - **Advanced Pro Telemetry & Logging**: Tracks Hour Meter (total runtime), heatsink Peak Temperature, and persists Fault History in NVS.
   - **Real-Time Spectrum Analyzer & Stereo VU Meters**: Native C++ KissFFT DSP pipeline with Hann windowing, dynamic DC offset removal, and strict squelch gating on GPIO 36 (Audio In R) and GPIO 39 (Audio In L).
   - **Professional Protection Suite**: Speaker Protector stability check (GPIO 34), SMPS hardware fault detect (GPIO 15), thermal throttling, QCC power detection (GPIO 5), and clipping detector.
   - **Smart Power & TM1637 Display**: Live state feedback (`"on"`, `"pass"`, `"err"`, `"off"`, `"bt"`, `"aux"`, `"usb"`, and HH:MM standby clock with 1 Hz SQW-synchronized colon blinking).
   - **Master Synchronization**: Panel operates as the master authority, automatically synchronizing parameters to the ESP32 firmware upon connection.

## Understanding Project Complexity for Beginners

For developers or audio enthusiasts exploring this repository, this project is a production-grade embedded system engineered on pure Espressif **ESP-IDF v6+** native APIs using C++17:

1. **Dual-Core FreeRTOS Multi-Tasking**:
   - **Core 0 (Dedicated DSP Engine)**: Runs continuous DMA-backed I2S audio ingestion at 44.1 kHz, feeding a KissFFT real-to-complex pipeline without interrupting control tasks.
   - **Core 1 (System ECU & Safety Engine)**: Runs power relay state machines, sensor telemetry polling (DS18B20 1-Wire, DS3231 RTC, ADS1115 Voltmeter), thermal PID fan loops, and optocoupler fault interlocks concurrently.
2. **Asynchronous Non-Blocking Communication Protocol**:
   - Full-duplex JSON telemetry stream over 921,600 bps high-speed UART and USB CDC.
   - Built-in ring-buffering, ACK response handlers, and command parser matching Linux-style argument standards (`tone`, `deviceinfo`, `selftest`).
3. **Hardware-Level Protection Interlocks**:
   - Multi-tiered power sequencing: soft-start delay loops, optocoupler SMPS/speaker protector fault latching, over-voltage shutdown, and post-power-off thermal dissipation fan control.
4. **Pure Native ESP-IDF Architecture**:
   - Zero Arduino framework overhead. Physical GPIO, PWM LEDC, I2C, SPI, and I2S peripherals are configured directly via native Espressif drivers.

## Directory Structure & Documentation
- [`docs/NES_EMULATOR.md`](docs/NES_EMULATOR.md) — Technical documentation of the ESP32-S3 NES Emulator core, SD card ROM setup, 2-player controller mapping, and audio architecture.
- [`docs/FFT_IMPLEMENTATION.md`](docs/FFT_IMPLEMENTATION.md) — Technical documentation of the KissFFT audio spectrum analyzer pipeline and hardware audio input stage.
- [`firmware/amplifier/README.md`](firmware/amplifier/README.md) — Detailed specifications of GPIO mapping, JSON communication protocols, and safety/protection features.
- [`firmware/panel/README.md`](firmware/panel/README.md) — Detailed specifications of the ESP32-S3 control panel, PCM5102 I2S DAC, USB DAC mode, and user interface engine.
- [`tools/`](tools) — Utility scripts (`cmd.py`, `upload.py`, `ota_push.py`) to aid in local compilation and setup with PlatformIO Core.

> [!IMPORTANT]
> **Initial Manual Flash Requirement for Upload Scripts**
> Before using the automated upload script (`tools/upload.py`), both the ESP32 Amplifier and ESP32-S3 Panel **MUST be manually flashed via USB cable AT LEAST ONCE** using PlatformIO (`pio run -t upload`). The initial manual flash places the Jacktor Audio firmware signature on the device, allowing the Python script to safely verify device identities and avoid cross-flashing when both boards are connected via USB simultaneously.

## Terminal Monitor Tool (`tools/cmd.py`)

A responsive 3-zone curses TUI monitor is provided in `tools/cmd.py` for real-time monitoring and debugging:
1. **Fixed Compact Header**: Max 60-column width displaying real-time power, input source, voltage, temperature, fan mode, runtime, error status, and button lock.
2. **Scrolling Live Log Area**: Middle area for real-time `TX`, `RX`, `ACK`, and `LOG` telemetry frames.
3. **Interactive On-Demand Help**: Type `help` or `help <category>` (e.g. `help gaia`, `help power`, `help smps`, `help ana`) to view commands.
4. **Fixed Footer & Real-Time Analyzer Screen**: Displays Stereo VU Meter bars (`VU L` & `VU R`) at the bottom footer and supports opening a full 16-band real-time Audio Analyzer screen overlay (`ana-page`).

## PlatformIO Installation & IDE Integration Guide

### Do you need a separate PlatformIO Core (CLI) installation when using VSCode?
> [!NOTE]
> **NO, a separate PlatformIO Core (CLI) installation IS NOT REQUIRED if you use PlatformIO IDE for Visual Studio Code.**
> The **PlatformIO IDE extension for VSCode** includes a built-in portable PlatformIO Core (CLI). You can access the `pio` command directly inside VSCode by clicking the **Terminal icon** on the bottom PlatformIO Toolbar or via **Activity Bar > PlatformIO > Quick Access > Miscellaneous > PlatformIO Core CLI**.

---

### Installing PlatformIO Core (CLI) on Linux & Windows

If you prefer using standalone command-line terminals (Terminal, Git Bash, CMD, PowerShell) or building without VSCode, you can install PlatformIO Core using the provided automated script or official installer:

#### Option A: Automated Installation Script (`tools/install_pio.py`)
Run the automated Python helper script included in this repository:
```bash
python3 tools/install_pio.py
```
This script downloads the official `get-platformio.py` installer from GitHub and sets up the Python virtual environment in `~/.platformio/penv/bin` (Linux/macOS) or `~/.platformio/penv/Scripts` (Windows).

#### Option B: Standalone PlatformIO Installation
1. **Linux**:
   ```bash
   # Install python3-venv prerequisite
   sudo apt update && sudo apt install -y python3-venv python3-pip wget

   # Download and run official installer
   wget -O get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
   python3 get-platformio.py
   ```
2. **Windows**:
   ```cmd
   # Download get-platformio.py and execute via Python
   python.exe get-platformio.py
   ```

---

## Utility Scripts Documentation (`tools/`)

The [`tools/`](tools) directory contains cross-platform Python scripts to simplify development, hardware diagnostics, and firmware flashing:

| Script File | Target Platform | Description & Key Functions |
| :--- | :--- | :--- |
| **`tools/install_pio.py`** | Linux & Windows | **Automated PlatformIO Installer**: Downloads the official `get-platformio.py` installer from the PlatformIO Core GitHub repository, executes it via Python without requiring root/admin privileges, and configures environment `PATH` variables across OSes. |
| **`tools/upload.py`** | Linux & Windows | **Unified Hardware Flash Tool**: Interactive cross-platform flash utility that presents a menu to select between **Amplifier (ESP32) [Option 1, Default]** and **Panel (ESP32-S3) [Option 2]**. Probes connected USB serial ports using single-attempt DTR/RTS resets and JSON telemetry signatures (`JACKTOR AUDIO` vs `jacktor_panel`) to prevent accidental cross-flashing when both microcontrollers are plugged in simultaneously. |
| **`tools/cmd.py`** | Linux & Windows | **Interactive Curses TUI Monitor**: 3-zone terminal user interface providing real-time telemetry headers (voltage, temperature, fan speed, hour meter, errors), live scrolling `TX`/`RX`/`ACK` log streams, categorized help (`help power`, `help gaia`, `help smps`), and full-screen 16-band audio spectrum analyzer overlays (`ana-page`). Supports `windows-curses` on Windows. |
| **`tools/ota_push.py`** | Cross-Platform | **Over-The-Air (OTA) Gateway Push Tool**: Uploads compiled firmware binaries (`.bin`) over HTTP API gateway services directly to target microcontrollers over Wi-Fi/Network. |

---

## Installation & Flashing via Command Line

### 1. Manual & Automated USB Flashing (Local)
To flash target boards using the unified upload utility:

```bash
# Launch interactive flash tool (selects Option 1 Amplifier by default):
python3 tools/upload.py

# Or specify target directly via CLI arguments:
python3 tools/upload.py amp    # Flash ESP32 Amplifier
python3 tools/upload.py panel  # Flash ESP32-S3 Panel
```

Alternatively, build and flash directly using PlatformIO CLI:
```bash
cd firmware/amplifier
pio run -t upload
```

### 2. Automatic OTA Flashing (via Mobile API)

Over-The-Air (OTA) updates allow pushing compiled binaries directly to the device over HTTP API gateway services (`tools/ota_push.py`):

```bash
# Push firmware OTA automatically:
python3 tools/ota_push.py --target firmware/amplifier --ip 192.168.1.100 --port 8080
```
