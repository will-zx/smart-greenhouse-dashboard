#ifndef CONFIG_H
#define CONFIG_H

#include "secrets.h"

// WiFi timeout (ms)
#define WIFI_TIMEOUT    15000

// Firebase Realtime Database (Spark gratis, tanpa kartu kredit).
#define FIREBASE_DB_URL       "https://smart-greenhouse-e214c-default-rtdb.asia-southeast1.firebasedatabase.app"
#define DEVICE_ID             "esp32-greenhouse"
#define DB_PUSH_MS            20000
#define DB_POLL_MS            5000

// Sensor pins
// DHT22 data line needs 4.7k-10k pull-up to VCC and 100nF decoupling
// across VCC-GND if not present on the module (see wiring C3).
#define DHT_PIN         4
#define SOIL_PIN        34

// OLED (I2C)
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// Relays (3 channels, active-LOW: LOW = ON, HIGH = OFF)
#define RELAY_MIST      26
#define RELAY_IRRIG     27
#define RELAY_BLOWER    25

// Timing (DHT22 needs >2s between reads; loop delay(2000) covers it)
#define BOT_CHECK_INTERVAL  5000
#define OLED_INTERVAL       1000

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
