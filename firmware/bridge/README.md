# Panel Bridge (ESP32-S3) - LVGL 9.x

Firmware ini beroperasi sebagai terminal antarmuka pengguna (*High-Level User Interface*) utama pada ekosistem Jacktor Audio. Arsitekturnya mengeksploitasi mikrokontroler ESP32-S3 yang didukung oleh PSRAM 8MB dan memori Flash 16MB guna menyokong mesin komposisi grafis *embedded* tingkat lanjut.

## Arsitektur Rendering Tampilan
Menerapkan teknik **Full-Screen Double Buffering** sepenuhnya di dalam alokasi memori PSRAM eksternal (`MALLOC_CAP_SPIRAM`). Metodologi ini berhasil mengeliminasi cacat visual (*screen tearing*) dan memastikan seluruh transisi animasi antarmuka tereksekusi secara presisi (~60 FPS) dengan mendayagunakan transfer DMA (*Direct Memory Access*) pada latar belakang (*background thread*).

- **Tab Home**: Memvisualisasikan parameter diagnostik primer (tegangan suplai, VU Meter), serta konsol interaktif untuk aktuasi relai daya, pensaklaran speaker, dan siklus *Sleep Timer* adaptif (15 - 120 menit).
- **Tab Analyzer**: Mendayagunakan objek `lv_chart` guna merepresentasikan distribusi spektrum frekuensi audio (*32-Band FFT*) yang ditransmisikan secara sinkron dari unit Amplifier.
- **Tab Settings**: Menyediakan konfigurasi konektivitas nirkabel (Wi-Fi) terpadu menggunakan kibor layar sentuh. Waktu dari peladen NTP kawasan (*Timezone Asia/Jakarta*) kemudian didelegasikan ke unit Amplifier guna menjamin integritas modul RTC perangkat keras (DS3231).

## Spesifikasi Pemetaan Pin (Wiring / GPIO)

> **⚠️ PERINGATAN ELEKTRIKAL WIRING USB (VBUS 5V):**
> Merupakan suatu **LARANGAN** untuk mengkoneksikan jalur suplai VCC 5V (kabel merah USB) dari sistem *Host* (PC) menuju port USB ESP32-S3 jika mikrokontroler telah memperoleh suplai daya sekunder dari sirkuit internal amplifier. Memparalelkan dua sumber rel 5V dengan potensial diferensial akan memicu aliran arus balik (*Voltage Backfeeding*), yang **berpotensi merusak dan membakar komponen VBUS/Motherboard pada sistem Host Anda.** Eksklusif gunakan 3 konfigurasi kabel transmisi: **Data (D+), Data (D-), dan Ground (GND)**.

*Catatan: Jalur bus SPI untuk instrumentasi LCD dan kontroler Touchscreen diisolasi guna mencegah persaingan I/O (bottleneck) pada pengontrol DMA grafis.*

| Komponen | Pin (ESP32-S3) | Spesifikasi & Keterangan |
| :--- | :--- | :--- |
| **LCD ILI9341 (FSPI)** | | Dimensi Layar Aktif: 320x240 Piksel |
| MOSI | 11 | Kanal transmisi aliran data piksel layar. |
| MISO | 13 | (Tidak disyaratkan secara esensial oleh layar, diabaikan) |
| SCK | 12 | Persinyalan *Clock* SPI |
| CS | 10 | Pin Selektor Sirkuit (*Chip Select*) LCD |
| DC | 9 | Pemilah Register *Data/Command* |
| RST | 14 | Memicu *Hardware Reset* |
| BL | 21 | Catu Backlight (dikendalikan via sinyal PWM terpadu guna animasi *dimming/fade-out*) |
| **Touch XPT2046 (HSPI)**| | Berada pada bus independen (VSPI-equivalent) |
| MOSI | 35 | Kanal input perintah koordinat sentuh |
| MISO | 37 | Kanal *output* matriks sentuhan |
| SCK | 36 | Persinyalan *Clock* modul Touchscreen |
| CS | 38 | Pin Selektor Sirkuit Touchscreen |
| IRQ | 39 | Saluran *Hardware Interrupt* untuk deteksi interaksi dan *Wake-from-Sleep* |
| **Utilitas Lanjutan** | | |
| UART TX (Ke Amp) | 17 | Transmisi paket *Command* JSON (Baud 921,600 bps) |
| UART RX (Dari Amp) | 18 | Akuisisi paket *Telemetry* JSON (Baud 921,600 bps) |
| Native USB D+ | 20 | Aliran data USB CDC dan pendeteksi status *PC Suspend/Resume* |
| Native USB D- | 19 | Aliran data USB CDC sekunder |
| SD_CS | 40 | (Dialokasikan untuk arsitektur pengembangan di masa mendatang) |

## Pembaruan Berbasis Jaringan (Wi-Fi OTA)
Modul Panel akan mengaktifkan *Asynchronous Web Server* secara laten melalui port HTTP(80) bila status internet *online*. Proses pengisian pembaruan (*flashing firmware*) berkas kompilasi binari `.bin` diotorisasi langsung melalui direktori alamat `/update` pada *Web Browser* standar.
