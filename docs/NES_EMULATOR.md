# DSN NES Emulator — ESP32-S3 Panel Technical Documentation

## 1. Overview & System Architecture

The **DSN NES Emulator** is a high-performance, embedded Nintendo Entertainment System (NES) emulator running on the **Jacktor Audio ESP32-S3 Panel** system. Built on top of the **Nofrendo** emulation core, it leverages pure **ESP-IDF v6.0+ / FreeRTOS** APIs (zero Arduino dependencies), PSRAM double-buffering, and direct SPI DMA display transfers to achieve full-speed 60 FPS emulation without display flickering.

```
+-----------------------------------------------------------------------------------+
|                            ESP32-S3 Panel Firmware                                |
|                                                                                   |
|  +---------------------+   +-----------------------+   +-----------------------+  |
|  |   NES Manager       |   |   NES Config (NVS)    |   |    NES Menu BGM Task  |  |
|  | - ROM Scanner       |   | - P1 & P2 Mappings    |   | - 8-Bit Chiptune Synth|  |
|  | - Task Lifecycle    |   | - Persistent Storage  |   | - Auto Pause/Resume   |  |
|  +----------+----------+   +-----------+-----------+   +-----------+-----------+  |
|             |                          |                           |              |
|             v                          v                           v              |
|  +-----------------------------------------------------------------------------+  |
|  |                     Nofrendo NES Emulation Engine                           |  |
|  |  - 6502 CPU Core       - PPU Frame Buffer (PSRAM)   - APU Sound Processor   |  |
|  +---------------------+-------------------------------------------+-----------+  |
|                        |                                           |              |
|                        v                                           v              |
|  +-------------------------------------+   +-----------------------------------+  |
|  | ILI9341 SPI DMA Display (256x240) |   | GY-PCM5102 I2S Audio DAC          |  |
|  +-------------------------------------+   +-----------------------------------+  |
+-----------------------------------------------------------------------------------+
```

---

## 2. SD Card Setup & ROM Scanner

### Directory Auto-Creation
Upon system initialization (`panel_hardware_init()`), the firmware mounts the SD Card at `/sdcard` using SDSPI and automatically creates the required directory if it does not exist:
```
/sdcard/NES/
```

### ROM Scanner & Empty-State UI
1. **File Scanning**: `nes_manager_scan_roms()` opens `/sdcard/NES` and filters files ending with `.nes` or `.NES` (ignoring hidden files starting with `.`).
2. **Alphabetical Sorting**: Scan results are automatically sorted alphabetically for clean navigation.
3. **Empty / Missing State**: If the SD Card is not mounted or `/sdcard/NES` contains no `.nes` files, the NES Launcher UI displays a friendly card:
   - **Title**: `"No .nes Games Found"`
   - **Guidance**: `"Please insert an SD card with .nes files inside the /sdcard/NES directory."`
   - **Action**: `"REFRESH SD"` button to re-trigger mounting and directory scanning on demand.

---

## 3. Display Rendering & PSRAM Double Buffering

### Specifications
* **Native NES Resolution**: 256 x 240 pixels.
* **ILI9341 Panel Resolution**: 320 x 240 pixels (Centered with 32px black margins on left and right).
* **Color Format**: 16-bit RGB565 (Big-Endian byte-swapped for SPI DMA).

### Eliminating Screen Flicker
1. **PSRAM Frame Allocation**: PPU rendering uses a double-buffered 256x240 RGB565 buffer allocated in PSRAM using `heap_caps_malloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`.
2. **Direct SPI DMA Streaming**: During gameplay, the LVGL timer task (`lv_timer_handler()`) is paused to prevent concurrent SPI bus access. Frames are pushed directly from PSRAM to the ILI9341 display via SPI DMA chunks (`ili9341_draw_direct_frame`), bypassing LVGL rendering overhead and achieving zero screen flicker.

---

## 4. Audio Subsystem & Menu BGM

### Hardware Driver
* **Audio DAC**: GY-PCM5102 32-Bit I2S DAC connected to ESP32-S3 I2S pins (BCK = GPIO 21, DOUT = GPIO 47, WS = GPIO 48, SCK = GND).
* **Sample Rate**: Configurable up to 384 kHz; NES APU runs at 30,000 Hz.

### NES APU Sound Output
1. The NES APU generates 16-bit mono PCM audio samples at 30,000 Hz.
2. `vid_flush()` upscales 16-bit mono samples to 32-bit stereo PCM streams (`stereo_audio_buffer_32`).
3. Audio is written directly to the GY-PCM5102 DAC via `panel_audio_write()`.

### 8-Bit NES Menu Background Music (BGM)
* **Chiptune Synthesizer**: `nes_bgm.cpp` runs a dedicated FreeRTOS task generating an arpeggiated 8-bit NES style menu melody (50% square wave lead + triangle wave sub-octave bass).
* **Auto Pause & Resume**:
  - Starts and loops seamlessly when browsing the NES Game Launcher menu.
  - Automatically **pauses** when a game is launched.
  - Automatically **resumes** when exiting back to the game list menu.
  - Automatically **stops and deletes** the task when leaving the NES application.

---

## 5. 2-Player DualShock 2 Gamepad Mapping & NVS Storage

### Dual Controller Support
The emulator core supports two simultaneous players:
* **Player 1**: Registered as NES `INP_JOYPAD0`.
* **Player 2**: Registered as NES `INP_JOYPAD1`.

### UART Telemetry Ingestion
Gamepad input states arrive via UART JSON packets from the USB OTG controller interface:
```json
{"type": "gamepad", "player": 1, "state": 512}
{"type": "gamepad", "p1": 257, "p2": 514}
```

### Persistent NVS Configuration
* **NVS Namespace**: `nes_config`
* **Default Mapping**:
  - **NES A**: DualShock Cross (`1 << 0`)
  - **NES B**: DualShock Circle (`1 << 1`)
  - **NES SELECT**: DualShock Select (`1 << 4`)
  - **NES START**: DualShock Start (`1 << 5`)
  - **NES D-Pad**: Up, Down, Left, Right (`1 << 6` .. `1 << 9`)
* Custom mappings can be reconfigured in the **Controller Settings** UI and saved persistently across reboots using `nes_config_save()`.

---

## 6. In-Game Navigation & Exit Hotkeys

### Exit Mechanisms
1. **Semi-Transparent Touch Overlay**:
   - Touching the screen anytime during gameplay displays a 5-second semi-transparent top navigation bar across scanlines Y = 0..24.
   - Touching the top overlay area (Y < 40) requests game exit, cleanly terminating the emulation task and returning to the game list menu.
2. **DualShock 2 Gamepad Hotkey**:
   - Holding `SELECT + START` simultaneously on Player 1 or Player 2 controller for **1 second** triggers an automatic game exit back to the NES menu.

---
