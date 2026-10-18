#ifndef CONFIG_H
#define CONFIG_H

#include "secrets.h"

// WiFi timeout (ms) + jeda percobaan reconnect berikutnya
#define WIFI_TIMEOUT    15000
#define WIFI_RETRY_MS   10000

// Firebase Realtime Database (Spark gratis, tanpa kartu kredit).
// DEVICE_ID & kredensial akun perangkat ada di secrets.h (satu per unit).
#define FIREBASE_DB_URL       "https://smart-greenhouse-e214c-default-rtdb.asia-southeast1.firebasedatabase.app"
#define LOGS_PATH             "/zones/" DEVICE_ID "/sensor_logs"
#define DB_PUSH_MS            20000
#define DB_POLL_MS            5000

// ID token Firebase berlaku 1 jam; re-login lebih awal agar tidak expired
// di tengah push. 55 menit = 5 menit margin.
#define TOKEN_REFRESH_MS      3300000

// Sensor: SHT31 (I2C, akurasi ±0.2°C / ±2% RH) — share bus dengan LCD.
// Alamat default 0x44 (LCD backpack PCF8574 di 0x27). Modul SHT31 umumnya sudah ada pull-up I2C.
#define SHT31_I2C_ADDR  0x44
#define SOIL_PIN        34

// I2C bus bersama: SHT31 + LCD 20x4 (backpack PCF8574 0x27; 0x3F = cadangan).
#define I2C_SDA         21
#define I2C_SCL         22
#define LCD_ADDR        0x27
#define LCD_COLS        20
#define LCD_ROWS        4

// Aktuator via modul XY-MOS 400W (umumnya AKTIF-HIGH: HIGH = ON).
// Kalau modul Anda aktif-LOW, set MOSFET_INVERTED 1 (di setActuators).
// Blower: 2 kipas 12x12cm DIPARALEL pada satu output PWM (LEDC).
#define MOSFET_MIST     26
#define BLOWER_PIN      25
#define BLOWER_FREQ     1000   // XY-MOS: spesifikasi PWM 0-20 kHz (25 kHz mati <100%); 1 kHz teruji
#define BLOWER_RES      8
// Duty minimum blower yang benar-benar memutar kipas (hw_test Okt 2026: start
// ~20 %, stop ~13 %). Level fuzzy di bawah ini dibulatkan naik ke nilai ini
// supaya perintah 0 < level < min tidak memerintahkan kipas yang diam.
#define BLOWER_MIN_DUTY 0.20f
#define MOSFET_INVERTED 0

// Irigasi = 4 pompa dalam satu siklus (state machine di irrigation.cpp):
// FILL -> FERT -> DIST. Semua via modul XY-MOS/MOSFET (hormati MOSFET_INVERTED).
#define PUMP_FILL          13   // 12V: penampungan utama -> ember campuran (5-10 L)
#define PUMP_FERT_A        14   // 5V : pupuk A -> ember (fertigasi)
#define PUMP_FERT_B        15   // 5V : pupuk B -> ember (fertigasi)
#define PUMP_DIST          32   // 12V: ember campuran -> tanaman
#define IRRIG_FILL_MS      30000     // durasi isi ember (ukur debit pompa asli)
#define IRRIG_DIST_MS      120000    // durasi alirkan ke tanaman (timer; tanpa sensor level)
#define IRRIG_FERT_TIMEOUT_MS 120000 // safety: dosing berhenti walau TDS < target
#define IRRIG_FERT_PULSE_MS 2000     // pompa fert nyala 2s (mengaduk) -> jeda 1s sampling
#define IRRIG_FERT_IDLE_MS  1000
#define IRRIG_TDS_HYSTERESIS 20.0    // toleransi di sekitar TDS_TARGET_PPM

// MicroSD (SPI, pin default VSPI ESP32: SCK 18, MISO 19, MOSI 23, CS 5).
// Backup lokal: seluruh log + antrian saat push Firebase gagal.
#define SD_CS           5
#define SD_BACKLOG_MAX  100
#define SD_BACKLOG_FLUSH_PER_CALL 20
// Tulis ulang seluruh file backlog setiap flush wears kartu SD.
// Push tiap 20s, jadi flush cukup 1x per 5 menit.
#define SD_FLUSH_MS     300000
#define SD_LOG_FLUSH_EVERY  30

// Timing (SHT31 jauh lebih cepat dari DHT22; delay(2000) di loop dipertahankan
// agar push DB & LCD tetap stabil).
#define LCD_INTERVAL    1000

// PSU cadangan: hardware-only — ESP32 + aktuator bisa ditenagai powerbank/PSU
// kedua saat listrik mati. Tidak ada kode khusus; dokumentasi di Laporan.

// Blaney-Criddle: ETc = Kc * p * (0.46*T + 8), p ≈ PEAK_HOURS/12 (ekuator ~0.27).
// Kc cabai: 0.4 (awal) / 0.75 (vegetatif) / 1.1 (bunga-buah). Referensi:
// "Penetapan Kebutuhan Air Tanaman Cabai Merah"; "Modified Blaney-Criddle...".
#define KC_CHILI       0.75
#define PEAK_HOURS     4.0
#define MAX_ET0        12.0

// Soil Moisture Calibration (modul KAPASITIF, inverted ADC: kering=tinggi, basah=rendah).
// SOIL_DRY  = ADC saat probe di udara kering (teramati ~2644 di serial).
// SOIL_WET  = ADC saat probe terendam penuh air (asumsi v1.2; ganti dengan ukur asli).
// ponytail: nilai sementara dari pembacaan serial; kalibrasi ulang setelah ukur titik basah.
#define SOIL_DRY   2644
#define SOIL_WET   1200

// TDS Meter v1.0 (analog, klon DFRobot SEN0244) di ember campuran irigasi.
// Output ~0..2.3V. GPIO35 = ADC input-only tanpa pull-up. Kompensasi suhu
// memakai suhu SHT31. Kalibrasi: TDS_CAL_K (set dengan larutan standar).
#define TDS_PIN         35
#define TDS_ADC_MAX     4095.0
#define TDS_REF_VOLT    3.3
#define TDS_TARGET_PPM  1000.0  // konsentrasi larutan akhir (air penampungan ~400 ppm)
#define TDS_CAL_K       1.0

// Filter EMA (Exponential Moving Average): y[n] = alpha*x[n] + (1-alpha)*y[n-1].
// alpha kecil = lebih halus tapi lebih lambat. Dipakai untuk suhu & kelembaban
// (input VPD) serta soil, agar VPD dan keputusan irigasi tidak bergetar.
// Alfa di sini PER-SAMPEL, bukan per-detik: konstanta waktunya t = dt/alfa.
// Loop controlTask 2000 ms dengan alfa 0.2 -> t = 10 s (63% perubahan dalam
// 10 s). Kalau periode loop diubah, alfa harus diskalakan ulang: alfa = dt/(t+dt).
#define EMA_ALPHA_TEMP  0.2f
#define EMA_ALPHA_HUM   0.2f
#define EMA_ALPHA_SOIL  0.2f
// TDS paling bising (pompa fert berdenyut 2 s on / 1 s off, jadi dua iterasi
// berurutan bisa menangkap kondisi adukan berbeda). Difilter lebih cepat dari
// suhu/kelembapan supaya gate target TDS tidak bereaksi pada satu sampel.
#define EMA_ALPHA_TDS   0.3f

// Kontrol VPD berbasis setpoint fase (di-set admin dari dashboard). Jika VPD
// target aktif: VPD > target+BAND -> MISTING; VPD < target-BAND -> BLOWER;
// dalam band -> OFF. Manual override tetap menang.
#define VPD_BAND_KPA    0.2f

// Setelan fase default (dipakai bila NVS masih kosong). Admin dapat mengubah
// nama fase, VPD target, dan durasi dari dashboard; tersimpan di NVS.
#define PHASE_DEFAULT_VPD       1.0f       // kPa
#define PHASE_DEFAULT_NAME      "Vegetatif"
#define PHASE_DEFAULT_DURATION  0u         // detik; 0 = tanpa batas
#define NVS_NAMESPACE           "smartgh"

#endif