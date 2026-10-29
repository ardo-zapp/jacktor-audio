# Jacktor Audio Framework - Android Systemless Module

This directory contains the complete source structure for the **Jacktor Audio Framework** root module, designed for Magisk, KernelSU, and APatch on **Android 14 GSI** (Qualcomm Bengal SM4250 / SM6115).

On the device, the installed payload resides at `/data/adb/modules/jacktor-audio-framework/`.

---

## 1. Directory Structure

```
jacktor-audio-framework/
├── META-INF/                # Standard flashable zip router (update-binary / updater-script)
├── customize.sh             # Dynamic installation script (symlink creation & KPM deploy)
├── kpm/                     # Pre-bundled KernelPatch Module drivers
│   └── usb-serial.kpm       # USB-Serial CP210x kernel driver
├── module.prop              # Module metadata (id, name, version, author, description)
├── service.sh               # Unified background runtime engine (post-boot)
├── post-fs-data.sh          # Early-boot filesystem and node initialization
├── system.prop              # System property overrides (audio, display, performance)
├── auto_mount               # Magisk systemless overlay marker
├── config/                  # Centralized configuration, authentication, and state files
├── system/                  # Systemless filesystem overlays (BusyBox NDK, audio configs)
└── webroot/                 # Responsive HTML5/CSS3/JS WebUI Dashboard (Port 80)
```

---

## 2. Core Services (`service.sh`)

The background service script executes after Android boot completion and manages the following tasks:

1. **WebUI HTTP Server (Port 80)**:
   - Starts BusyBox `httpd` bound to port 80: `busybox httpd -p 80 -h $MODDIR/webroot -c $MODDIR/config/httpd.conf`.
   - Serves the telemetry dashboard, audio DSP controls, equalizer settings, and power state toggles.
2. **Audio System Configuration**:
   - Enforces 384 kHz / 32-bit Bit-Perfect audio streaming pipelines for both speaker and USB DAC interfaces.
   - Disables deep-buffer sleeping to eliminate playback latency and jitter.
   - Registers audio background processes with the Android `deviceidle` (Doze) whitelist to ensure continuous audio playback when the display is sleeping.
3. **CPU Governor & Memory Configuration**:
   - Applies CPU governor profiles (`performance` or `balanced`) according to `config/cpu_profile`.
   - Sets virtual memory dirty ratios and activates swap space based on `config/swap_size_mb`.
4. **USB OTG Serial Node Permissions**:
   - Monitors the `/dev/ttyUSB0` character device node created by `usb-serial.kpm`.
   - Enforces `0666` permissions across `/dev/ttyUSB*` to allow unprivileged application access to the ESP32 Amplifier ECU.

---

## 3. Centralized Configuration Directory (`config/`)

All user configurations, authentication tokens, and cached telemetry are consolidated in `config/`:

| File | Description |
| :--- | :--- |
| `config/audio_gain.conf` | Digital, analog, and headphone gain settings. |
| `config/auth.conf` | WebUI and SSH authentication credentials. |
| `config/session_token` | Active WebUI session token. |
| `config/cpu_profile` | Governor profile (`performance` / `balanced`). |
| `config/resampler_mode` | Audio resampler mode (`highest` / `match_source`). |
| `config/swap_size_mb` | Virtual memory swapfile size in megabytes. |
| `config/usb_mode_state` | USB OTG controller state (`auto` / `host` / `device`). |
| `config/httpd.conf` | BusyBox HTTP server configuration file. |
| `config/tweaks.cache` | Cache file for fast status telemetry responses. |
| `config/current_track` | Track metadata for the currently active audio stream. |
| `config/current_cover_ts` | Timestamp for album cover cache validation. |

---

## 4. Module Packaging & Live Updates

### Building the Module ZIP
Use the project build automation script to compile the prerequisite KPM driver and assemble the flashable module ZIP:
```bash
python3 tools/build.py --module-only
```
The output file is written to `build/jacktor-framework.zip`.

### Live Synchronization (`tools/sync_busybox.py`)
To update scripts, WebUI assets, or configurations directly on the running device without reflashing or rebooting:
```bash
python3 tools/sync_busybox.py
```
This pushes the updated files to `/data/adb/modules/jacktor-audio-framework/` and re-applies executable permissions dynamically.
