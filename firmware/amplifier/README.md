# Unit Amplifier (ESP32)

Firmware ini berfungsi sebagai unit kontrol pusat (*Electronic Control Unit* / ECU) pada ekosistem Jacktor Audio. Didesain secara *headless* (hanya dibantu indikator OLED minimalis via I2C), modul ini dititikberatkan pada presisi *Real-Time Operating System* (RTOS) guna mengeksekusi konversi data *Analog-to-Digital* I2S, kalkulasi FFT 32-Band, dan akuisisi data termal.

## Manajemen Keselamatan Sistem (*Hardware Safety Features*)
- **Over-Temperature Protection (OTP)**: Berfungsi melacak batasan termal *heatsink* melalui sensor `DS18B20`. Jika suhu mencapai ambang kritis 85°C (setelah melalui proses *low-pass filter*), amplifier akan menginisiasi putus-daya darurat (*Force Shutdown*) guna mencegah malfungsi komponen semikonduktor, disertai dengan aktivasi alarm akustik.
- **SMPS Hardware Fault**: Memanfaatkan sirkuit *optocoupler* (pada konfigurasi pin `SMPS_FAULT_PIN`) untuk memonitor indikator LED limit/hubung-singkat (*short-circuit*) dari suplai daya SMPS. Saat anomali tegangan terdeteksi, unit akan secara **instan mengeksekusi shutdown paksa** untuk mengamankan sirkuit.
- **Anti-Stall Fan Control**: Pengendalian profil putaran kipas (*PWM duty cycle*) dialokasikan pada nilai dasar `450` (dari spektrum 0-1023). Hal ini menjamin putaran mekanis kipas tidak terhenti (bebas efek *stalling*) pada suhu kurang dari 40°C demi menjaga stabilitas peluruhan suhu. Implementasi tes lonjakan daya putar awal (*kickstart*) saat *booting* dipertahankan selama 5 detik.
- **Asynchronous Sensor Polling**: Implementasi pembacaan suhu kini dikonfigurasi melalui metode *non-blocking* (`setWaitForConversion(false)`). Resolusi asinkron ini membebaskan instruksi tunggu pada *thread* utama, memastikan sirkulasi transmisi telemetri 30Hz ke Panel Bridge tidak mengalami hambatan komputasi.
- **Sleep Timer Lifecycle**: Melalui sinkronisasi dari Panel Bridge (dalam interval 15 - 120 menit), sirkuit RTC ESP32 akan memverifikasi *target epoch time* secara mandiri, yang selanjutnya memutus pasokan daya relai sirkuit amplifier saat siklus berakhir.

## Antarmuka Komunikasi JSON

Amplifier menggunakan protokol komunikasi asinkron *Full-Duplex* UART dengan laju *Baud Rate* **921,600 bps**, dan secara rutin mentransmisikan struktur paket:
1. **`rt` (Realtime) ~ 30 Hz**: Memuat paket data array 32-Band FFT, status VU, dan mode persinyalan input (Bluetooth/AUX).
2. **`hz1` (Diagnostic) ~ 1 Hz**: Memuat pembacaan tegangan catu daya (SMPS & 12V), sisa durasi *sleep timer*, derajat termal aktual (`heat_c`), status relai, serta parameter diagnotik `errors[]` (aktif saat deteksi kegagalan perangkat, e.g. *speaker protection* atau OTP).

Setiap instruksi valid yang disalurkan dari antarmuka Panel Bridge (sebagai contoh: `{"type":"cmd","cmd":{"power":false}}`) akan disinkronisasikan bersama dengan *auditory feedback* pendek (`buzzerClick()`), yang mengkonfirmasi penyelesaian instruksi di ranah perangkat keras.
