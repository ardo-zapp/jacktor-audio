# Unit Amplifier (ESP32)

Firmware ini bertugas sebagai *Core Engine* atau ECU dari ekosistem Jacktor Audio. Tidak memiliki antarmuka grafis besar (hanya U8g2 I2C OLED statis), melainkan fokus pada waktu tanggap *real-time* (RTOS) untuk pembacaan sensor dan komputasi FFT 32-Band I2S.

## Sistem Keamanan Lanjutan (Safety Features)
- **Over-Temperature Protection (OTP)**: Jika sensor `DS18B20` mendeteksi suhu *heatsink* menembus 85°C (setelah disaring *low-pass filter*), amplifier akan melakukan *Force Shutdown* mendadak mematikan relay dan membunyikan alarm.
- **Anti-Stall Fan Control**: *Minimum duty cycle* kipas PWM dipatok pada `450` (rentang 0-1023). Hal ini menjamin bilah kipas terus bergulir pelan mendisipasikan sisa inersia panas pada suhu di bawah 40°C. Waktu tes lonjakan *kickstart* saat boot dinaikkan menjadi 5 detik.
- **Asynchronous Sensor Polling**: DallasTemperature kini dijalankan menggunakan format `setWaitForConversion(false)`. Penghapusan sifat pemblokiran ini melancarkan ritme loop agar data telemetri 30Hz ke Panel tidak pernah macet.
- **Sleep Timer**: Panel Bridge dapat mengirimkan menit parameter (15..120). ESP32 Amplifier akan mencatat *target epoch* ke depan berbasis RTC, dan akan secara otomatis membunuh daya listrik relay jika batas waktu terlewati.

## Protokol Komunikasi JSON

Amplifier berjalan pada Baud Rate **921,600**. Secara rutin memuntahkan paket:
1. **`rt` (Realtime) ~ 30 Hz**: Memuat 32 array FFT Band, status VU, dan mode Input (BT/AUX).
2. **`hz1` (Lambat) ~ 1 Hz**: Memuat tegangan SMPS/12V, sisa menit dari `sleep_timer`, suhu `heat_c`, mode power, dan array `errors[]` (jika perlindungan speaker rusak).

Jika Panel Bridge mengirim perintah (e.g. `{"type":"cmd","cmd":{"power":false}}`), unit Amplifier akan membunyikan `buzzerClick()` sebagai penanda bahwa instruksi berhasil dijalankan secara fisik.
