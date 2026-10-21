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

// Pompa irigasi. Prototipe sekarang memakai SATU pompa: DIST (12V) menarik
// air langsung dari penampungan utama ke zona tanam.
// PUMP_FILL / PUMP_FERT_A / PUMP_FERT_B TETAP ADA di sini dan tetap dipaksa
// LOW di initActuators(): firmware TIDAK PERNAH memanggilnya (siklus FILL/FERT
// dibuang, fertigasi ditunda). Jangan hapus definisi pinnya.
#define PUMP_FILL          13   // 12V: penampungan utama -> ember campuran (TIDAK dipakai)
#define PUMP_FERT_A        14   // 5V : pupuk A -> ember (TIDAK dipakai)
#define PUMP_FERT_B        15   // 5V : pupuk B -> ember (TIDAK dipakai)
#define PUMP_DIST          32   // 12V: penampungan utama -> tanaman (dipakai)

// ==== Kalibrasi dosis irigasi (TERUKUR 21 Okt 2026) ====
// Debit pompa: gelas ukur + stopwatch, 3x, rata-rata.
//   pompa DIST (dan FILL) = 31,7 mL/s ; pompa FERT = 22,2 mL/s (tidak dipakai
//   lagi selama fertigasi ditunda, dicatat untuk mengaktifkan kembali).
// Cara ukur ulang: letakkan gelas ukur di outlet pompa, nyalakan pompa,
// catat volume setelah 30 s, ulangi 3x, ambil rata-rata -> mL/detik.
#define PUMP_DIST_ML_PER_S   31.7f
// Luas wetted zona: 10 polybag 30x30 cm, 2 baris -> 900 cm2 per tanaman
// (asumsi: permukaan atas polibag = area wetted), total 9000 cm2.
// Cara ukur ulang: hitung luas dasar tiap polybag (pi * d^2 / 4 dengan d =
// diameter polybag) x jumlah polybag.
#define IRRIG_AREA_CM2       9000.0f
// Jumlah siklus irigasi per hari. Dosis per siklus = anggaran harian / nilai
// ini, jeda minimum antar siklus = 24 jam / nilai ini.
// Cara tetapkan: dari kebutuhan harian (ETc x area) dan debit pompa -> bagi
// jadi beberapa dosis kecil supaya tidak ada siram besar sekaligus.
#define IRRIG_CYCLES_PER_DAY 2
// Gate irigasi otomatis: 1 = keputusan fuzzy boleh menyalakan pompa.
// 0 = irigasi otomatis dimatikan (siklus manual tetap bisa); dipakai sebelum
// debit & luas media tersedia. Badge status " IRIGOFF" muncul saat 0.
#define IRRIG_AUTO_ENABLED   1
// Catatan: asumsi efisiensi irigasi 1,0 (volume keluar pompa = volume sampai
// media), jadi TIDAK ada konstanta efisiensi terpisah.

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

// ==== Blaney-Criddle: kebutuhan air ====
// ETo = p * (0,46 * Tmean + 8) ; ETc = Kc * ETo   (mm/hari)
//
// Faktor p = fraksi jam siang tahunan (dimensionless).
// Rumus FAO-24: p = (N/24) * (1 + 0,033*cos(2*pi*J/365)), N = jam siang efektif.
// Indonesia (0-7 LS) dengan N = 12 jam -> p ~ 0,48-0,50.
// TODO: tabel p resmi FAO-24 BELUM terverifikasi (dokumen FAO-24 tidak tersedia
// daring; FAO-56 yang daring hanya memuat Penman-Monteith & pan evaporation).
// Angka ini berasal dari RUMUS FAO-24, bukan dari tabel resmi -> sebelum
// dipakai di laporan harus dicek ke sumber primer.
// Catatan: kode lama memakai p = PEAK_HOURS/12 = 0,333 (PEAK_HOURS = jam puncak
// matahari, BUKAN jam siang) -> ETo kurang ~30 % dari rumus FAO-24.
#define BC_P_FACTOR      0.48f
// Konstanta rumus Blaney-Criddle: ETo = p * (BC_T_COEF * Tmean + BC_T_CONST).
#define BC_T_COEF        0.46f
#define BC_T_CONST       8.0f
// Tmean = rata-rata suhu sliding 3 jam (ember 1 menit). FAO-56 menetapkan
// Penman-Monteith sebagai metode standar; Blaney-Criddle (FAO-24) dikalibrasi
// untuk periode panjang, jadi hasil di sini PENDEKATAN PRAKTIS (bukan ETo
// standar) dan harus diberi label begitu di log maupun telemetri.
#define BC_TMEAN_BUCKET_MS   60000UL   // 1 menit per ember
#define BC_TMEAN_BUCKETS     180      // 180 ember x 1 menit = 3 jam sliding
// Batas atas ETo (mm/hari) supaya nilai ekstrem tidak dipakai.
#define MAX_ET0        12.0

// Kc per fase pertumbuhan. Sumber: Tabel 2.1 proposal ("Nilai koefisien
// tanaman (Kc) pada tanaman cabai merah", data Supriadi et al. [10]):
// Awal 0,40 | Vegetatif 0,75 | Pembungaan 1,10 | Pemuliaan buah 1,00 | Panen 0,90
// Default = fase "Vegetatif" -> perilaku lama (Kc 0,75) tetap berlaku.
#define KC_AWAL           0.40f
#define KC_VEGETATIF      0.75f
#define KC_PEMBUNGAAN     1.10f
#define KC_PEMULIAAN_BUAH 1.00f
#define KC_PANEN          0.90f
#define KC_DEFAULT        KC_VEGETATIF

// Soil Moisture Calibration (modul KAPASITIF, inverted ADC: kering=tinggi, basah=rendah).
// SOIL_DRY  = ADC saat probe di udara kering (teramati ~2644 di serial).
// SOIL_WET  = ADC saat probe terendam penuh air (asumsi v1.2; ganti dengan ukur asli).
// ponytail: nilai sementara dari pembacaan serial; kalibrasi ulang setelah ukur titik basah.
#define SOIL_DRY   2644
#define SOIL_WET   1200
// Toleransi raw ADC di luar titik kalibrasi. Probe dicabut -> ADC mengambang
// (0 atau 4095) -> di luar rentang ini -> ditandai tidak valid (bukan 100 %
// basah / 0 % kering palsu). Angka ini hanya "jaring pengaman", bukan kalibrasi.
#define SOIL_RAW_TOL 300

// TDS Meter v1.0 (analog, klon DFRobot SEN0244) di ember campuran irigasi.
// Output ~0..2.3V. GPIO35 = ADC input-only tanpa pull-up. Kompensasi suhu
// memakai suhu SHT31. Kalibrasi: TDS_CAL_K (set dengan larutan standar).
// CATATAN: TDS sekarang hanya TELEMETRI (dashboard). Dosis pupuk memakai
// pompa FERT yang ditunda, jadi TDS_TARGET_PPM belum dipakai menghitung
// dosis apa pun; dipakai lagi saat fertigasi diaktifkan kembali.
#define TDS_PIN         35
#define TDS_ADC_MAX     4095.0
#define TDS_REF_VOLT    3.3
#define TDS_TARGET_PPM  1000.0  // konsentrasi larutan akhir (air penampungan ~400 ppm)
#define TDS_CAL_K       1.0
// Rentang raw ADC yang dianggap sah (probe terendam, bukan mengambang / dicabut)
// dan rentang ppm. Di luar itu TDS ditandai tidak valid -> dosis pupuk tidak
// dihitung dari angka garbage.
#define TDS_RAW_MIN     200
#define TDS_RAW_MAX     3000
#define TDS_PPM_MIN     50.0f
#define TDS_PPM_MAX     5000.0f

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

// Kontrol VPD berbasis setpoint fase (di-set admin dari dashboard). Setpoint
// ini KOREKSI terhadap keluaran fuzzy, bukan pengganti: fuzzy tetap menentukan
// apa yang dijalankan, setpoint hanya mendorong satu arah.
// VPD > target+BAND -> tambah/pertahankan misting (blower tetap sesuai fuzzy).
// VPD < target-BAND -> dorong blower proporsional (misting tetap sesuai fuzzy).
// Di dalam band -> fuzzy menentukan sepenuhnya.
#define VPD_BAND_KPA    0.2f
// Duty maksimum yang boleh ditambahkan setpoint ke blower. Setpoint tidak
// pernah menaikkan blower ke penuh mendadak; duty penuh hanya bila fuzzy
// juga memintanya.
#define VPD_BOOST_MAX   0.6f
// Rentang VPD target yang diterima dari dashboard (kPa). Di luar ini perintah
// ditolak di sisi device (VPD wajar hortikultura: 0,3-2,5 kPa).
#define VPD_TARGET_MIN  0.3f
#define VPD_TARGET_MAX  2.5f


// Setelan fase default (dipakai bila NVS masih kosong). Admin dapat mengubah
// nama fase, VPD target, dan durasi dari dashboard; tersimpan di NVS.
#define PHASE_DEFAULT_VPD       1.0f       // kPa
#define PHASE_DEFAULT_NAME      "Vegetatif"
#define PHASE_DEFAULT_DURATION  0u         // detik; 0 = tanpa batas
#define NVS_NAMESPACE           "smartgh"

// Batas durasi override manual (ms). Setelah lewat, aktuator yang di-override
// otomatis kembali ke kendali fuzzy — misting tidak boleh menyala semalaman.
#define MANUAL_MAX_MS           600000UL   // 10 menit

// Timeout Task WDT (detik) + inisialisasi eksplisit (lihat RumahHijau.ino).
#define WDT_TIMEOUT_S           5

// Cooldown perintah restart (ms) supaya reboot berulang tidak memutus
// siklus irigasi.
#define RESTART_COOLDOWN_MS     60000UL

// Durasi fase maksimum (detik) = 365 hari, biar tidak overflow uint32.
#define PHASE_MAX_DURATION_SEC  31536000UL

#endif