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

// Sensor: SHT31 (I2C, akurasi ±0.2°C / ±2% RH) — share bus dengan OLED.
// Alamat default 0x44 (OLED di 0x3C). Modul SHT31 umumnya sudah ada pull-up I2C.
#define SHT31_I2C_ADDR  0x44
#define SOIL_PIN        34

// OLED + SHT31 (I2C bus yang sama: SDA 21, SCL 22)
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Aktuator via modul MOSFET 15A/400W (umumnya AKTIF-HIGH: HIGH = ON).
// Kalau modul Anda aktif-LOW, set MOSFET_INVERTED 1 (di setActuators).
// Blower: 2 kipas 12x12cm DIPARALEL pada satu output PWM (LEDC).
#define MOSFET_MIST     26
#define MOSFET_IRRIG    27
#define BLOWER_PIN      25
#define BLOWER_FREQ     25000
#define BLOWER_RES      8
#define MOSFET_INVERTED 0

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
// agar push DB & OLED tetap stabil).
#define OLED_INTERVAL   1000

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

#endif