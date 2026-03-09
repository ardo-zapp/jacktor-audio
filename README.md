# Jacktor Audio - Smart Amplifier Ecosystem

Jacktor Audio merupakan ekosistem amplifier pintar berarsitektur dua-tingkat (*Dual-Node*) yang memisahkan subsistem antarmuka visual dari mesin utama pemroses sinyal kontrol dan audio. Repositori ini merangkum seluruh komponen firmware terintegrasi (berbasis ESP32 dan ESP32-S3) serta utilitas pendukungnya.

## Arsitektur Sistem

- **Amplifier Unit (ESP32)**: Berperan sebagai unit kendali tingkat keras (*low-level ECU*). Menangani proses *Analog-to-Digital* I2S untuk *Fast Fourier Transform* (FFT), pengendalian modulasi *Pulse Width* (PWM) termal, proteksi batas suhu sistem (OTP 85°C), manajemen relai sirkuit daya, serta modul pewaktuan RTC presisi. Berkomunikasi asinkron via UART (921,600 bps).
- **Panel Bridge (ESP32-S3)**: Berperan sebagai terminal antarmuka pengguna interaktif (*High-level UI Node*). Berbekal PSRAM 8MB untuk mengeksekusi mesin grafis **LVGL 9.x** menggunakan layar sentuh TFT SPI (*Double-Buffered* tanpa *tearing*). Menyelenggarakan server diagnostik nirkabel (Web OTA), pengelolaan kredensial Wi-Fi, dan sinkronisasi standar waktu jaringan (*Network Time Protocol*).

## Sorotan Fitur

1. **Dynamic Thermal Control**: Modul sensor suhu DS18B20 dieksekusi melalui arsitektur pemrograman *non-blocking* dan dilengkapi *Low-Pass Filter* untuk meredam anomali pembacaan data. Kurva pendinginan menggunakan logika 3-titik adaptif dengan mitigasi putaran *anti-stall* kipas.
2. **Spectrum Analyzer (FFT)**: Unit Amplifier mengkalkulasi 32-band frekuensi audio dan memancarkannya dengan frekuensi penyegaran (~30Hz) menuju Panel Bridge, yang selanjutnya direpresentasikan secara grafis secara seketika (*real-time*).
3. **Smart Power Management**:
   - **Host Suspend Detection**: Melacak kondisi siaga (Sleep) komputer *host* via protokol Native USB CDC.
   - **Cyclic Sleep Timer**: Penjadwalan pemutusan daya mandiri yang dapat diatur pengguna melalui panel (15 - 120 menit).
   - **OLED Screen Saver**: Mitigasi retensi layar (*burn-in*) pada indikator OLED sekunder melalui penyesuaian kontras dinamis berdasarkan intensitas interaksi pengguna.
4. **Wireless OTA Update**: Mekanisme pembaruan firmware (flashing `.bin`) langsung melalui antarmuka *Web Browser*.

## Struktur Folder & Dokumentasi
- [`firmware/amplifier/README.md`](firmware/amplifier/README.md) — Penjelasan detail GPIO, protokol UART JSON, dan proteksi dari unit Amplifier.
- [`firmware/bridge/README.md`](firmware/bridge/README.md) — Detail lengkap sistem UI LVGL, pemetaan SPI LCD & Touch, serta setup Wi-Fi.

## Pengembangan & Kompilasi
PlatformIO telah disetup dengan *compiler flag* modern. Cukup jalankan:
```bash
python3 tools/setup_pio_clion.py
```
Atau jalankan manual melalui ekstensi VS Code.
