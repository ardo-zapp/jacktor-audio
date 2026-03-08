# Jacktor Audio - Smart Amplifier Ecosystem

Jacktor Audio adalah ekosistem amplifier pintar dua-tingkat (Dual-Node) yang memisahkan kendali visual dari mesin pengolah audio menggunakan konektivitas JSON Serial. Repositori ini menampung seluruh komponen firmware (ESP32 & ESP32-S3), serta aplikasi pendukung (Desktop & Android).

## Arsitektur Sistem

- **Amplifier Unit (ESP32)**: Node bawah yang mengendalikan hardware *low-level* secara presisi. Menangani ADC I2S (untuk FFT Analyzer), kontrol PWM kipas, perlindungan suhu (OTP 85°C), relay daya, dan sinkronisasi RTC I2C. Komunikasi dilakukan melalui UART (Baud: 921600).
- **Panel Bridge (ESP32-S3)**: Node atas untuk antar-muka pengguna. Dilengkapi dengan PSRAM 8MB untuk mengoperasikan antarmuka grafis *LVGL 9.x* dengan layar sentuh *ILI9341* (resolusi 320x240, *Double-Buffered*). Melayani Web Server OTA, koneksi Wi-Fi, dan sinkronisasi waktu internet (NTP).

## Sorotan Fitur

1. **Dynamic Fan Control & Protection**: Sensor DS18B20 diakses secara non-blocking dengan filter *Low-Pass* untuk mendeteksi suhu *heatsink*. Menampilkan 3-point Fan Curve dengan anti-stall minimum duty.
2. **Audio Analyzer (FFT)**: Amplifier menghitung 32-band FFT dari input audio dan mentransmisikannya setiap ~30ms (`rt` frame) ke panel, dimana LVGL me-rendernya menggunakan Chart grafik bar.
3. **Smart Power Management**:
   - **PC Detect**: Membaca sinyal VBUS/Suspend lewat Native USB Panel.
   - **Sleep Timer**: Pengguna bisa menyetel Auto-Off dari Panel (15m, 30m, 45m ... 120m).
   - **OLED Screen Saver**: OLED amplifier otomatis meredup saat tidak aktif, meminimalisir *burn-in*.
4. **Wireless OTA**: Mengunggah firmware `.bin` dengan mudah menggunakan Web Browser (`http://[ip]/update`).

## Struktur Folder & Dokumentasi
- [`firmware/amplifier/README.md`](firmware/amplifier/README.md) — Penjelasan detail GPIO, protokol UART JSON, dan proteksi dari unit Amplifier.
- [`firmware/bridge/README.md`](firmware/bridge/README.md) — Detail lengkap sistem UI LVGL, pemetaan SPI LCD & Touch, serta setup Wi-Fi.

## Pengembangan & Kompilasi
PlatformIO telah disetup dengan *compiler flag* modern. Cukup jalankan:
```bash
python3 tools/setup_pio_clion.py
```
Atau jalankan manual melalui ekstensi VS Code.
