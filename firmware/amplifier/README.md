# Jacktor Audio - Amplifier Firmware

Firmware berbasis ESP32. Perangkat ini mengelola proteksi SMPS, kipas, input Bluetooth/AUX, UI OLED,
telemetri JSON ke panel bridge, serta pola buzzer non-blocking. Seluruh parameter runtime dipersist di **NVS** (kecuali relay
utama yang wajib OFF saat boot dingin) sehingga konfigurasi bertahan lintas restart.

---

## Daftar Isi

1. [Fitur Utama](#fitur-utama)
2. [Factory Reset](#factory-reset)
3. [GPIO Mapping](#gpio-mapping)
4. [Build & Flash](#build--flash)
5. [Telemetri JSON](#telemetri-json)
6. [Skema Command UART](#skema-command-uart)
7. [Catatan OTA](#catatan-ota)
8. [Kebijakan RTC Sync](#kebijakan-rtc-sync)
9. [Feature Toggles & Buzzer](#feature-toggles--buzzer)

---

## Fitur Utama

| Kelompok | Ringkasan |
|----------|-----------|
| **Proteksi & Power** | Proteksi SMPS 65 V dengan ambang cut/recovery yang dapat dikonfigurasi (opsi bypass), sakelar `FEAT_SMPS_PROTECT_ENABLE` dan `SAFE_MODE_SOFT`, serta relai utama yang default OFF saat boot. Cold-boot selalu masuk standby; ON hanya terjadi ketika `powerSetMainRelay(..., reason)` dipicu oleh tombol, command, atau PC detect (GPIO34) yang mematuhi debounce & grace. |
| **Monitoring & Analyzer** | Voltmeter ADS1115 (divider R1=201.2 kΩ / R2=10.03 kΩ), sensor suhu heatsink DS18B20, pembacaan suhu internal RTC DS3231 (`rtc_c`), serta analyzer FFT 8/16/32/64 band + VU meter. Mode dan jumlah band dipersist di NVS dan otomatis dinonaktifkan saat standby. |
| **Pendinginan** | Mode kipas AUTO/CUSTOM/FAILSAFE dengan PWM 25 kHz, self-test (`FEAT_FAN_BOOT_TEST`), dan duty khusus yang disimpan di NVS. |
| **Antarmuka & Buzzer** | OLED 128×64: standby menampilkan jam besar, mode RUN memuat status input, tegangan, suhu, indikator proteksi, serta VU/analyzer. Buzzer LEDC non-blocking menangani event `boot`, `shutdown`, `error`, `warn`, `bt`, `aux`, `click`, dan `custom` dengan konfigurasi `dev/bz/*` (enabled, volume %, quiet hours). |
| **Telemetri** | Dua kanal JSON: `rt` (~30 Hz saat ON) membawa `mode/bands_len/vu/bands[]` untuk analyzer + indikator LINK/RX/TX, sementara `hz1` (1 Hz sinkron SQW) memuat status lengkap (SMPS, suhu, error list, snapshot NVS, fitur aktif, dan metadata buzzer). |
| **OTA & RTC** | OTA streaming via UART (CRC32 + ack per chunk) dan sinkronisasi RTC dengan kebijakan offset > 2 s serta rate-limit 24 jam (`FEAT_RTC_SYNC_POLICY`). |
| **Persistensi** | Semua pengaturan runtime disimpan di NVS; factory reset tersedia via kombinasi tombol Power+BOOT maupun perintah UART dengan log audit. |

---

## Factory Reset

Kedua jalur berikut menghapus seluruh NVS, menampilkan pesan pada OLED, membunyikan buzzer dua kali, memastikan relay utama tetap OFF, menerbitkan log `factory_reset_executed`, dan melakukan reboot otomatis.

### A. Manual Combo (Power + BOOT)

1. Saat menyalakan amplifier, tekan dan tahan tombol Power utama (`BTN_POWER_PIN`, GPIO13) bersama tombol BOOT (GPIO0).
2. Setelah ±1 detik, OLED menampilkan `FACTORY RESET`, buzzer berbunyi dua kali, dan perangkat langsung menghapus NVS sebelum restart.

### B. Factory Reset via Panel (UART)

1. Pastikan amplifier berada dalam kondisi standby (`powerIsOn() == false`).
2. Kirim perintah berikut:

   ```json
   {"type":"cmd","cmd":{"factory_reset":true}}
   ```

3. Panel menerima ACK sukses:

   ```json
   {"type":"ack","ok":true,"changed":"factory_reset","value":true}
   ```

4. Amplifier menampilkan `FACTORY RESET (UART)` dan melakukan reboot setelah NVS dibersihkan.
5. Jika perintah dikirim saat amplifier ON, balasan menjadi `{"type":"ack","ok":false,"changed":"factory_reset","error":"system_active"}` dan reset tidak dijalankan.

---

## GPIO Mapping

| GPIO | Fungsi | Catatan |
|-----:|--------|---------|
| 2 | UART activity LED | Indikasi TX/RX UART2 |
| 4 | Bluetooth enable | Auto-off 5 menit idle/AUX |
| 5 / 19 / 18 | Tombol Play / Prev / Next | Kontrol modul Bluetooth |
| 13 | Tombol Power (`BTN_POWER_PIN`) | Aktif LOW, debounced; dipakai combo factory reset |
| 14 | Relay utama | OFF default saat boot |
| 16 / 17 | UART2 RX/TX | Ke panel bridge |
| 21 / 22 | I²C SDA/SCL | RTC + ADS1115 + OLED |
| 23 | Status Bluetooth (aktif LOW) | AUX→LOW≥3 s→BT (`FEAT_BT_AUTOSWITCH_AUX`) |
| 25 | Speaker power switch | Suplai modul proteksi speaker |
| 26 | Speaker selector | Persist di NVS |
| 27 | DS18B20 sensor suhu | Heatsink |
| 32 | Fan PWM output | Mode AUTO/CUSTOM/FAILSAFE; self-test via `FEAT_FAN_BOOT_TEST` |
| 33 | Buzzer | Pola non-blocking LEDC |
| 34 | PC detect via opto | LOW = PC ON (`FEAT_PC_DETECT_ENABLE`) |
| 35 | RTC SQW input | 1 Hz DS3231 |
| 36 | I²S analyzer input | FFT 16 band |
| 39 | Speaker protector sense | HIGH = normal; fault → `SPEAKER_PROTECT_FAIL` |

---

## Build & Flash

1. Pastikan dependensi PlatformIO terpasang.
2. Masuk ke folder firmware amplifier:
   ```bash
   cd firmware/amplifier
   pio run
   ```
3. Untuk flash:
   ```bash
   pio run -t upload
   ```
4. Serial monitor default berada pada 921600 baud (`pio device monitor -b 921600`).

Proyek ini memakai tabel partisi bersama `../partitions/jacktor_audio_ota.csv` (dua slot OTA + NVS) yang identik dengan firmware Jacktor Audio Bridge, sehingga paket rilis berbagi layout memori yang sama.

### Update Firmware

1. **OTA via Panel (disarankan)**
   - Pastikan panel bridge telah tersambung ke amplifier dan aplikasi host (desktop/Android) sudah melakukan handshake.
   - Kirim urutan perintah berikut melalui panel:
     ```text
     ota begin size <SIZE> [crc32 <HEX>]
     ota write <B64>
     ota end [reboot on|off]
     ```
     atau gunakan format JSON langsung:
     ```json
     {"type":"cmd","cmd":{"ota_begin":{"size":123456,"crc32":"ABCD1234"}}}
     {"type":"cmd","cmd":{"ota_write":{"seq":0,"data_b64":"..."}}}
     {"type":"cmd","cmd":{"ota_end":{"reboot":true}}}
     ```
   - Amplifier menerbitkan event `{"type":"ota","evt":"begin_ok|write_ok|end_ok|abort_ok|error"}` untuk tiap tahap. Selama OTA aktif, panel menahan aksi destruktif lain.
   - Field telemetri `features{}` mencerminkan flag `FEAT_*` yang sedang aktif sehingga UI dapat menyesuaikan perilaku.

2. **Flash langsung via USB amplifier**
   - Buka cover amplifier, sambungkan port micro-USB bawaan ke PC.
   - Masuk ke `firmware/amplifier`, kemudian jalankan `pio run -t upload` atau gunakan `esptool.py` seperti biasa.
   - Jalur RX0/TX0 melalui panel **tidak tersedia**; update langsung hanya lewat port USB internal amplifier.

---

## Telemetri JSON

Firmware menerbitkan **dua** frame JSON terpisah melalui UART/Wi-Fi/BLE:

- **`rt` (real-time)** – ~30 Hz selama amplifier ON. Memuat `mode`, `bands_len`, `update_ms`, `vu`, opsi `bands[]` (hanya bila `mode="fft"`), indikator link `{alive,rx_ms,tx_ms}`, serta status input/bt.
- **`hz1` (1 Hz)** – Sinkron dengan pulsa SQW RTC (fallback timer internal jika SQW absen). Membawa status lengkap: waktu, versi firmware, status OTA, blok `smps{v,stage,cutoff,recover}`, suhu (`heat_c`,`rtc_c`), `inputs{}`, `states{}`, `errors[]`, `pc_detect{enabled,armed,level,last_change_ms}`, snapshot analyzer, konfigurasi buzzer, isi NVS penting, dan daftar feature toggle aktif.

Contoh frame `rt`:

```json
{
  "type": "telemetry",
  "rt": {
    "mode": "fft",
    "bands_len": 16,
    "update_ms": 33,
    "vu": 128,
    "bands": [3, 5, 8, 14, 21, 32, 27, 18, 11, 7, 5, 3, 2, 1, 0, 0],
    "link": {"alive": true, "rx_ms": 12, "tx_ms": 24},
    "input": "bt",
    "bt_state": "bt"
  }
}
```

Contoh frame `hz1`:

```json
{
  "type": "telemetry",
  "hz1": {
    "time": "2025-10-30T12:34:56Z",
    "fw_ver": "amp-1.0.0",
    "ota_ready": true,
    "smps": {"v": 53.8, "stage": "armed", "cutoff": 50.0, "recover": 52.0},
    "heat_c": 36.2,
    "rtc_c": 28.5,
    "inputs": {"bt": true, "speaker": "big"},
    "states": {"on": true, "standby": false},
    "errors": ["LOW_VOLTAGE"],
    "pc_detect": {"enabled": true, "armed": true, "level": "LOW", "last_change_ms": 420},
    "analyzer": {"mode": "fft", "bands_len": 16, "update_ms": 33, "vu": 128},
    "buzzer": {"enabled": true, "last_tone": "boot", "last_ms": 123456, "quiet_now": false},
    "nvs": {"fan_mode": 0, "fan_mode_str": "auto", "spk_big": true, "smps_bypass": false},
    "features": {"pc_detect": true, "smps_protect": true, "factory_reset_combo": true}
  }
}
```

`errors` berisi kombinasi `LOW_VOLTAGE`, `NO_POWER`, `SENSOR_FAIL`, dan/atau `SPEAKER_PROTECT_FAIL` (boleh kosong). Objek `analyzer` di `hz1` menyertakan daftar `bands[]` hanya jika mode saat ini `fft`; UI dapat menggunakan `rt` untuk animasi dan `hz1` untuk sinkronisasi state.

---

## Feature Toggles & Buzzer

Semua sakelar diagnostik tersedia di `include/config.h` sehingga perilaku firmware dapat diubah tanpa menyentuh modul lain.

- `FEAT_PC_DETECT_ENABLE` — aktifkan otomatis ON/OFF berbasis sinyal PC detect.
- `FEAT_BT_AUTOSWITCH_AUX` — izinkan pindah AUX↔BT ketika level AUX menahan LOW ≥3 s.
- `FEAT_FAN_BOOT_TEST` — jalankan self-test kipas beberapa ratus milidetik saat boot.
- `FEAT_FACTORY_RESET_COMBO` — kombinasikan BTN_POWER + BOOT di startup untuk factory reset.
- `FEAT_RTC_TEMP_TELEMETRY` — sertakan `rtc_c` di telemetri.
- `FEAT_RTC_SYNC_POLICY` — tegakkan syarat offset >2 s dan rate-limit 24 jam saat sync RTC.
- `FEAT_SMPS_PROTECT_ENABLE` — hidup/matikan logika proteksi tegangan SMPS.
- `FEAT_FILTER_DS18B20_SOFT` — aktifkan filter software suhu DS18B20 (opsional).
- `SAFE_MODE_SOFT` — paksa output kritis OFF (relay, speaker power, BT) untuk troubleshooting.

Buzzer LEDC (GPIO33) berjalan non-blocking dan menyimpan konfigurasi di NVS namespace `dev/bz`:
- `dev/bz/enabled` (`bool`, default `true`)
- `dev/bz/volume` (`uint8_t`, 0–100%, default 100)
- `dev/bz/quiet` (jam mulai/akhir; event non-fatal dibisukan selama rentang ini, sedangkan `boot`/`shutdown`/`error` tetap berbunyi)

Event yang dipicu otomatis:

| Event | Pola / Catatan |
|-------|----------------|
| Boot selesai init / power ON | Nada menaik 880→1175→1568 Hz |
| Masuk standby / shutdown | Nada menurun 1568→1175→880 Hz |
| Pindah ke BT | 1568 Hz 60 ms, jeda 40 ms, 2093 Hz 80 ms |
| Pindah ke AUX | 1175 Hz 60 ms |
| Klik UI / ACK command sukses | Klik 3 kHz 25 ms |
| Warning (misal sensor hilang) | 1175 Hz 70 ms, sekali per rising edge, dibisukan saat quiet hours |
| Error fatal (`SPEAKER_PROTECT_FAIL`, `LOW_VOLTAGE`, `NO_POWER`) | 880 Hz 70 ms → jeda 100 ms → 880 Hz 120 ms, sekali per rising edge |
| Custom tone | Sesuai parameter CLI (`buzz`), tetap menghormati rate-limit ≥150 ms |

---

## Skema Command UART

Semua request memakai struktur `{"type":"cmd","cmd":{...}}`. Balasan standar untuk setter konfigurasi:

```json
{"type":"ack","ok":true,"changed":"<key>","value":<val>}
```

Apabila gagal: `{"type":"ack","ok":false,"changed":"<key>","error":"range|invalid|nvs_fail"}`.

### Kontrol dasar

| Command | Keterangan |
|---------|------------|
| `{"type":"cmd","cmd":{"power":true}}` | ON/OFF relay utama |
| `{"type":"cmd","cmd":{"bt":false}}` | Enable / disable modul Bluetooth |
| `{"type":"cmd","cmd":{"spk_sel":"small"}}` | Pilih speaker kecil |
| `{"type":"cmd","cmd":{"spk_pwr":true}}` | Suplai speaker protector |
| `{"type":"cmd","cmd":{"buzz":{"ms":60,"d":500}}}` | Pola buzzer kustom |
| `{"type":"cmd","cmd":{"nvs_reset":true}}` | Reset konfigurasi NVS |
| `{"type":"cmd","cmd":{"factory_reset":true}}` | Factory reset lengkap (hanya standby) |

### Konfigurasi NVS

| Command | Tipe & Rentang |
|---------|----------------|
| `smps_bypass` | `true|false` |
| `smps_cut` | `float` 30.0–70.0 V (harus < `smps_rec`) |
| `smps_rec` | `float` 30.0–80.0 V (harus > `smps_cut`) |
| `bt_autooff` | `uint32` milidetik, 0–3.600.000 |
| `fan_mode` | `"auto"|"custom"|"failsafe"` |
| `fan_duty` | `int` 0–1023 (aktif bila mode custom) |

### RTC Sync

- `{"type":"cmd","cmd":{"rtc_set":"YYYY-MM-DDTHH:MM:SS"}}`
- `{"type":"cmd","cmd":{"rtc_set_epoch":1698653727}}`

Jika diterapkan, amplifier mengirim:

```json
{"type":"log","lvl":"info","msg":"rtc_synced","offset_sec":12}
```

Jika ditolak karena offset kecil atau rate-limit 24 jam:

```json
{"type":"log","lvl":"warn","msg":"rtc_sync_skipped","reason":"offset_small|ratelimited"}
```

### OTA Streaming

1. **Begin**
   ```json
   {"type":"cmd","cmd":{"ota_begin":{"size":123456,"crc32":"ABCD1234"}}}
   ```
   Respon: `{"type":"ota","evt":"begin_ok"}` atau `{"type":"ota","evt":"begin_err","err":"..."}`

2. **Write**
   ```json
   {"type":"cmd","cmd":{"ota_write":{"seq":1,"data_b64":"AAEC..."}}}
   ```
   Respon: `{"type":"ota","evt":"write_ok","seq":1}` atau `{"type":"ota","evt":"write_err","seq":1,"err":"..."}`

3. **End**
   ```json
   {"type":"cmd","cmd":{"ota_end":{"reboot":true}}}
   ```
   Respon: `{"type":"ota","evt":"end_ok","rebooting":true}` atau `{"type":"ota","evt":"end_err","err":"..."}`

4. **Abort**
   ```json
   {"type":"cmd","cmd":{"ota_abort":true}}
   ```
   Respon: `{"type":"ota","evt":"abort_ok"}`

Semua error OTA juga disiarkan sebagai `{"type":"ota","evt":"error","err":"..."}`. Ketika OTA aktif, auto-power dari PC detect diabaikan.

---

## Catatan OTA

- Alur standar: `ota_begin` → beberapa `ota_write` berurutan → `ota_end` (pilih `reboot:true` untuk restart otomatis atau `false` untuk menunggu perintah manual).
- Selama OTA berjalan, guard internal memaksa `ota_ready=false` pada telemetri dan menonaktifkan auto-power PC detect.
- Jika `ota_end` diminta dengan `reboot:true`, status guard tetap aktif sampai restart selesai agar panel tidak memicu ulang secara prematur.
- `ota_abort` kapan saja mengembalikan amplifier ke mode normal dan mengatur `ota_ready=true`.

---

## Kebijakan RTC Sync

- Sinkronisasi hanya dijalankan jika |offset| > 2 detik dibanding RTC lokal.
- Setelah berhasil, timestamp sync disimpan di NVS (`stateSetLastRtcSync`).
- Permintaan berikutnya baru diproses setelah 24 jam sejak sync terakhir.
- OSF (oscillator stop flag) DS3231 dibersihkan otomatis setelah RTC disetel.
