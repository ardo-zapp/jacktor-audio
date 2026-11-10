# Jacktor Audio - Smart Amplifier Ecosystem

Jacktor Audio is an industrial-grade audio amplifier ecosystem built on a tightly integrated hybrid architecture. Core control, low-level DSP, real-time safety automation, and hardware protection execute on an **ESP32** microcontroller using native **ESP-IDF** APIs and FreeRTOS. User interaction, telemetry visualization, media controls, and network audio services run on an **Android Control Panel** (Realme C15 Qualcomm Bengal SM4250 / SM6115 running Android 14 GSI) via the systemless **Jacktor Audio Framework** root module.

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
|  - Audio Path : Direct ALSA Bit-Perfect (Up to 384 kHz / 32-bit)        |
+-------------------------------------------------------------------------+
                                     |
                       USB OTG Host Cable (Auto-Active)
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
|  - Hardware   : TAS5805M / TAS5825M, SMPS Boost, ADS1115, DS18B20       |
+-------------------------------------------------------------------------+
```

### 1.1 Subsystem Overview
* **Amplifier Unit (ESP32-WROOM-32)**: Serves as the dedicated Electronic Control Unit (ECU). Handles continuous DMA analog-to-digital conversion for real-time Fast Fourier Transform (FFT) across GPIO 39 (Audio In R) and GPIO 34 (Audio In L), closed-loop thermal PWM fan regulation, over-temperature protection (OTP 85°C), SMPS and speaker relay sequencing (Monitor GPIO 35), TM1637 7-segment display output, and DS3231 1 Hz hardware SQW timekeeping (GPIO 36). Communicates asynchronously over USB Serial at **921,600 bps** using structured JSON telemetry frames.
* **Android Control Panel (Realme Bengal SM4250)**: Serves as the primary operator interface and audio endpoint. Manages system audio routing, Direct ALSA Bit-Perfect output (up to 384 kHz / 32-bit), low-latency CPU governor tuning, and the local WebUI dashboard (Port 80) provided by the **Jacktor Audio Framework** module.
* **USB-to-Serial Kernel Driver (`usb-serial.kpm` v1.0.0)**: Because the stock vendor kernel lacks `CONFIG_USB_SERIAL_CP210X`, a freestanding KernelPatch Module (KPM) runs in kernel space to expose `/dev/ttyUSB0` through `/dev/ttyUSB7` with a default baud rate of **115,200 bps** and support for dynamic reconfiguration up to 3,000,000 bps (including 921,600 bps).

---

## 2. System Complexity & Engineering Depth

The Jacktor Audio platform is not a simple user-space application or audio wrapper; it is an industrial multi-tiered engineering stack bridging bare-metal silicon, real-time operating systems, Linux kernel internals, and high-performance native userspace services:

```
+-------------------------------------------------------------------------+
| LAYER 4: REACTIVE HEAD UNIT & USERSPACE CONTROL                         |
| Jetpack Compose Launcher | Port 80 WebUI | High-Speed SSE Telemetry     |
+-------------------------------------------------------------------------+
                                     |
+-------------------------------------------------------------------------+
| LAYER 3: NATIVE AARCH64 MUSL SYSTEM STACK                               |
| Static Binaries | Multi-Client serial-bridge (TCP 7777) | jacktor-api   |
+-------------------------------------------------------------------------+
                                     |
+-------------------------------------------------------------------------+
| LAYER 2: KERNEL-SPACE DRIVER & MEMORY INJECTION                         |
| Linux 4.19.152-perf+ | APatch KernelPatch (usb-serial.kpm)              |
| Live Termios Fingerprint Resolution | Atomic I/O Lock-Free Hotplug      |
+-------------------------------------------------------------------------+
                                     |
                         USB OTG Host Link @ 921,600 bps
                                     |
+-------------------------------------------------------------------------+
| LAYER 1: BARE-METAL REAL-TIME EMBEDDED ECU (ESP32 FreeRTOS)             |
| Core 0: 44.1 kHz DMA ADC + 32-Band KissFFT Pipeline                     |
| Core 1: FSM Safety Engine, ADS1115 16-bit ADC, DS18B20 1-Wire ROM,      |
|         DS3231 1 Hz SQW Hardware Colon Pacing, Thermal PWM Fan Control  |
+-------------------------------------------------------------------------+
```

### 2.1 Layer 1: Bare-Metal Real-Time Embedded ECU (ESP32-WROOM-32)
* **Deterministic Dual-Core Task Segregation**: Core 0 is strictly dedicated to audio signal processing, continuously acquiring analog audio samples via DMA-driven ADC (GPIO 34 and GPIO 39) at 44.1 kHz, feeding into a 32-band KissFFT pipeline with Hann windowing and peak decay estimation. Core 1 runs the supervisory safety state machine, communication handlers, thermal automation, and display pacing.
* **Multi-Sensor Bus Architectures**:
  - **I2C Bus**: Interfaces with an **ADS1115 16-bit 4-channel ADC** for continuous rail voltage telemetry (SMPS 65V rail, Standby rail, Logic 3.3V, and 5V rail) and an **RTC DS3231** high-precision temperature-compensated clock.
  - **1-Wire Bus (GPIO 27)**: Communicates with multi-drop **DS18B20 digital temperature sensors** mapped dynamically via unique 64-bit hardware ROM addresses to measure Stereo Amplifier, Subwoofer Amplifier, and SMPS power supply temperatures independently.
  - **Hardware SQW 1 Hz Colon Pacing**: Eliminates timer jitter by utilizing physical SQW square-wave interrupt pulses (GPIO 36) from the DS3231 RTC to drive the TM1637 7-segment display clock colon without software drift.
* **Fail-Safe Hardware Protection**: Active over-temperature trip (OTP 85°C), under-voltage lockout (UVLO), DC offset fault detection, sequential soft-start SMPS relay control, and speaker disconnect muting.
* **High-Speed Telemetry Link**: Continuous serialization of real-time audio FFT frames (`rt` at ~30 FPS) and hardware health telemetry (`hz1` at 1 FPS) across UART0 strictly at **921,600 bps**.

### 2.2 Layer 2: Kernel-Space Driver & Dynamic Memory Patching
* **Freestanding KernelPatch Module (`usb-serial.kpm`)**: Because the vendor kernel lacks in-tree CP210x driver support, a freestanding C driver runs directly in kernel space via KernelPatch (APatch).
* **Live Memory Fingerprint Scanning**: Dynamically discovers unexported kernel symbol structures (`tty_std_termios`) at runtime by scanning memory bounds (`0x100`..`0x2c0`) for a 6-byte POSIX termios fingerprint.
* **Crash-Resilient Hotplug Safety**: Employs atomic slab allocation flags (`GFP_ATOMIC` and `GFP_KERNEL`) and lock-free in-flight atomic I/O counters to guarantee zero kernel panics or pointer corruption upon sudden physical cable disconnection.
* **Custom Linux Kernel (4.19.152-perf+)**: Built specifically for Realme Bengal by Ardo Zapp, pre-patched with APatch (SuperKey: `ArdoZapp2026`) to deterministically force USB OTG Host Mode immediately after boot completion.

### 2.3 Layer 3: High-Performance Native AArch64 Musl Stack
* **Statically Compiled Musl C Binaries**: All core utilities (`amp-status`, `aplay`, `audio-test`, `clear`, `esputil`, `jacktor-api`, `jacktor-motd`, `otg-trigger`, `serial-bridge`, and `socat`) are compiled with Musl libc for minimal binary footprint, maximum execution speed, and zero external shared library dependencies.
* **Multi-Client Telemetry Multiplexer (`serial-bridge`)**: Eliminates serial contention on `/dev/ttyUSB0` by multiplexing the raw 921,600 bps stream across TCP Port 7777 and pseudo-terminal `/dev/ttyESP`. This permits simultaneous access by local daemons, developer CLI monitors (`tools/cmd.py`), WebUI clients, and the Android touch UI without dropping packets.
* **Unified REST & SSE Gateway (`jacktor-api` on Port 80)**: Serves WebUI assets and provides high-speed Server-Sent Events (`action=stream`) for telemetry alongside secure firmware upload and streaming flashing APIs (`action=flash_firmware`).

---

## 3. Direct ALSA Hardware Bypass vs. Third-Party Audio Mods

> [!WARNING]
> **Strict Compatibility Warning: Third-Party Audio Mods Are Prohibited**
> 
> The Android Control Panel environment **CANNOT** and **MUST NOT** be modified with third-party audio enhancement modules such as **Dolby Atmos, ViPER4Android, JamesDSP, AudioFX, or Sound Enhancement packages**.
>
> Any attempt to install these modules **will not work** (resulting in silent failure or no-op) because the Jacktor Audio architecture completely **bypasses Android's AudioFlinger framework**.
>
> Forcing such modifications to work requires dismantling the direct ALSA hardware pipeline and reverting back to AudioFlinger, which **drastically degrades audio quality, forces lossy downsampling, and introduces unacceptable latency**.

### 3.1 The Bit-Perfect Direct ALSA Pipeline
Traditional Android audio architectures route all audio playback through a heavy multi-layered stack:
`App` $\to$ `AudioTrack (Java)` $\to$ `AudioFlinger (C++)` $\to$ `Audio HAL` $\to$ `TinyALSA` $\to$ `Kernel ALSA Driver`.

Within this stock Android pipeline:
1. **Mandatory Software Resampling**: AudioFlinger automatically forces all media streams through its software resampler (typically locking the output to 48.0 kHz / 16-bit), regardless of the source audio resolution.
2. **Phase Smearing & Dynamic Compression**: Multiple software volume mixers, peak limiters, and gain controls introduce digital harmonic distortion and phase errors.
3. **Buffer Latency**: Multiple ring-buffer handoffs add dozens of milliseconds of delay, desynchronizing the audio playback from the ESP32 hardware FFT spectrum analyzer.

**The Jacktor Audio Architecture bypasses this entirely**:
* The system routes audio directly to the **Advanced Linux Sound Architecture (ALSA)** hardware PCM devices (`/dev/snd/pcmC*D*p`).
* Audio streams are transmitted **Bit-Perfect** at native sample rates up to **384 kHz / 32-bit**, preserving exact dynamic range, bit depth, and transient response without software tampering.

### 3.2 Why Dolby Atmos, ViPER4Android, and Audio Mods Fail
1. **AudioFlinger Dependency**: Third-party sound enhancement modules operate exclusively by hooking into the Android `audioserver` and registering software effect libraries within `audio_effects.xml` and `libeffect`.
2. **Zero Effect on ALSA**: Because Jacktor Audio bypasses AudioFlinger and talks directly to ALSA hardware nodes, **AudioFlinger effect libraries are never invoked**. The effects are completely inert (no-op).
3. **The Consequence of Forcing Audio Mods**:
   - Forcing audio back through AudioFlinger to enable these mods completely destroys the Bit-Perfect pipeline.
   - The audio signal is subjected to forced downsampling, digital saturation, clipping, and phase distortion.
   - High buffer latency ruins the real-time visual synchrony of the ESP32 hardware KissFFT spectrum analyzer and analog VU meters.

For these reasons, the Android Control Panel is strictly engineered as a clean, bit-perfect audiophile transport.

---

## 4. Repository Layout & Component Documentation

| Path | Description | Documentation |
| :--- | :--- | :--- |
| **[`firmware/amplifier/`](firmware/amplifier/)** | ESP32 Amplifier firmware source code, pinout tables, and communication protocol. | [Firmware Docs](firmware/amplifier/README.md) |
| **[`firmware/qcc5125/`](firmware/qcc5125/)** | Qualcomm QCC5125 Bluetooth co-processor firmware workspace & calibration status (WIP). | [QCC5125 Status](firmware/qcc5125/README.md) |
| **[`panel/realme-bengal/`](panel/realme-bengal/)** | Android platform adaptation for Realme C15 Bengal (SM4250/SM6115). | [Platform Docs](panel/realme-bengal/README.md) |
| **[`panel/realme-bengal/image/`](panel/realme-bengal/image/)** | Pre-patched partition images (`boot.img.xz`, `vbmeta.img.xz`) with APatch SuperKey `ArdoZapp2026`. | [Image Docs](panel/realme-bengal/README.md#2-partition-images-image) |
| **[`panel/realme-bengal/kpm/`](panel/realme-bengal/kpm/)** | KernelPatch Module source code & Makefile for `usb-serial.kpm` v1.0.0. | [Driver Docs](panel/realme-bengal/kpm/README.md) |
| **[`panel/realme-bengal/bin/`](panel/realme-bengal/bin/)** | Source code & Makefiles for ARM64 system binaries (`amp-status`, `aplay`, `audio-test`, `clear`, `esputil`, `jacktor-api`, `jacktor-motd`, `otg-trigger`, `serial-bridge`, and third-party `socat`). | [Binaries Docs](panel/realme-bengal/bin/README.md) |
| **[`panel/realme-bengal/modul/`](panel/realme-bengal/modul/)** | Systemless root module suite (`jacktor-audio-framework`) for Magisk, KernelSU, and APatch. | [Module Docs](panel/realme-bengal/modul/README.md) |
| **[`panel/app/`](panel/app/)** | Native Kotlin / Jetpack Compose Android control panel application. | [App Docs](panel/app/README.md) |
| **[`tools/`](tools/)** | Build automation, flashers, and diagnostic consoles (`build.py`, `upload.py`, `cmd.py`, `sync_module.py`, `discover.py`). | [Tools Docs](tools/README.md) |
| **[`docs/FFT_IMPLEMENTATION.md`](docs/FFT_IMPLEMENTATION.md)** | Technical analysis of the KissFFT audio spectrum pipeline. | [FFT Specs](docs/FFT_IMPLEMENTATION.md) |
| **[`docs/ERROR_CODES.md`](docs/ERROR_CODES.md)** | Hardware fault indicators and error codes. | [Error Codes](docs/ERROR_CODES.md) |

---

## 5. Kernel Driver: `usb-serial.kpm` (v1.0.0)

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

## 6. Build Automation ([`tools/build.py`](tools/build.py))

A single build script compiles all project targets and outputs them to the `/build/` directory:

```bash
python3 tools/build.py
```

### Build Workflow
1. **Compiles Kernel Driver**: Builds [`panel/realme-bengal/kpm/usb-serial/`](panel/realme-bengal/kpm/usb-serial/) using the AArch64 Musl toolchain (`build/usb-serial.kpm`).
2. **Compiles Native ARM64 Binaries**: Builds all command-line and daemon tools in [`panel/realme-bengal/bin/`](panel/realme-bengal/bin/) (`jacktor-api`, `serial-bridge`, `esputil`, `socat`, `amp-status`, etc.), installing them into `$MODDIR/system/bin/` and mirroring to `/build/`.
3. **Compiles ESP32 Firmware**: Runs PlatformIO to compile [`firmware/amplifier/`](firmware/amplifier/) and outputs `build/amplifier.bin` along with partition tables and bootloaders.
4. **Packages Root Module**: Packages the unified [`panel/realme-bengal/modul/jacktor-audio-framework/`](panel/realme-bengal/modul/jacktor-audio-framework/) directory into standard flashable ZIP archives: `build/jacktor-framework.zip` and `build/jacktor-audio-framework.zip`.

### Target Build Options
```bash
python3 tools/build.py --clean        # Clean all intermediate objects before building
python3 tools/build.py --kpm-only     # Build only usb-serial.kpm
python3 tools/build.py --bin-only     # Build all native ARM64 system binaries
python3 tools/build.py --api-only     # Build only jacktor-api
python3 tools/build.py --esputil-only # Build only esputil ARM64 binary
python3 tools/build.py --bridge-only  # Build only serial-bridge daemon
python3 tools/build.py --fw-only      # Build only amplifier.bin
python3 tools/build.py --module-only  # Package only the Android module ZIP
```

---

## 7. Firmware Flashing ([`tools/upload.py`](tools/upload.py))

The firmware flasher provides interactive target selection between direct local USB and remote Android panel flashing over Jacktor API strictly at **921,600 bps**:

```bash
# Interactive selection (1 = USB [DEFAULT], 2 = Panel - builds then uploads):
python3 tools/upload.py

# Build firmware only (skip upload):
python3 tools/upload.py --build-only
python3 tools/upload.py -b

# Upload only (skip compilation, use existing binary):
python3 tools/upload.py --upload-only
python3 tools/upload.py -u

# Flash to ESP32 over Android Panel via High-Speed Jacktor API @ 921600 bps:
python3 tools/upload.py panel
python3 tools/upload.py panel --upload-only

# Direct USB to host machine @ 921600 bps:
python3 tools/upload.py usb
python3 tools/upload.py usb --upload-only
```

### Remote Panel Flash Procedure (Jacktor API)
1. Discovers the Android panel dynamically on the local network (UDP beacon / subnet sweep via [`tools/discover.py`](tools/discover.py)).
2. Authenticates with encrypted credentials stored in `tools/config/credentials.enc` (`JacktorApiClient`).
3. Uploads `bootloader.bin`, `partitions.bin`, and `amplifier.bin` via HTTP POST (`action=upload_file`).
4. Executes real-time flash streaming via `action=flash_firmware`:
   - Jacktor API acquires exclusive port lock (`/data/local/tmp/jacktor_flashing.lock`).
   - Temporarily stops the `serial-bridge` daemon to free `/dev/ttyUSB0`.
   - Runs `esputil` strictly at **921,600 bps**, streaming byte-by-byte and percentage progress in real-time.
   - Restarts `serial-bridge` and releases lock once flashing is complete.

---

## 8. Diagnostic Console & TUI Monitor ([`tools/cmd.py`](tools/cmd.py))

`tools/cmd.py` is a terminal interface for telemetry observation and amplifier parameter adjustment at **921,600 bps**:

```bash
# Interactive selection (1 = USB [DEFAULT], 2 = Panel):
python3 tools/cmd.py

# Connect via Panel (High-Speed Jacktor API Server-Sent Events Port 80):
python3 tools/cmd.py panel

# Connect via direct local USB port:
python3 tools/cmd.py usb

# Connect directly to specific TCP port or serial node:
python3 tools/cmd.py tcp:<panel_ip>:7777
python3 tools/cmd.py /dev/ttyUSB0
```

* **Header**: Real-time power state, input mode (Bluetooth/AUX/USB), rail voltages, heatsink temperature, fan status, and runtime counter.
* **Telemetry Feed**: Continuous display of `rt` (~30 Hz) and `hz1` (~1 Hz) JSON data frames.
* **Clean Segmented VU Meter**: Clear horizontal stereo LED bars (`■`) with independent Left vs Right channel styling without confusing vertical divider lines.
* **FFT Analyzer**: Type `ana-page` to open the 16/32/64-band KissFFT Spectrum Analyzer overlay with stereo channel separation (active exclusively during runtime mode).
* **High-Speed Jacktor API Streaming (`ApiSerialBridge`)**: Streams telemetry via HTTP SSE (`/cgi-bin/jacktor-api?action=stream`) over Port 80, eliminating serial port contention and allowing WebUI, Android apps, and developer consoles to stream telemetry simultaneously.
* **Boot Logs**: Type `rtos-log` inside the console to switch to 115,200 bps and view raw ESP-IDF boot logs.

---

## 9. Android Module Installation & Live Sync

### Flashing via Magisk / APatch
Transfer `build/jacktor-framework.zip` to the Android panel (via SCP or file transfer) and install:
```bash
# On Magisk:
su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'

# Or on APatch:
su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'
```

### Live Module & BusyBox Synchronization ([`tools/sync_module.py`](tools/sync_module.py))
To update scripts, WebUI files, binaries, or configurations on the active device over SSH without reflashing the ZIP or rebooting:
```bash
# Push modified module files & WebUI:
python3 tools/sync_module.py

# Force re-sync all module files:
python3 tools/sync_module.py --force

# Sync BusyBox NDK binary and re-generate symlinks safely:
python3 tools/sync_module.py --busybox
```

### Audio Engine & System Policy Highlights
* **Fixed Quality with Hardware Bit-Depth**: WebUI supports high-resolution audio formats labeled with bit-depth (e.g., `48.0 kHz / 16-bit`, `48.0 kHz / 24-bit`, `96.0 kHz / 24-bit`).
* **Per-Device Audio Profiles**: Internal Speaker, 3.5mm Headset, and USB DAC endpoints retain independent resampler modes and fixed quality settings.
* **Master Volume Stream Isolation**: Hardware master volume slider exclusively controls `STREAM_MUSIC` (stream 3), leaving notification, ringtone, and alarm volume levels untouched.
* **Standby Track Display**: When no media is playing, track details display cleanly as `"-"` (retaining track information when paused).

---

## 10. License
* **Core Ecosystem, Firmware & Native Android Binaries (`amp-status`, `aplay`, `audio-test`, `clear`, `esputil`, `jacktor-api`, `jacktor-motd`, `otg-trigger`, `serial-bridge`)**: [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](LICENSE) — Authored or adapted by Ardo Zapp. (`esputil` adapted from `cpq/esputil` by Cesanta Software Limited under MIT sublicensing).
* **KernelPatch Driver (`usb-serial.kpm`)**: Authored by Ardo Zapp under **GNU General Public License v2.0 or later (GPL-2.0-or-later)** for Linux kernel compliance.
* **Ported Third-Party Utilities (`socat`, `busybox`)**: `socat` originally authored by Gerhard Rieger and contributors ([`dest-unreach.org`](http://www.dest-unreach.org/socat/)), ported and compiled for standalone AArch64 Musl static target by Ardo Zapp under **GNU General Public License v2.0 (GPL-2.0-only)**. `busybox` subject to GPL-2.0-only by upstream authors (see [LICENSE](LICENSE) and [`panel/realme-bengal/bin/README.md`](panel/realme-bengal/bin/README.md)).
* **Copyright**: © 2026 Ardo Zapp & Jacktor Audio Team.
