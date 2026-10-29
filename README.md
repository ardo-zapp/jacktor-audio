# Jacktor Audio - Smart Amplifier Ecosystem

Jacktor Audio is an audio amplifier ecosystem built on a single-node architecture. Core control, low-level DSP, and hardware protection execute on an **ESP32** microcontroller using native **ESP-IDF** APIs and FreeRTOS. User interaction, telemetry visualization, media controls, and network audio services run on an **Android Control Panel** (Realme C15 Qualcomm Bengal SM4250 / SM6115 running Android 14 GSI) via the systemless **Jacktor Audio Framework** root module.

---

## 1. System Architecture

```
+-------------------------------------------------------------------------+
|                  ANDROID CONTROL PANEL & HEAD UNIT                      |
|  - Platform   : Realme C15 (Qualcomm Snapdragon 460 / Bengal SM4250)    |
|  - System     : Android 14 (GSI / Treble, Linux Kernel 4.19.152-perf+)  |
|  - Root Core  : APatch (KernelPatch) / KernelSU / Magisk                |
|  - Driver KPM : usb-serial.kpm v1.0.0 (/dev/ttyUSB0..ttyUSB7)           |
|  - Framework  : Jacktor Audio Framework (Systemless Audio & WebUI)       |
|  - UI / Audio : Touchscreen UI, Port 80 WebUI, Bit-Perfect 384kHz/32-bit|
+-------------------------------------------------------------------------+
                                    |
                           USB OTG Host Cable
                       Silicon Labs CP2102 Bridge
                  Native TTY Node: /dev/ttyUSB0 (KPM)
                 High-Speed UART Stream: 921,600 bps
                                    |
+-------------------------------------------------------------------------+
|                     AMPLIFIER ECU UNIT (ESP32)                          |
|  - MCU        : ESP32-WROOM-32 (Xtensa Dual-Core @ 240 MHz)             |
|  - Framework  : Pure Espressif ESP-IDF Native (FreeRTOS, C++17)         |
|  - Core 0     : Continuous DMA ADC (44.1 kHz), 32-Band KissFFT Pipeline |
|  - Core 1     : Safety State Machine, Thermal PWM, Relays, RTC, TM1637  |
|  - Hardware   : TAS5805M / TAS5825M, SMPS Boost, PCM5102, DS18B20       |
+-------------------------------------------------------------------------+
```

### 1.1 Subsystem Overview
* **Amplifier Unit (ESP32-WROOM-32)**: Serves as the dedicated Electronic Control Unit (ECU). Handles continuous DMA analog-to-digital conversion for real-time Fast Fourier Transform (FFT) across GPIO 36 (Audio In R) and GPIO 39 (Audio In L), closed-loop thermal PWM fan regulation, over-temperature protection (OTP 85°C), SMPS and speaker relay sequencing, TM1637 7-segment display output, and DS3231 1 Hz hardware SQW timekeeping. Communicates asynchronously over USB Serial at **921,600 bps** using structured JSON telemetry frames.
* **Android Control Panel (Realme Bengal SM4250)**: Serves as the primary operator interface and audio endpoint. Manages system audio routing, Bit-Perfect output (up to 384 kHz / 32-bit), low-latency CPU governor tuning, and the local WebUI dashboard (Port 80) provided by the **Jacktor Audio Framework** module.
* **USB-to-Serial Kernel Driver (`usb-serial.kpm` v1.0.0)**: Because the stock vendor kernel lacks `CONFIG_USB_SERIAL_CP210X`, a freestanding KernelPatch Module (KPM) runs in kernel space to expose `/dev/ttyUSB0` through `/dev/ttyUSB7` with a default baud rate of **115,200 bps** and support for dynamic reconfiguration up to 3,000,000 bps (including 921,600 bps).

---

## 2. Repository Layout & Component Documentation

| Path | Description | Documentation |
| :--- | :--- | :--- |
| **[`firmware/amplifier/`](firmware/amplifier/)** | ESP32 Amplifier firmware source code, pinout tables, and communication protocol. | [Firmware Docs](firmware/amplifier/README.md) |
| **[`panel/realme-bengal/`](panel/realme-bengal/)** | Android platform adaptation for Realme C15 Bengal (SM4250/SM6115). | [Platform Docs](panel/realme-bengal/README.md) |
| **[`panel/realme-bengal/kpm/usb-serial/`](panel/realme-bengal/kpm/usb-serial/)** | KernelPatch Module source code & Makefile for `usb-serial.kpm` v1.0.0. | [Driver Docs](panel/realme-bengal/kpm/usb-serial/README.md) |
| **[`panel/realme-bengal/modul/`](panel/realme-bengal/modul/)** | Systemless root module directory for Magisk, KernelSU, and APatch. | [Module Docs](panel/realme-bengal/modul/README.md) |
| **[`panel/realme-bengal/modul/jacktor-audio-framework/`](panel/realme-bengal/modul/jacktor-audio-framework/)** | Standard root module payload (`META-INF/`, `customize.sh`, `kpm/`, `service.sh`, `config/`, `webroot/`, `system/`). | [Framework Docs](panel/realme-bengal/modul/jacktor-audio-framework/README.md) |
| **[`panel/app/`](panel/app/)** | Native Kotlin Multiplatform Android control panel application. | [App Docs](panel/app/README.md) |
| **[`tools/`](tools/)** | Build automation, flashers, and diagnostic consoles (`build.py`, `upload.py`, `cmd.py`, `sync_busybox.py`). | [Tools Docs](tools/README.md) |
| **[`docs/FFT_IMPLEMENTATION.md`](docs/FFT_IMPLEMENTATION.md)** | Technical analysis of the KissFFT audio spectrum pipeline. | [FFT Specs](docs/FFT_IMPLEMENTATION.md) |
| **[`docs/ERROR_CODES.md`](docs/ERROR_CODES.md)** | Hardware fault indicators and error codes. | [Error Codes](docs/ERROR_CODES.md) |

---

## 3. Kernel Driver: `usb-serial.kpm` (v1.0.0)

The Android panel communicates with the amplifier over USB OTG via the KernelPatch Module driver located in [`panel/realme-bengal/kpm/usb-serial/`](panel/realme-bengal/kpm/usb-serial/).

### Driver Characteristics
1. **Default Baud Rate (115,200 bps)**:
   - Matches standard Linux serial port behavior (`0x1cb2` on `init_termios`, `c_ispeed = 115200`, `c_ospeed = 115200`).
2. **Dynamic Line & Flow Configuration**:
   - **Baud Rate**: Decodes POSIX `CBAUD` (50 to 3,000,000 bps) and non-standard rates via `c_ospeed`. Directly dispatches `CP210X_SET_BAUDRATE` control packets.
   - **Data Bits**: Supports CS5, CS6, CS7, and CS8.
   - **Parity**: Supports None, Even, Odd, Mark, and Space (`PARENB`, `PARODD`, `CMSPAR`).
   - **Stop Bits**: Supports 1, 1.5, and 2 stop bits (`CSTOPB`).
   - **Flow Control**: Supports RTS/CTS hardware flow control (`CRTSCTS` -> `CP210X_SET_FLOW`).
   - **Break & Flush**: Implements `break_ctl` (`CP210X_SET_BREAK`) and `flush_buffer` (`CP210X_PURGE`).
3. **Dynamic Termios Offset Resolution**:
   - Scans for the 6-byte fingerprint of `tty_std_termios` (`c_line=0, VINTR=3, VQUIT=28, VERASE=127, VKILL=21, VEOF=4`) within safe memory bounds (`0x100`..`0x2c0`).
   - On the Realme Bengal kernel (`4.19.152-perf+`), resolved at offset `0x140` and cached for $O(1)$ runtime access.
4. **Multi-Device & Hotplug Safety**:
   - Supports up to 8 concurrent USB serial ports (`/dev/ttyUSB0` through `/dev/ttyUSB7`).
   - Uses exact Linux 4.19 slab flags (`GFP_KERNEL = 0x6000c0u`, `GFP_ATOMIC = 0x280020u`) and atomic in-flight I/O counters to avoid kernel panics during sudden physical cable disconnection.

---

## 4. Build Automation (`tools/build.py`)

A single build script compiles all project targets and outputs them to the `/build/` directory:

```bash
python3 tools/build.py
```

### Build Workflow
1. **Compiles Kernel Driver**: Builds `panel/realme-bengal/kpm/usb-serial/` using the AArch64 Musl toolchain.
2. **Generates Driver Artifacts**: Copies `usb-serial.kpm` to `build/usb-serial.kpm` and bundles it into `panel/realme-bengal/modul/jacktor-audio-framework/kpm/usb-serial.kpm`.
3. **Compiles ESP32 Firmware**: Runs PlatformIO to compile `firmware/amplifier` and outputs `build/amplifier.bin` along with partition tables and bootloaders.
4. **Packages Root Module**: Packages the unified `jacktor-audio-framework` directory into standard flashable ZIP archives: `build/jacktor-framework.zip` and `build/jacktor-audio-framework.zip`.

### Target Build Options
```bash
python3 tools/build.py --clean        # Clean all intermediate objects before building
python3 tools/build.py --kpm-only     # Build only usb-serial.kpm
python3 tools/build.py --fw-only      # Build only amplifier.bin
python3 tools/build.py --module-only  # Package only the Android module ZIP
```

---

## 5. Firmware Flashing (`tools/upload.py`)

The firmware flasher supports both direct USB connections and remote flashing over ADB:

```bash
# Interactive selection:
python3 tools/upload.py

# Direct USB to host machine:
python3 tools/upload.py usb

# Flash to ESP32 over Android USB OTG via ADB:
python3 tools/upload.py adb
```

### Remote ADB Flash Procedure
1. Verifies the presence of `/dev/ttyUSB0` on the connected Android panel.
2. Copies the static ARM64 `esputil` binary and compiled firmware images to `/data/local/tmp/jacktor_upload/`.
3. Executes the flash operation directly through `/dev/ttyUSB0` at **921,600 bps**:
   ```bash
   su -c '/data/local/tmp/jacktor_upload/esputil -b 921600 -p /dev/ttyUSB0 flash \
          0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 ota_data_initial.bin 0x10000 firmware.bin'
   ```

---

## 6. Diagnostic Console & TUI Monitor (`tools/cmd.py`)

`tools/cmd.py` is a terminal interface for telemetry observation and amplifier parameter adjustment at **921,600 bps**:

```bash
# Connect via direct USB:
python3 tools/cmd.py usb

# Connect via ADB over Android USB OTG:
python3 tools/cmd.py adb
```

* **Header**: Real-time power state, input mode (Bluetooth/AUX/USB), rail voltages, heatsink temperature, fan status, and runtime counter.
* **Telemetry Feed**: Continuous display of `rt` (~30 Hz) and `hz1` (~1 Hz) JSON data frames.
* **FFT Analyzer**: Stereo VU meters at footer; type `ana-page` to open the 16-band KissFFT Spectrum Analyzer overlay.
* **Boot Logs**: Type `rtos-log` inside the console to switch to 115,200 bps and view raw ESP-IDF boot logs.

---

## 7. Android Module Installation

### Flashing via ADB
```bash
adb push build/jacktor-framework.zip /data/local/tmp/
adb shell su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'

# Or on APatch:
adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'

adb reboot
```

### Live Script Synchronization (`tools/sync_busybox.py`)
To update scripts, WebUI files, or configurations on the active device without reflashing the ZIP or rebooting:
```bash
python3 tools/sync_busybox.py
```

---

## 8. License
* **Core Ecosystem & Firmware**: [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](LICENSE)
* **KernelPatch Driver (`usb-serial.kpm`)**: GNU General Public License v2.0 or later (GPL-2.0-or-later) per Linux kernel module requirements.
* **Copyright**: © 2026 Ardo Zapp & Jacktor Audio Team.
