# Jacktor Audio Panel Bridge (ESP32-S3)

Firmware ini berjalan di atas ESP32-S3 yang didedikasikan sebagai panel kendali utama. Memiliki layar sentuh TFT ILI9341 3.2" dan antarmuka modern menggunakan LVGL 9.x.

## Pin Mapping ESP32-S3

*Bridge ini tidak lagi menggunakan modul USB OTG Android.* Native USB D+/D- dari ESP32-S3 (GPIO 19 dan 20) dihubungkan langsung ke PC untuk USB Serial (CDC) dan mendeteksi PC Sleep (USB Suspend).

### SPI 2 (FSPI) - TFT LCD ILI9341
- `MOSI` : GPIO 11
- `MISO` : GPIO 13
- `SCK`  : GPIO 12
- `CS`   : GPIO 10
- `DC`   : GPIO 9
- `RST`  : GPIO 14
- `LED/BL`: GPIO 21

### SPI 3 (VSPI) - Touchscreen XPT2046 & SD Card
(Terpisah dari LCD untuk menghindari lag pada DMA LVGL)
- `MOSI` : GPIO 35
- `MISO` : GPIO 37
- `SCK`  : GPIO 36
- `CS`   : GPIO 38
- `IRQ`  : GPIO 39
- `SD_CS`: GPIO 40

### UART - Komunikasi dengan Amplifier
- `TX`   : GPIO 17
- `RX`   : GPIO 18 (Cross ke RX/TX Amplifier)
