# Jacktor Audio - Systemless Module Suite

This directory contains the root module for integrating the **Jacktor Audio Framework** into Android devices via standard root managers: **Magisk**, **KernelSU**, and **APatch**.

---

## 1. Standard Module Architecture (`jacktor-audio-framework`)

The module follows the official Android root module specification. All installation metadata, installer lifecycle hooks, and runtime payloads are organized under a single standard structure:

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
    ├── config/                  # CENTRALIZED configuration & persistent state directory
    ├── system/                  # Systemless overlay: BusyBox NDK, audio configs, binaries
    └── webroot/                 # Responsive HTML5/CSS3/JS WebUI Dashboard (Port 80)
```

### Clean Runtime Module
When the zip archive is flashed via Magisk, KernelSU, or APatch:
1. The manager extracts the zip payload to `$MODPATH` (`/data/adb/modules/jacktor-audio-framework/`).
2. The installer runs `customize.sh`.
3. At the conclusion of setup, `customize.sh` purges temporary installer files (`META-INF/`, `customize.sh`, `kpm/`) from `$MODPATH`.
4. As a result, the live running module on the Android device remains clean and uncluttered.

---

## 2. Centralized Configuration Directory (`config/`)

To prevent configuration fragmentation across the filesystem, all settings, credentials, and state caches are strictly consolidated under `config/`:

| File | Purpose & Function |
| :--- | :--- |
| **`config/audio_gain.conf`** | Audio gain calibration values (Digital, Analog, Ear PA). |
| **`config/auth.conf`** | Credentials for WebUI and SSH dashboard login. |
| **`config/session_token`** | Active session token for authenticated WebUI requests. |
| **`config/cpu_profile`** | Active CPU governor profile (`performance` / `balanced`). |
| **`config/resampler_mode`** | Audio resampler mode (`highest` / `match_source`). |
| **`config/swap_size_mb`** | Swapfile size in megabytes for memory virtualization. |
| **`config/usb_mode_state`** | USB OTG power/data mode state (`auto` / `host` / `device`). |
| **`config/httpd.conf`** | BusyBox HTTP server configuration file for Port 80 WebUI. |
| **`config/tweaks.cache`** | Cached telemetry values used by background WebUI status queries. |
| **`config/current_track`** | Real-time metadata of currently playing track. |
| **`config/current_cover_ts`** | Album art cache timestamp. |
| **`config/audio-deviceidle.xml`** | Android deviceidle (Doze mode) whitelist to prevent audio dropouts. |
| **`config/run_once_doze`** | One-time flag verifying Doze whitelist registration. |
| **`config/jitter-reducer-functions.shlib`** | Shell library for real-time task affinity and I/O scheduler tuning. |

---

## 3. Dynamic Applet Installation (`customize.sh`)

When the module is installed:
1. **BusyBox Setup**: Inspects the stock ROM's binary list in `/system/bin`.
2. **Conflict Avoidance**: Dynamically generates symlinks **only for applets not already present in the stock ROM**, preventing binary collisions or bootloops.
3. **Configuration Defaults**: Populates missing configuration files in `$MODPATH/config/` with factory defaults.
4. **Kernel Driver Deployment**: If APatch is detected (`/data/adb/ap/`), automatically installs the bundled `kpm/usb-serial.kpm` driver into `/data/adb/ap/kpm/usb-serial/usb-serial.kpm` (`chmod 644`).
5. **Payload Cleanup**: Removes `META-INF`, `customize.sh`, and `kpm` from `$MODPATH` so the running module directory remains minimal.

---

## 4. Building & Packaging the Flashable ZIP

The module is built automatically by the project's unified build automation tool:

```bash
# Build complete project (KPM driver, firmware, and flashable module ZIP):
python3 tools/build.py

# Or package only the module ZIP:
python3 tools/build.py --module-only
```

The build script automatically:
1. Compiles `usb-serial.kpm` using the AArch64 Musl toolchain.
2. Copies `usb-serial.kpm` into `jacktor-audio-framework/kpm/usb-serial.kpm`.
3. Packages the module into `build/jacktor-framework.zip` and `build/jacktor-audio-framework.zip` with correct POSIX executable permissions (`0755` for scripts and binaries, `0644` for data files, and preserved symlinks).

---

## 5. Module Deployment & Flashing

### Via Magisk App / APatch App
1. Transfer `build/jacktor-framework.zip` to device storage.
2. Open **Magisk** or **APatch** application -> **Modules** tab -> **Install from storage**.
3. Select `jacktor-framework.zip` and reboot after installation.

### Via ADB Terminal
```bash
adb push build/jacktor-framework.zip /data/local/tmp/
adb shell su -c 'magisk --install-module /data/local/tmp/jacktor-framework.zip'

# Or on APatch:
adb shell su -c '/data/adb/ap/bin/apd --install /data/local/tmp/jacktor-framework.zip'

adb reboot
```

### Live Synchronization Without Rebooting (`tools/sync_busybox.py`)
To push updated scripts, WebUI assets, or configurations directly to the live module without flashing the ZIP or rebooting:
```bash
python3 tools/sync_busybox.py
```
