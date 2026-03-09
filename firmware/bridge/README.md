# Panel Bridge (ESP32-S3) - LVGL 9.x

Firmware ini bertugas sebagai UI (User Interface) utama ekosistem Jacktor Audio. Ia menggunakan ESP32-S3 dengan PSRAM 8MB dan Flash 16MB untuk menyokong mesin grafis tingkat lanjut.

## Arsitektur Tampilan
Menggunakan teknik **Full-Screen Double Buffering** di memori PSRAM (`MALLOC_CAP_SPIRAM`). Teknik ini menghilangkan gejala *tearing* dan membuat animasi UI berjalan sangat mulus (60 FPS) berkat transfer DMA latar belakang.

- **Tab Home**: Menampilkan status tegangan, VU Meter, kontrol daya, speaker, serta tombol Sleep Timer (15-120 menit).
- **Tab Analyzer**: Memanfaatkan `lv_chart` untuk menggambarkan grafis respons frekuensi 32-band yang tersinkronisasi dari Amplifier via UART.
- **Tab Settings**: Konektivitas Wi-Fi terintegrasi dengan layar sentuh (Virtual Keyboard). Jam dari server NTP Asia/Jakarta diteruskan ke unit Amplifier agar hardware RTC (DS3231) selalu akurat.

## Pemetaan Pin / Wiring (ESP32-S3)

> **⚠️ PERINGATAN KERAS WIRING USB (VCC):**
> Anda **TIDAK BOLEH** menghubungkan kabel merah VCC (5V) dari port USB PC ke port USB ESP32-S3 jika ESP32-S3 juga ditenagai dari dalam amplifier (adaptor). Memparalel dua tegangan 5V dari sumber berbeda dapat menyebabkan *backfeeding* yang berpotensi **membakar port USB Motherboard PC Anda!** Cukup sambungkan 3 kabel saja dari PC: **D+, D-, dan Ground**.

*Catatan: Semua SPI bus dipisah untuk mencegah *bottleneck* performa dari DMA layar dengan Polling sentuhan.*

| Komponen | Pin (ESP32-S3) | Fungsi & Keterangan |
| :--- | :--- | :--- |
| **LCD ILI9341 (FSPI)** | | Resolusi Layar: 320x240 |
| MOSI | 11 | Jalur data utama untuk render gambar. |
| MISO | 13 | (Sering tidak dipakai oleh LCD, diabaikan) |
| SCK | 12 | SPI Clock |
| CS | 10 | Chip Select LCD |
| DC | 9 | Data/Command pin |
| RST | 14 | Hardware Reset |
| BL | 21 | Backlight (Dikonfigurasi PWM untuk animasi fade-out) |
| **Touch XPT2046 (HSPI)**| | SPI terpisah (VSPI) agar DMA tidak tersendat. |
| MOSI | 35 | Jalur data input |
| MISO | 37 | Jalur data output (koordinat) |
| SCK | 36 | Touch SPI Clock |
| CS | 38 | Chip Select Touchscreen |
| IRQ | 39 | Interrupt deteksi sentuhan (*wake from sleep*) |
| **Lainnya** | | |
| UART TX (Ke Amp) | 17 | Kirim perintah JSON (Baud 921,600) |
| UART RX (Dari Amp) | 18 | Terima telemetri JSON (Baud 921,600) |
| Native USB D+ | 20 | USB CDC & Deteksi status Sleep (PC) |
| Native USB D- | 19 | USB CDC |
| SD_CS | 40 | (Akan digunakan pada update ke depan) |

## Wi-Fi OTA
Panel ini menyalakan *Web Server Asynchronous* di port 80 setelah terhubung ke internet. Anda bisa melakukan flash file `.bin` langsung melalui rute `/update`.
