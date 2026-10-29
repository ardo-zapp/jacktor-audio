# Realme Bengal (SM4250 / SM6115) Panel Adaptation

This directory contains the hardware and kernel adaptation configuration for the **Realme C15 Qualcomm Edition** (`bengal` / `RMX2195`, Snapdragon 460 / SM4250 and Snapdragon 662 / SM6115) running **Android 14 GSI**, utilized as the central control panel and audio head unit for the Jacktor Audio ecosystem.

---

## 1. System Role & Topology

```
+-------------------------------------------------------------------------+
|                  REALME C15 ANDROID PANEL (Bengal SM4250)                |
|  - Operating System  : Android 14 (GSI / Treble, Kernel 4.19.152-perf+)  |
|  - Root Core         : APatch / KernelSU / Magisk                        |
|  - Kernel Driver     : usb-serial.kpm v1.0.0 (/dev/ttyUSB0..7)          |
|  - System Module     : Jacktor Audio Framework (Systemless Audio/WebUI)  |
|  - User Interface    : On-screen Touch App + Port 80 WebUI              |
+-------------------------------------------------------------------------+
                                    |
                           USB OTG Host Cable
                                    |
+-------------------------------------------------------------------------+
|                      AMPLIFIER ECU (ESP32-WROOM-32)                     |
|  - UART0 Interface   : 921,600 bps full-duplex JSON telemetry           |
|  - Hardware Engine   : TAS5805M / TAS5825M, SMPS, 32-Band FFT, Relays    |
+-------------------------------------------------------------------------+
```

---

## 2. Directory Layout

| Directory | Description |
| :--- | :--- |
| **[`kpm/usb-serial/`](kpm/usb-serial/)** | **KernelPatch Module (KPM)**: USB-to-Serial converter driver providing native `/dev/ttyUSB0` through `/dev/ttyUSB7` support for CP210x devices with 115,200 bps default baud rate and dynamic termios negotiation. |
| **[`modul/jacktor-audio-framework/`](modul/jacktor-audio-framework/)** | **Standard Root Module**: Systemless root module containing installer hooks (`META-INF/`, `customize.sh`, `kpm/`), runtime service (`service.sh`), WebUI server (`webroot/`), centralized settings (`config/`), and system overlays (`system/`). |

---

## 3. Installation Guide

### Step 1: Install APatch or KernelSU / Magisk
The Android 14 GSI environment must be rooted. For persistent KPM kernel driver loading across reboots, **APatch** (KernelPatch) is recommended:
1. Install the APatch manager application.
2. Flash the patched boot image containing KernelPatch for kernel 4.19.

### Step 2: Build the Artifacts
Run the unified build tool from the project root:
```bash
python3 tools/build.py
```
This generates:
* `build/usb-serial.kpm`
* `build/jacktor-framework.zip`
* `build/amplifier.bin`

### Step 3: Install Driver (`usb-serial.kpm`)
The driver is automatically installed when flashing `build/jacktor-framework.zip` on APatch. Alternatively, to install or update the driver manually:
```bash
adb push build/usb-serial.kpm /data/local/tmp/
adb shell su -c 'mkdir -p /data/adb/ap/kpm/usb-serial && \
                 cp /data/local/tmp/usb-serial.kpm /data/adb/ap/kpm/usb-serial/usb-serial.kpm && \
                 chmod 644 /data/adb/ap/kpm/usb-serial/usb-serial.kpm'
adb reboot
```

### Step 4: Flash the Framework Module
Flash `build/jacktor-framework.zip` via Magisk or APatch:
```bash
adb push build/jacktor-framework.zip /data/local/tmp/
adb shell su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'

# Or on APatch:
adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'

adb reboot
```

---

## 4. Runtime Services

Once initialized during boot via `service.sh`:
* **WebUI Dashboard**: Served locally or over Wi-Fi at `http://<device-ip>/` (Port 80) via BusyBox `httpd`.
* **Bit-Perfect Audio Engine**: Adjusts audio configuration to enforce 384 kHz / 32-bit output and adds audio processes to the Android `deviceidle` whitelist.
* **CPU & I/O Tuning**: Applies CPU governor tuning and memory management parameters according to settings in `config/`.
* **USB OTG Serial Monitor**: Monitors `/dev/ttyUSB0` availability and ensures correct node permissions (`0666`) for unprivileged userspace access.
