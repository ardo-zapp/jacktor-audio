# Jacktor Audio — Android Control Panel Application (`com.jacktor.audiopanel`)

This directory contains the native **Kotlin Android (Jetpack Compose)** launcher and hardware control application for the **Jacktor Audio Control Panel**, running on the **Realme C15 Qualcomm Edition (`bengal` / `RMX2195`)**.

All kernel USB-serial drivers (`usb-serial.kpm`), `serial-bridge` multiplexing (`127.0.0.1:7777` & `/dev/ttyESP`), Direct ALSA Bit-Perfect audio routing, and Port 80 Jacktor API services are managed at the system level by the **Jacktor Audio Framework** root module.

---

## 1. System Role & Architecture

The Android Panel application operates as both the **Default Home Launcher** and the central digital interface for the amplifier ecosystem:

```
+-------------------------------------------------------------------------+
|                  ANDROID CONTROL PANEL APPLICATION                      |
|  - App ID            : com.jacktor.audiopanel                           |
|  - Role              : Default System Launcher (CATEGORY_HOME)          |
|  - UI Stack          : Jetpack Compose + Industrial Dark Theme          |
|  - Telemetry Stream  : SSE via Jacktor API (Port 80) & TCP 127.0.0.1:7777|
|  - Bit-Perfect Audio : Direct ALSA Hardware Playback (Up to 384kHz/32b) |
+-------------------------------------------------------------------------+
                                     |
                       Multiplexed UART Link @ 921,600 bps
                                     |
+-------------------------------------------------------------------------+
|                      AMPLIFIER ECU UNIT (ESP32)                          |
+-------------------------------------------------------------------------+
```

### Key Architectural Capabilities
* **Full-Duplex Asynchronous Telemetry**: Receives `rt` audio frames (~30 FPS) for real-time 8/16/32/64-band KissFFT visualization and segmented stereo VU meters, alongside `hz1` diagnostic frames (rail voltages, temperatures, relay status).
* **Zero-Latency Boot Terminal (`SetupTerminalFragment.kt`)**: Displays a hardware-style pure black boot terminal on amplifier power-on, rendering the self-test diagnosis in real-time and returning cleanly to the main dashboard once self-test passes.
* **Synchronized Display Power**: Manages screen wake policies automatically (`stay_on_mode=always` when amplifier is active; reverts to standard screen timeout during standby unless 24/7 standby is enabled).
* **Direct ALSA Bit-Perfect Path**: Bypasses Android's AudioFlinger, streaming unadulterated high-resolution audio directly to the ALSA driver.

---

## 2. Directory Structure

* **`androidApp/src/main/java/com/jacktor/audiopanel/`**:
  * `MainActivity.kt`: Main launcher dashboard, screen wake-lock manager, and power-on terminal overlay host.
  * `SettingsActivity.kt`: Comprehensive settings interface for `serial-bridge`, relays, Bluetooth QCC5125 GAIA DSP, KissFFT/ADC calibration, thermal PID fan curves, and DS3231 RTC synchronization.
  * `service/TelemetryConnectionManager.kt`: Non-blocking client for `serial-bridge` (`127.0.0.1:7777`) and Jacktor API SSE stream.
  * `repository/AmplifierRepository.kt`: Reactive `StateFlow` state holder for real-time FFT, slow telemetry, NVS settings, and boot terminal logs.
  * `parser/TelemetryParser.kt` & `model/TelemetryData.kt`: `kotlinx.serialization` JSON models and command builders.
  * `ui/`: Industrial Jetpack Compose UI components (`SetupTerminalFragment.kt`, `SpectrumAnalyzerView.kt`, `StereoVuMeterView.kt`, `TelemetryGaugeView.kt`, `MediaControlToolbar.kt`, `AppDrawerDialog.kt`).

---

## 3. Building & Deployment

```bash
cd panel/app
./gradlew :androidApp:assembleDebug
```

---

## 4. License

Copyright (C) 2026 **Ardo Zapp**. Licensed under **[Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)](../../LICENSE)**.
