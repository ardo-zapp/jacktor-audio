# Jacktor Audio - Amplifier Firmware (ESP32)

Firmware untuk board Jacktor Audio Amplifier. Memonitor sensor (DS18B20 secara asinkron/non-blocking), ADC ADS1115 (voltage SMPS dan 12V), serta mengatur FAN PWM berdasarkan suhu dan perlindungan speaker (protection).

## Konektivitas
- Komunikasi dengan Bridge (ESP32-S3) melalui UART2.
- Mengirim telemetri (suhu, daya, status, analyzer) dalam bentuk JSON.
- Mendapatkan perintah (mode fan, power on/off) dari PC/Bridge.

## Fitur Terkini
- **Cold Boot Fan Test**: Saat unit dihidupkan, kipas akan diberi daya penuh selama 5 detik agar memutar kencang awal (kickstart).
- **Anti-Stall Fan Duty**: Nilai mininum kipas PWM diset ke 450/1023 agar pada suhu rendah di bawah 40°C bilah tetap terus berputar halus tanpa berhenti yang bisa menyebabkan inersia panas terlambat dihempaskan.
- **Asynchronous Temperature Probe**: `DS18B20` tidak lagi mem-blok RTOS karena menggunakan `setWaitForConversion(false)`.

(Lihat konfigurasi teknis lainnya di `include/config.h`)
