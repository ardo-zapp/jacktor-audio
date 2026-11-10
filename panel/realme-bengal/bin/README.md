# Jacktor Audio Framework - Native Android System Binaries (`panel/realme-bengal/bin/`)

Source code for all native ARM64 command-line utilities and background daemons deployed to the Android Control Panel (`Realme C15 Bengal`). Compiled binaries are installed into `../modul/jacktor-audio-framework/system/bin/` and synchronized to the device via `tools/sync_module.py`.

---

## 1. Tools Overview & Authorship

| Binary | Authorship | License | Source Directory | Description |
| :--- | :--- | :--- | :--- | :--- |
| **`amp-status`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`amp-status/`](amp-status/) | Amplifier & Audio Diagnostics — reads ALSA, USB OTG, power supply, and audio server status. |
| **`aplay`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`aplay/`](aplay/) | ALSA Compatibility Layer — wraps Android `tinyplay` with automatic sound card detection. |
| **`audio-test`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`audio-test/`](audio-test/) | Safe 24-bit/44.1kHz Audio Signal Generator — real-time sine/sweep tone generator with hardware volume safety. |
| **`clear`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`clear/`](clear/) | Full Screen & Scrollback Clear — emits ANSI `\033[3J\033[H\033[2J` for clean SSH terminal sessions. |
| **`esputil`** | **Adapted / Ported** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`esputil/`](esputil/) | ESP32 Serial Flasher & Bootloader Utility — adapted from `cpq/esputil` (Cesanta) for AArch64 Musl, flashing ESP32 at 921,600 bps. |
| **`jacktor-api`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`jacktor-api/`](jacktor-api/) | Native High-Speed API Service & CGI Engine — direct POSIX kernel/sysfs reader and native TCP bridge socket client (installed as `/system/bin/jacktor-api` and symlinked to `webroot/cgi-bin/jacktor-api`). |
| **`jacktor-motd`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`jacktor-motd/`](jacktor-motd/) | SSH Login Banner & Telemetry Generator — displays ASCII branding, 2-column hardware telemetry, and service statuses. |
| **`otg-trigger`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`otg-trigger/`](otg-trigger/) | USB OTG State Controller — manages `host`, `device`, and `auto` modes with PMIC/extcon ID-pin detection. |
| **`serial-bridge`** | **First-Party** (Ardo Zapp) | CC BY-NC-SA 4.0 | [`serial-bridge/`](serial-bridge/) | Serial Telemetry Bridge Daemon — manages `socat` multi-client TCP (Port 7777) & PTY (`/dev/ttyESP`) multiplexing for `/dev/ttyUSB0`. |
| **`socat`** | **Ported** (Gerhard Rieger / Ardo Zapp) | **GPL-2.0-only** | [`socat/`](socat/) | Multipurpose Relay — ported to AArch64 Musl Static by Ardo Zapp (Upstream: Gerhard Rieger - [`dest-unreach.org`](http://www.dest-unreach.org/socat/)). Underlying bi-directional byte stream relay invoked by `serial-bridge`. |

---

## 2. Build System & Cross-Compilation

All binaries are compiled into standalone static executables (`-static` / `-static-pie`) using the AArch64 Musl cross-compiler so they have zero dependencies on Android's Bionic `libc`:

```makefile
PROJECT_ROOT ?= $(abspath $(CURDIR)/../../../..)
TOOLCHAIN ?= $(PROJECT_ROOT)/tools/aarch64-linux-musl-cross
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/aarch64-linux-musl-
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
CFLAGS = -O2 -Wall -Wextra -static -pie -D_GNU_SOURCE
```

### Build & Install All Binaries to Module
```bash
cd panel/realme-bengal/bin
for dir in */; do
    make -C "$dir" clean all install
done
```
*(Running `make install` strips the binary, copies it to `../modul/jacktor-audio-framework/system/bin/`, sets `0755` permissions, and cleans intermediate build artifacts from the source directory).*

### Sync Updated Binaries to Android Panel
```bash
python3 tools/sync_module.py
```

---

## 3. Licensing & Open-Source Compliance Notice

- **Custom & Adapted Utilities (`amp-status`, `aplay`, `audio-test`, `clear`, `esputil`, `jacktor-api`, `jacktor-motd`, `otg-trigger`, `serial-bridge`)**: Authored and adapted by **Ardo Zapp** for the Jacktor Audio Framework and licensed under [CC BY-NC-SA 4.0](../../../LICENSE). (Note: `esputil` is adapted from the original codebase by Cesanta Software Limited / Sergey Lyubka [`cpq/esputil`](https://github.com/cpq/esputil) under the permissive terms of the MIT License).
- **Ported Open-Source Utility (`socat`)**: Originally authored by **Gerhard Rieger and contributors** ([`dest-unreach.org`](http://www.dest-unreach.org/socat/)), licensed under the **GNU General Public License v2.0 (GPL-2.0-only)**. Ported, configured, and compiled for standalone AArch64 Musl static target on Android by **Ardo Zapp**. In full compliance with GPL v2.0 Section 3, the complete corresponding Linux/Android C source code and custom `Makefile` used to build the bundled `system/bin/socat` binary are maintained in [`socat/`](socat/). Note that `serial-bridge` invokes `socat` as a separate user-space executable via `fork()`/`exec()` and does not link against `socat` libraries.
