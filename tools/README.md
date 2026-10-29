# Jacktor Audio - Developer Tools & Utilities

This directory contains cross-platform build automation tools, diagnostic consoles, and flashing scripts for the Jacktor Audio ecosystem.

---

## 1. Tool Summary

| Tool | Target Environment | Description |
| :--- | :--- | :--- |
| **[`build.py`](build.py)** | Linux Host | **Unified Project Build Tool**: Compiles the KPM kernel driver (`usb-serial.kpm`), ESP32 amplifier firmware (`amplifier.bin`), and packages the systemless root module (`jacktor-framework.zip`) into `/build/`. |
| **[`cmd.py`](cmd.py)** | Linux, macOS, Windows | **Interactive 3-Zone Curses TUI Monitor**: Real-time telemetry, 16-band spectrum analyzer (`ana-page`), stereo VU meters, diagnostic logs, and command console at 921,600 bps. |
| **[`upload.py`](upload.py)** | Linux, macOS, Windows | **Firmware Flasher**: Compiles and flashes the ESP32 Amplifier firmware either **directly via USB** or **remotely via ADB to the Android Panel**. |
| **[`sync_busybox.py`](sync_busybox.py)** | Host PC (via ADB) | **Live Module Synchronizer**: Pushes updated scripts, binaries, and BusyBox applets to the running Android panel without requiring a reboot. |
| **[`ota_push.py`](ota_push.py)** | Cross-Platform | **OTA Firmware Deployer**: Sends compiled firmware binaries (`.bin`) over HTTP to microcontrollers on the local network. |
| **[`install_pio.py`](install_pio.py)** | Linux & Windows | **PlatformIO Core Installer**: Automated installer script downloading official `get-platformio.py` and configuring the environment. |
| **[`run.sh`](run.sh)** | Linux / macOS | **Console Launcher**: Shell shortcut to launch `cmd.py`. |

---

## 2. Unified Build Automation (`build.py`)

`build.py` automates the compilation and packaging of all project components (excluding the Android application APK) in a single command.

### Usage
```bash
# Build complete project (KPM, ESP32 firmware, and flashable module ZIP):
python3 tools/build.py

# Perform a clean build of all components:
python3 tools/build.py --clean

# Build individual targets:
python3 tools/build.py --kpm-only     # Build only usb-serial.kpm
python3 tools/build.py --fw-only      # Build only amplifier.bin
python3 tools/build.py --module-only  # Package only the Android module ZIP
```

### Build Steps Executed
1. **Kernel Driver (`usb-serial.kpm`)**:
   - Compiles `panel/realme-bengal/kpm/usb-serial/` using `aarch64-linux-musl-gcc`.
   - Copies the output binary to `build/usb-serial.kpm`.
   - Copies the driver into `panel/realme-bengal/modul/jacktor-audio-framework/kpm/usb-serial.kpm`.
2. **Amplifier Firmware (`amplifier.bin`)**:
   - Executes PlatformIO to compile `firmware/amplifier`.
   - Copies `firmware.bin` to `build/amplifier.bin`.
   - Collects auxiliary boot images: `bootloader.bin`, `partitions.bin`, and `ota_data_initial.bin`.
3. **Systemless Android Module (`jacktor-framework.zip`)**:
   - Validates the presence of `kpm/usb-serial.kpm`.
   - Preserves UNIX symlinks and file permissions (`0755` for scripts/binaries, `0644` for configs).
   - Generates `build/jacktor-framework.zip` and `build/jacktor-audio-framework.zip`.

---

## 3. Interactive Terminal Monitor (`cmd.py`)

`cmd.py` provides a curses-based terminal user interface (TUI) for real-time telemetry streaming and parameter adjustments with the ESP32 Amplifier ECU at **921,600 bps**.

### Connection Selection
```bash
# Launch interactive mode (prompts for Direct USB or via ADB):
python3 tools/cmd.py

# Direct local USB (/dev/ttyUSB0):
python3 tools/cmd.py usb

# Via ADB over Android USB OTG (/dev/ttyUSB0 on Android panel):
python3 tools/cmd.py adb

# Explicit port or ADB serial bridge:
python3 tools/cmd.py /dev/ttyUSB0
python3 tools/cmd.py adb:192.168.1.11:5555:/dev/ttyUSB0
```

### Key Functions
* **Status Header**: Displays real-time power state, active input source, rail voltages, heatsink temperature, fan state, and total system runtime.
* **Telemetry Log**: Real-time display of incoming and outgoing JSON frames at ~30 Hz.
* **Audio Analysis**: Real-time stereo VU meters and full 16-band KissFFT Spectrum Analyzer overlay (access with `ana-page`).
* **ESP-IDF Boot Logs**: Type `rtos-log` inside the console to switch baud rate to 115,200 bps and stream raw boot logs.

---

## 4. Firmware Flasher (`upload.py`)

`upload.py` manages the compilation and flashing process of the ESP32 Amplifier firmware.

### Usage
```bash
# Interactive mode:
python3 tools/upload.py

# Direct USB to host machine:
python3 tools/upload.py usb

# Flash via ADB over Android USB OTG:
python3 tools/upload.py adb
```

### Remote ADB Workflow
1. Verifies `/dev/ttyUSB0` on the Android panel.
2. Pushes the static ARM64 `esputil` tool and firmware binaries to `/data/local/tmp/jacktor_upload/`.
3. Flashes the ESP32 at **921,600 bps**:
   ```bash
   su -c '/data/local/tmp/jacktor_upload/esputil -b 921600 -p /dev/ttyUSB0 flash \
          0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 ota_data_initial.bin 0x10000 firmware.bin'
   ```

---

## 5. Live Module Synchronization (`sync_busybox.py`)

When modifying scripts or system binaries in `panel/realme-bengal/modul/jacktor-audio-framework/`:

```bash
python3 tools/sync_busybox.py
```
This utility:
1. Pushes updated BusyBox NDK binaries to `/data/adb/modules/jacktor-audio-framework/system/bin/busybox`.
2. Sets executable permissions (`0755`).
3. Scans `/system/bin` on the device and recreates non-conflicting applet symlinks dynamically.
