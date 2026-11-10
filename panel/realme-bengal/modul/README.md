# Jacktor Audio Framework - Systemless Module Suite

This directory contains the root module suite for integrating the **Jacktor Audio Framework** into Android devices via standard root managers: **APatch**, **KernelSU**, and **Magisk**.

---

## 1. System Requirements & Hardware Scope

> [!IMPORTANT]
> **Exclusive Device Target**:
> This module suite is specifically tailored for the **Realme C15 Qualcomm Edition (`bengal` / `RMX2195`)** running **Android 14 GSI** on top of the mandatory **Realme UI 2.0 (Version C.06)** base firmware.

---

## 2. Standard Module Architecture (`jacktor-audio-framework`)

The module adheres to the official Android systemless root specification:

```
panel/realme-bengal/modul/
└── jacktor-audio-framework/
    ├── META-INF/                # Standard flashable zip & Magisk entrypoints
    │   └── com/google/android/
    │       ├── update-binary    # Standard root manager update binary router
    │       └── updater-script   # Recovery dummy script
    ├── customize.sh             # Dynamic BusyBox applet linker & installer script
    ├── kpm/
    │   └── usb-serial.kpm       # Bundled USB-Serial KernelPatch Module driver
    ├── module.prop              # Magisk/KernelSU/APatch metadata
    ├── service.sh               # Unified boot & background runtime engine
    ├── post-fs-data.sh          # Early boot filesystem & node preparation
    ├── system.prop              # System property overrides (audio, display, tweaks)
    ├── auto_mount               # Magisk systemless overlay marker
    ├── config/                  # Centralized configuration & persistent state directory
    ├── system/                  # Systemless overlay: BusyBox NDK, audio configs, binaries
    └── webroot/                 # Responsive HTML5/CSS3/JS WebUI Dashboard & REST/SSE API (Port 80)
```

---

## 3. Centralized Configuration Directory (`config/`)

All parameters, credentials, and persistent states are strictly consolidated under `config/`:

| File | Purpose & Function |
| :--- | :--- |
| **`config/auth.conf`** | Credentials for WebUI and SSH dashboard login (SHA-256 hashed). |
| **`config/session_token`** | Active session token for authenticated WebUI requests. |
| **`config/cpu_profile`** | Active CPU governor profile (`performance` / `balanced`). |
| **`config/resampler_mode`** | Global / fallback audio resampler mode (`highest` / `match_source`). |
| **`config/resampler_mode_<dev>`** | Per-device resampler mode for `internal_speaker`, `internal_headset`, `usb_dac`. |
| **`config/fixed_quality_<dev>`** | Per-device fixed sample rate and bit-depth (e.g. `48000_16`, `96000_24`, `192000_24`). |
| **`config/serial_bridge.conf`** | Configuration for serial-bridge daemon (`TCP_PORT=7777`, `BAUD=921600`). |
| **`config/swap_size_mb`** | Swapfile size in megabytes for memory virtualization. |
| **`config/usb_mode_state`** | USB OTG power/data mode state (`auto` / `host` / `device`). |
| **`config/httpd.conf`** | BusyBox HTTP server configuration file for Port 80 WebUI. |
| **`config/current_track`** | Real-time metadata of currently playing track (displays `"-"` in standby). |
| **`config/current_cover_ts`** | Album art cache timestamp. |
| **`config/audio-deviceidle.xml`** | Android deviceidle (Doze mode) whitelist to prevent audio dropouts. |

---

## 4. Core Runtime Services (`service.sh`)

The module's background daemon executes upon Android boot completion and manages critical real-time subsystems:
1. **Direct ALSA Bit-Perfect Audio Pipeline**: Bypasses Android's software mixing framework (**AudioFlinger**) and routes audio directly to ALSA hardware PCM nodes (`/dev/snd/pcmC*D*p`) up to **384 kHz / 32-bit**.
2. **WebUI & Jacktor API Engine (Port 80)**: Launches BusyBox `httpd` serving the responsive WebUI (`webroot/`) and high-speed CGI REST/SSE endpoints (`/cgi-bin/jacktor-api`).
3. **USB OTG Host State & Node Permissions**: Sets `/dev/ttyUSB0..7` permissions to `0666` and enforces host mode to bind the CP2102 serial bridge automatically.
4. **Serial Telemetry Bridge (`socat` ARM64 Port 7777)**: Launches the background `serial-bridge` daemon multiplexing `/dev/ttyUSB0` at **921,600 bps** across TCP Port 7777 and PTY `/dev/ttyESP`.

---

## 5. Direct ALSA Bypass & Prohibition of Third-Party Audio Mods

> [!WARNING]
> **Do NOT Install Third-Party Audio Mods (Dolby Atmos, ViPER4Android, JamesDSP)**
>
> The Jacktor Audio Framework bypasses Android's **AudioFlinger** (`audioserver`) entirely, routing playback straight to the kernel ALSA audio drivers for bit-perfect fidelity.
>
> Third-party audio mods depend on injecting effect libraries into AudioFlinger. Because AudioFlinger is bypassed, these mods **will not function (no-op)**.
>
> Attempting to force audio through AudioFlinger to enable these mods destroys the bit-perfect pipeline, causing forced software downsampling (to 48 kHz / 16-bit), severe audio distortion, and buffer latency that ruins the real-time visual synchrony of the ESP32 FFT analyzer.

---

## 6. Live Synchronization Without Reflashing (`tools/sync_module.py`)

During development, differential synchronization can be performed over SSH without creating ZIP packages or rebooting:

```bash
# Push modified files & restart WebUI if needed:
python3 tools/sync_module.py

# Force full re-sync:
python3 tools/sync_module.py --force

# Sync BusyBox NDK & re-generate symlinks:
python3 tools/sync_module.py --busybox
```
