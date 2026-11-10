# Realme Bengal (SM4250 / SM6115) Panel Adaptation

> [!IMPORTANT]
> **Device Compatibility Notice**:
> This project currently **STRICTLY AND EXCLUSIVELY supports Realme C15 Qualcomm Edition (`bengal` / `RMX2195`)** powered by Qualcomm Snapdragon 460 (SM4250) or Snapdragon 662 (SM6115). MediaTek variants or other device models are NOT supported.

This directory contains the hardware, partition images, and kernel adaptation configuration for the **Realme C15 Qualcomm Edition** running **Android 14 GSI**, utilized as the central control panel and audio head unit for the Jacktor Audio ecosystem.

---

## 1. System Requirements & Specifications

| Component | Requirement / Specification | Notes |
| :--- | :--- | :--- |
| **Target Device** | **Realme C15 Qualcomm Edition** (`bengal` / `RMX2195`) | Snapdragon 460 (SM4250) / Snapdragon 662 (SM6115) |
| **Mandatory Base Firmware** | **Realme UI 2.0 (Version C.06)** | Mandatory Android 11 vendor firmware base for Treble GSI compatibility |
| **Operating System** | **Android 14 GSI (Project Treble)** | A/B 64-bit Treble system image (arm64-ab) |
| **Kernel Version** | **Linux 4.19.152-perf+** | Built & adapted by **Ardo Zapp** (`ardo@jacktor`) |
| **Root & Kernel Patching** | **APatch (KernelPatch)** | Pre-patched into `boot.img` with **SuperKey: `ArdoZapp2026`** |
| **OTG Host Engine** | Mandatory custom kernel + `jacktor-audio-framework` | Automatically enables OTG Host mode post-boot for CP2102 bridge |
| **UART Telemetry Link** | **921,600 bps** (Strict) | Multiplexed via `serial-bridge` (TCP Port 7777 / `/dev/ttyUSB0`) |

---

## 2. Partition Images (`image/`)

Pre-patched partition images are stored in [`panel/realme-bengal/image/`](image/) and compressed with `xz -9k` to preserve bandwidth and repository integrity:

| Image Archive | Uncompressed Size | Description |
| :--- | :---: | :--- |
| **[`boot.img.xz`](image/boot.img.xz)** | ~96 MB | **APatch Patched Boot Image**: Contains the mandatory custom `4.19.152-perf+` kernel by Ardo Zapp patched with APatch (KernelPatch) using **SuperKey: `ArdoZapp2026`**. This kernel is strictly required to reliably activate and maintain USB OTG Host Mode right after boot completion, supported and assisted by `jacktor-audio-framework`. |
| **[`vbmeta.img.xz`](image/vbmeta.img.xz)** | 64 KB | **Stock VBMeta Image**: Partition image with Android Verified Boot (AVB) flags configured for disabling verity and verification, preventing bootloops when booting custom kernels or GSI images. |

### Extracting & Flashing via Fastboot

To flash the required partition images to the Realme C15 Qualcomm Edition:

```bash
# 1. Navigate to the image directory
cd panel/realme-bengal/image

# 2. Decompress the partition images (if not already unpacked)
xz -dk boot.img.xz
xz -dk vbmeta.img.xz

# 3. Reboot the device into Fastboot / Bootloader mode
adb reboot bootloader

# 4. Flash VBMeta with verification disabled (Crucial)
fastboot flash vbmeta --disable-verity --disable-verification vbmeta.img

# 5. Flash the mandatory APatch-patched boot image
fastboot flash boot boot.img

# 6. Reboot device
fastboot reboot
```

---

## 3. Directory Layout

| Directory | Description |
| :--- | :--- |
| **[`image/`](image/)** | **Partition Images**: Pre-patched partition archives (`boot.img.xz`, `vbmeta.img.xz`) with APatch SuperKey `ArdoZapp2026` for OTG host mode activation. |
| **[`kpm/usb-serial/`](kpm/usb-serial/)** | **KernelPatch Module (KPM)**: USB-to-Serial converter driver (`usb-serial.kpm` v1.0.0) providing native `/dev/ttyUSB0` through `/dev/ttyUSB7` support for CP210x devices with dynamic termios negotiation up to 3,000,000 bps. |
| **[`bin/`](bin/)** | **Native System Binaries (ARM64 Musl)**: First-party C utilities (`amp-status`, `aplay`, `audio-test`, `clear`, `esputil`, `jacktor-api`, `jacktor-motd`, `otg-trigger`, `serial-bridge`) authored by Ardo Zapp, plus `socat` (GPL-2.0). |
| **[`modul/`](modul/)** | **Root Module (`jacktor-audio-framework`)**: Systemless root module suite containing installer hooks (`customize.sh`), background daemon (`service.sh`), WebUI server (`webroot/`), and system overlays. |

---

## 4. System Role & Topology

```
+-------------------------------------------------------------------------+
|                  REALME C15 ANDROID PANEL (Bengal SM4250)                |
|  - Device Model      : Realme C15 Qualcomm Edition (RMX2195)            |
|  - Base Firmware     : Realme UI 2.0 (Version C.06)                     |
|  - Operating System  : Android 14 (GSI / Treble, Kernel 4.19.152-perf+)  |
|  - Root Core         : APatch (SuperKey: ArdoZapp2026)                  |
|  - Kernel Driver     : usb-serial.kpm v1.0.0 (/dev/ttyUSB0..7)          |
|  - System Module     : Jacktor Audio Framework (Systemless Audio/WebUI)  |
|  - User Interface    : On-screen Touch App + Port 80 WebUI / SSE API    |
+-------------------------------------------------------------------------+
                                     |
                       USB OTG Host Cable (Auto-Active)
                        Silicon Labs CP2102 Bridge
                                     |
+-------------------------------------------------------------------------+
|                      AMPLIFIER ECU (ESP32-WROOM-32)                     |
|  - UART0 Interface   : 921,600 bps full-duplex JSON telemetry           |
|  - Hardware Engine   : TAS5805M / TAS5825M, SMPS, 32-Band FFT, Relays    |
+-------------------------------------------------------------------------+
```

---

## 5. Installation & Deployment Guide

### Step 1: Base Firmware & Partition Setup
1. Ensure the device is running official **Realme UI 2.0 C.06** (`RMX2195_11_C.06`).
2. Unlock the bootloader via official deep testing / fastboot.
3. Flash Android 14 GSI into the `system` partition:
   ```bash
   fastboot flash system <android-14-gsi>.img
   ```
4. Flash `vbmeta.img` and `boot.img` provided in [`panel/realme-bengal/image/`](image/) as detailed in Section 2.

### Step 2: Configure APatch Manager
1. After booting into Android 14 GSI, install the **APatch Manager APK**.
2. When prompted for authorization, enter the SuperKey:
   ```
   ArdoZapp2026
   ```
3. Grant Superuser privileges when requested.

### Step 3: Install Driver & Framework Module
During development, differential synchronization can be performed directly via:
```bash
python3 tools/sync_module.py
```
For standalone or initial installation, generate the distribution ZIP:
```bash
python3 tools/build.py --module-only
```
Then install via APatch Manager or command line:
```bash
adb push build/jacktor-framework.zip /data/local/tmp/
adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'
adb reboot
```

---

## 6. Runtime Services

Once initialized during boot via `service.sh`:
* **OTG Host Trigger & Monitor**: Detects USB OTG connection state and triggers the Qualcomm USB controller into host mode to bind the CP2102 bridge to `/dev/ttyUSB0`.
* **Centralized Jacktor API & WebUI Dashboard**: Served at `http://<device-ip>/` (Port 80) via BusyBox `httpd` with high-speed SSE telemetry stream (`/cgi-bin/jacktor-api?action=stream`).
* **Bit-Perfect Audio Engine**: Configures Android HAL and policy to enforce 384 kHz / 32-bit output with strict per-device policy routing.
* **Serial Telemetry Bridge (`socat` ARM64 Port 7777)**: Multiplexes `/dev/ttyUSB0` at 921,600 bps for concurrent access by local background daemons, the Android launcher app, and developer tooling (`tools/cmd.py`).
