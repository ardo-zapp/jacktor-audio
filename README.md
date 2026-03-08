# Jacktor Audio - Amplifier 

Jacktor Audio adalah ekosistem amplifier pintar lengkap dengan panel bridge, aplikasi desktop (Electron), serta aplikasi Android (Compose) untuk kontrol dan telemetri. Repositori ini menampung seluruh komponen tersebut dalam satu monorepo.

## Struktur Repository

```
firmware/
  amplifier/    # Firmware ESP32 (biasa) untuk unit amplifier utama
  bridge/       # Firmware ESP32-S3 panel bridge (LCD ILI9341 + Touch + Native USB)
  partitions/   # Tabel partisi OTA bersama (jacktor_audio_ota.csv)
panel/
  desktop/      # Aplikasi desktop Electron + React
  android/      # Aplikasi Android (Compose, target Android 16)
  docs/
```

## Arsitektur Terbaru

- **Amplifier (ESP32)**: Menangani I2S, FFT Analyzer, Kontrol PWM Kipas, dan deteksi sensor Dallas (Non-blocking).
- **Panel Bridge (ESP32-S3)**: Menangani UI modern dengan LVGL 9.x pada layar 3.2" ILI9341 SPI Touchscreen. Memakai `Native USB CDC` untuk koneksi PC sekaligus pendeteksi `USB Suspend` saat PC Sleep untuk mengendalikan auto off Amplifier.
- *Fitur OTG Android telah dihapuskan dari Bridge.* Koneksi kini sepenuhnya mengandalkan Bluetooth atau komunikasi serial native USB.

## Dokumentasi Spesifik

- `firmware/amplifier/README.md` — Detail fungsi dan telemetri ESP32 Amplifier.
- `firmware/bridge/README.md` — Mapping PIN ESP32-S3 SPI, Touchscreen XPT2046, dan instruksi UI LVGL 9.x.

## PlatformIO + CLion Quick Setup (AUTO)

Jalankan dari root repo:
```bash
python3 tools/setup_pio_clion.py
```
