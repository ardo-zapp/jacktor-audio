# Jacktor Audio — Android Control Panel

This directory contains the Kotlin Multiplatform Android application for the **Jacktor Audio Control Panel**, replacing the legacy ESP32-S3 hardware panel. All panel system services, Bit-Perfect audio routing, low-latency DSP, and web interfaces are managed natively by the **Jacktor Audio Framework** Magisk/APatch module on Android.

---

## 1. Architecture Overview

The Android Panel communicates with the **Jacktor Audio ESP32 Amplifier** ECU over **USB Serial (USB-CDC)** at **921,600 bps** using the `usb-serial-for-android` driver engine.

* **USB Serial Bridge:** Full-duplex asynchronous JSON telemetry stream (`rt` @ 30 Hz for 16-band FFT and VU meters, `hz1` @ 1 Hz for diagnostics).
* **Industrial UI:** Jetpack Compose & Kotlin Multiplatform UI featuring real-time stereo VU meters, audio spectrum analyzers, amplifier power management, and application launching.
* **Bit-Perfect Audio Engine:** Directly interfacing with USB Audio DACs and internal audio pipelines at up to 384kHz 32-bit audio.

---

## 2. Directory Structure

* **`androidApp/`**: Native Android application module, UI components, activities, and theme definitions.
* **`shared/`**: Common Kotlin Multiplatform module containing:
  * `service/UsbSerialManager.kt`: USB OTG serial communication manager (`921,600 bps`).
  * `model/TelemetryData.kt`: Serialized data models for amplifier and panel telemetry.
  * `parser/TelemetryParser.kt`: Real-time JSON parser for incoming amplifier frames.

---

## 3. Building & Installation

To build and install the Android Panel application using Gradle:

```bash
cd panel/app
./gradlew assembleDebug
adb install androidApp/build/outputs/apk/debug/androidApp-debug.apk
```
