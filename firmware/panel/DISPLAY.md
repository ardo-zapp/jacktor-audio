# MAIN PANEL DISPLAY & USER INTERFACE SPECIFICATIONS (ESP32-S3)

This document serves as the authoritative UI (User Interface) and UX (User Experience) architecture, state machine specification, and visual design guideline for the **3.2" TFT LCD Main Panel (ILI9341 + XPT2046)** powered by the **ESP32-S3** microcontroller in the Jacktor Audio Smart Amplifier Ecosystem.

---

## 1. System State Machine & Power Lifecycle (Audio System Standard)

Following professional audio equipment standards, when the system is switched to Standby/Power OFF, the main panel display enters **ESP32-S3 Deep Sleep** (`esp_deep_sleep_start()`) with the display backlight turned **completely OFF (0%)** for ultra-low power consumption.

*   **No Touch-Wake in Standby**: Touch events do not wake up the panel while in Deep Sleep Standby.
*   **Amplifier Synchronized Boot**: The panel wakes up automatically upon receiving the power-on trigger or handshake frame from the main ESP32 Amplifier ECU, entering the **Self-Test Procedure** to synchronize hardware states.
*   **Independent Bypass Mode**: Booting up using the panel BOOT button (GPIO 0) bypasses the Self-Test phase entirely, proceeding straight to the Operational Dashboard with an overlay warning badge for standalone operations.

```
                  ┌─────────────────────────────────────────┐
                  │ 0. DEEP SLEEP STANDBY / COLD POWER OFF  │
                  │   (Display & Backlight Completely OFF)  │
                  └────────────────────┬────────────────────┘
                                       │
            ┌──────────────────────────┴──────────────────────────┐
            │ (Power-On Command from Amplifier ECU)               │ (Panel BOOT Button GPIO 0 Pressed)
            ▼                                                     ▼
┌───────────────────────┐                               ┌──────────────────────────┐
│   1. SELF-TEST PAGE   │ ◄── ECU diagnostic handshake  │ 1B. INDEPENDENT BYPASS   │
│ (Boot Sync Phase)     │                               │     MODE (Self-Test Skipped)│
└───────────┬───────────┘                               └────────────┬─────────────┘
            │                                                        │
            │ (Self-Test PASS)                                       │
            └──────────────────────────┬────────────────────────────┘
                                       │
                                       ▼
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│                             2. OPERATIONAL DASHBOARD & MENU LAUNCHER                     │
│                             (Main Operational Hub / Active UI)                           │
└──────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬────────┘
       │          │          │          │          │          │          │          │
       ▼          ▼          ▼          ▼          ▼          ▼          ▼          ▼
 ┌──────────┐┌──────────┐┌──────────┐┌──────────┐┌──────────┐┌──────────┐┌──────────┐┌──────────┐
 │ MUSIC    ││ USB DAC  ││ USB MSC  ││ SAF FILE ││ 2P NES   ││ SETTINGS ││ AUDIO    ││ EXT LCD  │
 │ PLAYER   ││ ("Jacktor││ TRANSFER ││ MANAGER  ││ EMULATOR ││ APP      ││ VISUAL-  ││ 1602     │
 │ + LYRICS ││  Audio") ││ APP      ││          ││          ││          ││ IZER     ││ LYRICS   │
 └──────────┘└──────────┘└──────────┘└──────────┘└──────────┘└──────────┘└──────────┘└──────────┘
```

---

## 2. Comprehensive Application Suite & UI Specifications

### 2.1. Standby & Deep Sleep Power Management
- **Deep Sleep Mode (`ScreenState::STANDBY` / Deep Sleep)**: Display panel backlight is completely turned OFF (0%). No active touch response; panel stays off until woken up by the main Amplifier ECU or physical power line event.
- **Amplifier Telemetry Synchronization**: Upon wake-on-power command from the ESP32 Amplifier ECU, the panel initializes peripherals and launches the synchronized **Self-Test** screen before displaying the Operational Dashboard.
- **Independent Bypass Mode (`isIndependentBypassMode = true`)**: Activated via BOOT button (GPIO 0). **Bypasses the Warm Boot Self-Test phase completely**, boots directly to the Operational Dashboard with a top warning overlay, grays out amplifier-dependent controls, and enables standalone panel features (USB DAC, NES Emulator, SD Manager, Settings).

---

### 2.2. Operational Dashboard & Top Status Bar
- **Top Status Bar (`status_bar.cpp`)**: Centralized top bar displaying real-time clock, supply rails, active app badges (`[ MUSIC ]`, `[ USB DAC ]`), Wi-Fi status icon, and dedicated Green RX / Blue TX UART activity LEDs (`trigger_status_bar_rx_pulse()`, `trigger_status_bar_tx_pulse()`).
- **Main Launcher Hub**: Interactive grid containing launcher cards for core apps.
- **Memory Management**: Closing non-background screens calls `lv_obj_del` to release memory while maintaining background audio operations.

---

### 2.3. Music Player App & LCD 1602 I2C Karaoke Lyrics Display
- **Multi-Format Audio Engine**: High-fidelity audio playback engine parsing local SD Card tracks (`/sdcard/Music`) supporting FLAC (`.flac`), Ogg FLAC (`.ogg`), uncompressed WAV (`.wav` via zero-library PCM reader), and MP3 (`.mp3` via `micro-mp3` decoder component).
- **MusicScanner & JSON Database (`library.json`)**: Scans music files, generating metadata database at `/sdcard/Jacktor/data/music/library.json`. Supports Full and Partial scans with real-time UI modal progress.
- **UI Views & Categorization**: Five navigation tabs (Title, Artist, Album, Genre, Album Artist), mini player bar, expandable full player screen, and 4 repeat modes (Off, All, One, Shuffle).
- **External LCD 1602 I2C Display (`lcd1602_i2c.cpp`)**: Dedicated hardware display connected via PCF8574 I2C adapter (SDA = GPIO 1, SCL = GPIO 2, Address `0x27`). Displays real-time track title and synchronized word-by-word karaoke lyrics lines.

---

### 2.4. USB DAC Mode App ("Jacktor Audio")
- **Plug-and-Play USB Audio Class 2.0 (`ScreenState::USB_DAC`)**: Recognized on host devices as **"Jacktor Audio"**, supporting up to 32-bit / 384 kHz PCM playback.
- **Manual Launch & PHY Switch**: User manually launches USB DAC mode; system dynamically switches USB PHY to USB Device mode (slave DAC), closing audio sessions and restoring USB Host/OTG mode on exit.
- **Visualizer & Relay Interlocks**: Displays dynamic sample rate, spectrum analyzer, stereo VU meters, activates Relay Audio (GPIO 26), and overrides Amplifier TM1637 display to show `"usb"`.

---

### 2.5. USB MSC File Transfer App
- **Direct PC Storage Access (`ScreenState::USB_MSC`)**: Temporarily reconfigures USB PHY to USB Device MSC mode, exposing the SD card directly to connected host PCs.
- **Interface & Safety Controls**: Displays SD card capacity progress card, transfer activity animation bar, safety warnings, and an `EJECT & EXIT` control button to safely restore USB Host/OTG mode.

---

### 2.6. Storage Access Framework (SAF) File Manager
- **Unified File Manager (`ui_file_manager.cpp`)**: Built upon `saf_manager.cpp`, providing unified access across SD Card, USB Mass Storage, and SMB network shares.
- **File Operations**: Directory creation, recursive deletion, rename, stream copy, cross-storage move, drive capacity reporting (`saf_get_available_drives`), and FAT32/exFAT formatting tools (`saf_format_drive`).
- **PSRAM Stream Cache (`psram_cache.cpp`)**: Allocates 32KB PSRAM ring-buffers for network/SMB audio streaming and artwork/lyrics caching.

---

### 2.7. Onboard 2-Player NES Nofrendo Emulator
- **Nofrendo Core Engine (`ui_nes.cpp`)**: Embedded 60 FPS NES emulation with PSRAM double-buffering and direct SPI DMA rendering to ILI9341 (zero screen flicker).
- **2-Player DualShock 2 Support**: Supports dual inputs (`INP_JOYPAD0` and `INP_JOYPAD1`) via UART JSON telemetry, with persistent button remapping in NVS.
- **Chiptune BGM & Touch Overlay**: Plays synthesized 8-bit NES menu music via GY-PCM5102 I2S DAC (`nes_bgm.cpp`), features a 5-second semi-transparent touch overlay bar, and supports a 1-second `SELECT + START` exit hotkey.

---

### 2.8. System Settings App
- **Categorized Management (`ui_settings.cpp`)**:
  1. **Amplifier Controls**: Power relays, fan presets (Quiet, Normal, Performance), SMPS cutoffs, GAIA tone/EQ controls.
  2. **Panel Display**: PWM backlight brightness slider (0..255) and sleep timeout configuration.
  3. **SD Card Management**: Drive capacity metrics and formatting utilities.
  4. **Gamepad Remapping**: Visual 2-player controller button mapping editor.
  5. **Wi-Fi & Time Sync**: SSID/password configuration and NTP RTC time sync with main amplifier.
  6. **Device Info & Diagnostics**: MCU specifications, memory usage (Flash/PSRAM), firmware version, and sensor logs.

---

## 3. Ambient RGB LED (WS2812) Status & Audio-Reactive Rhythm Tracker

The onboard WS2812 RGB LED serves as a dynamic visual status and audio rhythm indicator (`panel_rgb_rhythm_tick` in `panel_hardware.cpp`):

| Operating Mode / State | RGB LED Visual Pattern | Behavior / Description |
| :--- | :--- | :--- |
| **Standby / Deep Sleep** | OFF (0%) | Completely powered down in Standby. |
| **Active Idle** | Steady Neon Green | Soft, non-intrusive constant illumination. |
| **Music / DAC Playback** | Audio-Reactive Dynamic | Modulates hue and brightness based on real-time audio energy, RMS thresholds, and drum beats. |
| **Paint App Active** | Touch-Mapped Color | Dynamic color mixing derived from canvas touch coordinates (X/Y). |
| **Audio Clipping** | Strobe Red Flash | Instant bright red flash on clipping detection. |
| **System Emergency Alarm** | Strobe Red Alert | 4 Hz rapid red strobe alerting hardware protection faults. |

---

## 4. Visualizer Source Selector & Audio DSP Engine

The audio spectrum analyzer and stereo VU meters support dual-source selection managed by `analyzer_config.cpp`:

1. **Panel PCM DSP Mode**: Direct PCM audio processing from local playback (Music Player or USB DAC) using local KissFFT routines.
2. **Amplifier ADC Mode**: Audio spectrum telemetry received over UART from ESP32 GPIO 36 (Right Channel) and GPIO 39 (Left Channel).

Automatic fallback switches to Panel mode if UART communication is interrupted or bypassed.

---

## 5. UI Design Tokens & Theme Specification

UI visual elements are standardized via Android-style design tokens (`panel_theme.h`):

- **Background Surfaces**: Dark mode palette (`#0A0D14`, `#121721`, `#1A202C`).
- **Accent Colors**: Electric Blue (`#2563EB`), Emerald Green (`#10B981`), Crimson Red (`#EF4444`), Amber Orange (`#F59E0B`).
- **Card Styling**: 10px rounded corners with subtle dark border strokes.
- **Interactive Buttons**: 14px rounded pill buttons with tactile touch feedback states.
- **Universal UTF-8 Support**: UTF-8 string sanitizer (`utf8_utils.cpp`) ensuring robust handling of CJK, Cyrillic, Arabic, Latin-Extended, symbols, and emojis across LVGL components (`LV_TXT_ENC_UTF8`).

---

## 6. Native ESP-IDF Driver Architecture

To guarantee low latency, robust multi-tasking, and zero frame dropouts, all panel peripherals rely strictly on native ESP-IDF v6+ APIs:

1. **Backlight LEDC PWM (`panel_hardware.cpp`)**: 5 kHz PWM frequency with linear 8-bit brightness resolution.
2. **Asynchronous Ring-Buffered UART Bridge (`panel_comms.cpp`)**: 921,600 bps UART2 communication with 4KB DMA ring-buffers.
3. **I2S Audio DAC (`panel_audio.cpp`)**: ESP-IDF `driver/i2s_std.h` standard driver operating at 32-bit resolution up to 384 kHz sample rate.
4. **PCF8574 I2C Driver (`lcd1602_i2c.cpp`)**: Native I2C master driver sending HD44780 command sequences to the LCD 1602 display.
