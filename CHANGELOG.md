# Changelog

All notable changes to the Jacktor Audio Smart Amplifier Ecosystem project are documented in this file.

## [1.2.0] - Recent Release Updates

### Added & Enhanced Features
- **Hardware SQW 1 Hz Timekeeping & Synchronized Digital Clock**:
  - Implemented monotonic epoch timekeeping driven directly by the DS3231 physical 1 Hz SQW falling edge on GPIO 35.
  - Standardized TM1637 7-segment display standby colon (`:`) blinking with a 500ms ON / 500ms OFF cycle locked to SQW phase.
  - Integrated real-time ISO 8601 hardware timestamps into `commsLog()` frames, diagnostic packets, and host TUI logs.

- **Audio Analyzer Industrial Calibration & Diagnostic Telemetry**:
  - Calibrated ADC1 continuous sampling with $kAnalogScaleR = 9.80$ on GPIO 36 (Right Channel) to equalize amplitude with GPIO 39 (Left Channel).
  - Integrated 1-Pole IIR DC Blocker ($R = 0.996\text{f}$) and dynamic leaky baseline noise estimator for true silence noise gating and responsive low-level signal capture (-40 dBFS).
  - Added real-time `adc_audio` telemetry object to `deviceinfo` command reporting DC bias voltages and raw digital ADC values for both channels.

- **Setup Diagnostics Sequence & Standby Thermal Safety**:
  - Refactored boot self-test into a structured setup diagnostics sequence with a steady 500ms `"  on"` indicator on the TM1637 display.
  - Implemented dynamic standby thermal cooling: fan engages during standby only when heatsink temperature is elevated and shuts off completely once cooled.
  - Persisted module power states (e.g. QCC5125, Speaker Protector) in NVS to prevent false-positive optocoupler error trips across reboots.

- **Music Player Playlist Scanner & SD Card JSON Database (`library.json`)**:
  - Implemented Full Scan (resets database and re-indexes all `.flac` and `.ogg` files in `/sdcard/Music`) and Partial Scan (discovers new files and removes missing ones while preserving existing entries).
  - Saved track library persistently at `/sdcard/Jacktor/data/music/library.json`.
  - Added Action Bar scan controls and empty-state centered "Full Scan" button.
  - Implemented real-time scan progress overlay modal with active file status reporting.
  - Added categorization tabs (Title, Artist, Album, Genre, Album Artist).
  - Implemented word-by-word karaoke lyrics highlighting, mini player bar, expandable full player view, and 4-state repeat modes (Off, All, One, Shuffle).

- **Storage Access Framework (SAF) & File Manager App**:
  - Unified file/directory access layer (`saf_manager.cpp/.h`) supporting SD Card, USB Mass Storage, and SMB network shares.
  - Implemented complete file manipulation tools (directory creation, recursive deletion, rename, stream copy, and cross-storage move).
  - Added File Manager App (`ui_file_manager.cpp`) featuring storage drive capacity cards, FAT32/exFAT formatting dialogs, and SMB connection setup.
  - Implemented 32KB PSRAM ring-buffer audio stream caching (`psram_cache.cpp/.h`) for zero-buffering network audio streaming.

- **USB OTG Host Mode & Mass Storage Transfer App**:
  - Reconfigured default USB PHY state to USB Host / OTG mode.
  - Built Mass Storage File Transfer App (`ui_msc_transfer.cpp/.h`) in English with SD card capacity card, transfer activity animation bar, safety warning, and EJECT & EXIT controls.
  - Implemented dynamic USB PHY switching: switches to USB Device mode on launching USB DAC or MSC apps, and restores USB Host/OTG mode upon exiting.
  - Disabled automatic screen opening on USB DAC connection to enforce manual launch control while retaining dynamic PHY mode switching.

- **Visualizer Source Selector & Audio-Reactive Lighting Engine**:
  - Added persistent NVS configuration manager (`analyzer_config.cpp/.h`) for Audio Analyzer and Stereo VU Meter source selection (Panel direct PCM DSP vs Amplifier GPIO ADC).
  - Designed audio-reactive rhythm tracking engine (`panel_rgb_rhythm_tick` in `panel_hardware.cpp/.h`) to dynamically modulate RGB LED colors and brightness based on audio energy history and rhythm peaks.
  - Updated `tools/cmd.py` TUI so Amplifier mode VU meters and Audio Analyzer telemetry remain available at all times.

- **Android-Style UI Overhaul & Design Tokens**:
  - Centralized dark mode surface colors, 10px rounded card corners, and 14px pill buttons in `panel_theme.h`.
  - Redesigned status bar with clock, Music/DAC mode badges, Green RX and Blue TX UART activity LEDs, and Wi-Fi signal indicator.
  - Implemented memory cleanup on closing non-background applications (`lv_obj_del`) to free RAM/PSRAM while preserving background audio playback.

- **Hardware & Universal UTF-8 Support**:
  - Configured ESP32-S3 N16R8 Octal SPI (OPI) Flash (16MB) and Octal PSRAM (8MB) in `platformio.ini` and `sdkconfig.defaults`.
  - Added native ESP-IDF PCF8574 LCD 1602 I2C driver (`lcd1602_i2c.cpp/.h`) with HD44780 4-bit initialization sequence and I2C karaoke lyrics line rendering on SDA (GPIO 1) and SCL (GPIO 2).
  - Enabled universal UTF-8 encoding (`LV_TXT_ENC_UTF8` in `lv_conf.h`) and created UTF-8 string sanitizer (`utf8_utils.cpp/.h`) supporting CJK, Cyrillic, Arabic, Latin-Extended, symbols, and emojis.
  - Fully migrated firmware components from Arduino frameworks to 100% native ESP-IDF v6+ APIs.

---

## [1.1.0] - 2025-10-30

- Completed UART protocol handlers: 10/1 Hz telemetry, NVS setters with ACK, RTC synchronization (offset/rate-limit), and base64 OTA streaming with reboot guard.
- Added RTC utility functions in sensor module (ISO8601 Z suffix, epoch getter/setter) and scheduled safe reboot on OTA completion.
- Updated documentation (`README.md`) with telemetry schema, command definitions, RTC policies, final GPIO mappings, and NVS configuration examples.
- Integrated manual (Power+BOOT) and UART factory reset with `factory_reset_executed` log, dual buzzer tones, and safe reboot sequence.
- Migrated firmware from Arduino frameworks to pure ESP-IDF v6+ native APIs for all ESP32 and ESP32-S3 components.
