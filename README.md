# Jacktor Audio - Amplifier 

Jacktor Audio adalah ekosistem amplifier pintar berbasis ESP32 lengkap dengan panel bridge, aplikasi desktop (Electron), serta aplikasi Android (Compose) untuk kontrol dan telemetri. Repositori ini menampung seluruh komponen tersebut dalam satu monorepo.

> ESP-32 DEVKITC V4 WROOM-32U

## Struktur Repository

```
firmware/
  amplifier/    # Firmware ESP32 untuk unit amplifier utama
  bridge/       # Firmware ESP32 panel bridge (USB OTG + UART bridge)
  partitions/   # Tabel partisi OTA bersama (jacktor_audio_ota.csv)
panel/
  desktop/      # Aplikasi desktop Electron + React
  android/      # Aplikasi Android (Compose, target Android 16)
  docs/
```

Masing-masing subdirektori memiliki README yang mendokumentasikan GPIO, fitur, dan instruksi build spesifik.

## Sorotan Fitur

- Analyzer FFT 8/16/32/64 band dengan konfigurasi via CLI, cache NVS, dan telemetri lengkap (`analyzer{}` serta legacy array `an`).
- Telemetri 10 Hz saat aktif dan sinkron 1 Hz saat standby (berbasis SQW RTC) yang menyertakan objek `buzzer{enabled,last_tone,last_ms,quiet_now}` dan snapshot NVS.
- Auto power berbasis PC detect dengan cold-boot→standby, debounce/grace terpusat, dan event reason untuk tombol fisik, perintah panel, maupun deteksi PC.
- OTA terpadu: panel dapat memperbarui dirinya dan amplifier tanpa jalur EN/GPIO0; jalur `panel ota *` dan `ota *` tersedia dalam bentuk CLI maupun JSON.
- Sistem buzzer non-blocking dengan konfigurasi `dev/bz/*` (enabled, volume %, quiet hours) dan event `boot/shutdown/error/warn/bt/aux/click/custom`.

## Jalur Update Firmware

| Perangkat   | Mode OTA                                                                 | Mode Flash Langsung                           |
|-------------|---------------------------------------------------------------------------|-----------------------------------------------|
| Jacktor Audio Amplifier | Via panel bridge (UART2) menggunakan perintah `ota_*` line-based JSON.   | Colok micro-USB amplifier langsung dan flash memakai esptool/PlatformIO. |
| Jacktor Audio Panel Bridge | Via USB CDC/Android OTG memakai perintah `panel ota *` (CLI/JSON).       | Tekan BOOT + EN pada board panel dan flash melalui port USB panel.        |

- Kedua firmware memakai **file partisi yang sama**: `firmware/partitions/jacktor_audio_ota.csv` (layout dual-slot OTA + NVS). Hal ini menyederhanakan distribusi dan rollback karena image amplifier maupun panel memiliki ukuran maksimum yang sama.
- Selama OTA panel berlangsung, mesin OTG serta bridge UART dipause sehingga update tidak terganggu.
- OTA amplifier dijembatani panel tanpa kebutuhan jalur RX0/TX0 fisik; semua update rilis dilakukan via panel atau USB langsung pada perangkat amplifier.
- Panel bridge tidak lagi memegang pin EN/GPIO0 amplifier; pembaruan firmware sepenuhnya dilakukan via OTA.

## PlatformIO + CLion Quick Setup (AUTO)

Repositori ini berisi beberapa proyek **PlatformIO** di dalam direktori `firmware/`.  
Contoh proyek bawaan:
- `./firmware/amplifier`
- `./firmware/bridge`

Anda dapat menambahkan proyek baru di dalam `firmware/` dengan membuat folder berisi `platformio.ini`.

### Menjalankan setup otomatis
Script ini akan:
- Memastikan **PlatformIO** terinstal (via `pipx` jika perlu),
- Membuat wrapper absolut `tools/piow`,
- Menjalankan `pio init` (untuk kompatibilitas lama),
- Menjalankan `pio project init --ide clion` untuk setiap proyek di `firmware/*` yang memiliki `platformio.ini`.

Jalankan dari root repo:
```bash
python3 tools/setup_pio_clion.py
```

### Menentukan folder firmware lain (opsional)
Jika proyek Anda tidak berada di `./firmware/`, Anda dapat menentukan lokasi lain:
```bash
python3 tools/setup_pio_clion.py --firmware-root ./path/to/firmware
```

Script akan otomatis memindai semua subfolder di dalam `--firmware-root` yang berisi `platformio.ini`.

### Setelah setup
1. Buka **CLion**.  
2. Pastikan plugin **“PlatformIO for CLion”** telah diinstal dan diaktifkan:  
   - `File → Settings → Plugins → Marketplace → cari "PlatformIO for CLion" → Install`.  
3. Atur path PlatformIO (opsional, biasanya deteksi otomatis):
   - `File → Settings → Languages & Frameworks → PlatformIO`.  
   - Isi kolom **PlatformIO Core Executable** dengan path yang ditampilkan oleh script (misalnya `/home/ardo/.local/bin/pio` atau `tools/piow`).
4. Buka proyek langsung dari folder yang berisi `platformio.ini` (mis. `firmware/amplifier` atau `firmware/bridge`).

### Catatan
- Script **tidak mengubah atau menimpa** file `platformio.ini`.  
- Folder baru di `firmware/` akan otomatis terdeteksi saat Anda menjalankan script ulang.  
- Jika Anda menggunakan CLion versi sandbox (Snap/Flatpak), gunakan `tools/piow` sebagai path executable di pengaturan PlatformIO.

## UI Design (Landscape, Theme Blue)

Antarmuka panel (desktop & Android) mengusung tata letak **landscape** dengan grid 12 kolom, palet neon cyan (`#00CFFF / #00E6FF`), dan latar `#080B0E`.

- **Statusbar kanan** menampilkan RTC live, mode kipas (AUTO/CUSTOM/FAILSAFE), progres OTA, serta hitung mundur auto-off Bluetooth.
- **Indikator LINK/RX/TX** berada di blok telemetry: LINK solid jika frame diterima <3 detik, RX/TX berkedip 200 ms saat ada paket.
- **Analyzer 16-bar** memvisualisasikan telemetri amplifier secara real-time.
- **Factory Reset** dipindahkan ke halaman Settings dan membutuhkan PIN 4–6 digit (konfirmasi dua kali).
- **Tone Lab** menyediakan preset Simple, Sequence, Musical, dan Randomizer untuk hiburan saat demo.
- **Sinkronisasi data** memakai pola digest/etag (`nv_digest?` → `nv_get`/`nv_set`). NVS menjadi sumber kebenaran; aplikasi hanya menyimpan cache dan otomatis pulih setelah instal ulang.

## Dokumentasi Lanjutan

- `firmware/amplifier/README.md` — Fitur lengkap Jacktor Audio Amplifier, skema telemetri (termasuk `rtc_c` dan blok `features{}`), kebijakan OTA & RTC, serta langkah update.
- `firmware/bridge/README.md` — Mapping GPIO final Jacktor Audio Panel Bridge, state machine OTG adaptif, fallback power, handshake, perintah CLI panel, dan instruksi OTA panel.
- `panel/desktop/README.md` & `panel/android/README.md` — Roadmap UI, dependensi build, dan spesifikasi CLI→JSON.
