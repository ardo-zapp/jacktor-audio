# Changelog

All notable changes to the Jacktor Audio Smart Amplifier Ecosystem project are documented in this file.

---

## [1.3.0] - 2026-11-10

### Added & Architecture Enhancements
- **Direct ALSA Bit-Perfect Pipeline & AudioFlinger Bypass**:
  - Implemented direct hardware PCM audio output via ALSA (`/dev/snd/pcmC*D*p`), completely bypassing Android's software mixing layer (**AudioFlinger**).
  - Delivers uncompressed, low-latency audio transmission up to **384 kHz / 32-bit**.
  - Documented strict architectural prohibition against third-party audio mod modules (Dolby Atmos, ViPER4Android, JamesDSP) which depend on AudioFlinger and are incompatible with direct ALSA routing.
- **Partition Image Archives & Custom Kernel Deployment**:
  - Dumped and packaged pre-patched partition images from the physical Realme C15 Qualcomm Edition device into `panel/realme-bengal/image/`:
    - `boot.img.xz`: Linux kernel `4.19.152-perf+` authored by Ardo Zapp, pre-patched with APatch (KernelPatch) using SuperKey **`ArdoZapp2026`** to automatically trigger USB OTG host mode post-boot.
    - `vbmeta.img.xz`: Partition image configured with verification and verity disabled for fastboot deployment.
- **Multi-Sensor 1-Wire Thermal Architecture (GPIO 27)**:
  - Added support for up to 3 multi-drop DS18B20 digital sensors on GPIO 27, mapped dynamically via 64-bit ROM hardware addresses in NVS (`temp_ab_rom`, `temp_d_rom`, `temp_smps_rom`).
  - Supported dynamic address scanning (`temp-scan`), role assignment (`temp-assign`), and role swapping (`temp-swap`).
- **TM1637 Display Clock Colon Timing Stabilization**:
  - Fixed dual-clock phase beat race condition where the 1000ms watchdog competed against 1000ms hardware SQW ticks, causing microsecond double-updates.
  - Locked display colon blinking strictly to the physical falling-edge interrupt pulses of the DS3231 RTC 1 Hz SQW pin (GPIO 36).
- **High-Speed Auto-Discovery Engine Optimization (`tools/discover.py`)**:
  - Streamlined socket verification timeouts (0.25s – 0.5s) to eliminate LAN stalls during IP collision, achieving discovery resolution in under 360ms.
- **License & Architecture Documentation Updates**:
  - Updated root `LICENSE` and subsystem documentation across all modules to reflect first-party CC BY-NC-SA 4.0 authorship, custom kernel GPL-2.0 distribution, and typography licensing.

---

## [1.2.0] - Recent Release Updates

### Added & Enhanced Features
- **Hardware SQW 1 Hz Timekeeping & Synchronized Digital Clock**:
  - Implemented monotonic epoch timekeeping driven directly by the DS3231 physical 1 Hz SQW falling edge on GPIO 36.
  - Standardized TM1637 7-segment display standby colon (`:`) blinking with a 500ms ON / 500ms OFF cycle locked to SQW phase.
  - Integrated real-time ISO 8601 hardware timestamps into `commsLog()` frames, diagnostic packets, and host TUI logs.
- **Audio Analyzer Industrial Calibration & Diagnostic Telemetry**:
  - Calibrated ADC1 continuous sampling on GPIO 34 (Left Channel / `ADC1_CH6`) and GPIO 39 (Right Channel / `ADC1_CH3`).
  - Integrated 1-Pole IIR DC Blocker ($R = 0.996\text{f}$) and dynamic leaky baseline noise estimator for true silence noise gating and responsive low-level signal capture (-40 dBFS).
  - Added real-time `adc_audio` telemetry object to `deviceinfo` command reporting DC bias voltages and raw digital ADC values for both channels.
- **Setup Diagnostics Sequence & Standby Thermal Safety**:
  - Refactored boot self-test into a structured setup diagnostics sequence with a steady 500ms `"  on"` indicator on the TM1637 display.
  - Implemented dynamic standby thermal cooling: fan engages during standby only when heatsink temperature is elevated and shuts off completely once cooled.
  - Persisted module power states (e.g. QCC5125, Speaker Protector) in NVS to prevent false-positive optocoupler error trips across reboots.
- **Dynamic Termios & USB OTG Kernel Driver (`usb-serial.kpm`)**:
  - Created standalone KernelPatch Module exposing `/dev/ttyUSB0..7` with dynamic termios baud rates up to 3,000,000 bps (including 921,600 bps).
  - Added dynamic fingerprint scanning for `tty_std_termios` memory offset resolution.
