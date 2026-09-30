# AGENTS.md

Smart watering/greenhouse control firmware + dashboard for an ESP32 (PlatformIO + Arduino framework), targeting **chili cultivation** (Kc cabai, VPD sayuran terlindung).

## Pilih mode sesi — baca dulu, tiap sesi baru pilih satu
User menentukan mode di prompt pertama (kalau tidak jelas, tanya dulu):
- **PROPOSAL** → dokumen proposal per BAB (`Dokumen_Pendukung/Proposal/`), lihat "Mode PROPOSAL".
- **LAPORAN** → laporan capstone C1/C2/C3 (`Laporan/`), lihat "Mode LAPORAN".
- **DEVELOPMENT** → firmware ESP32 + web dashboard, lihat "Mode DEVELOPMENT" + bagian bawah file ini.

## Mode PROPOSAL
- Lokasi: `Dokumen_Pendukung/Proposal/` (gitignored, bukan bagian repo). Versi lama dipindah ke `_arsip/` di folder yang sama.
- **Penamaan per BAB**: `[Bab_x-rev_x]2305110036_yyyymmdd.docx` = revisi belum ACC; `[Bab_x-fix]2305110036_yyyymmdd.docx` = sudah ACC. NIM `2305110036`, tanggal `yyyymmdd`.
- Status: BAB 1 sudah ACC (`[Bab_1-fix]2305110036_20260930.docx`). BAB baru/revisi BAB lain mengikuti format `rev_x` sampai ACC, lalu di-rename jadi `fix`.
- BAB baru dimulai dari template `Dokumen_Pendukung/[template baru]Dokumen_C1_Proposal_Template_Rev8 (1).docx` (struktur: 1.1 Latar Belakang, 1.2 Rumusan Masalah, dst. per BAB).
- **Sitasi = field Zotero, bukan teks statis.** Di BAB 1 tiap `[n]` adalah field Word `ADDIN ZOTERO_ITEM CSL_CITATION {json}`; DAFTAR PUSTAKA dibungkus field `ADDIN ZOTERO_BIBL CSL_BIBLIOGRAPHY {...}`:
  - Renumber mengikuti **urutan kemunculan pertama** di teks; DAFTAR PUSTAKA mengikuti urutan yang sama.
  - Field harus render statis `[n]` di OnlyOffice (editor user) & Word (dosen) **tanpa plugin**, dan tetap refreshable via plugin Zotero di Word.
  - Sitasi rusak/kosong → ambil itemData segar dari API lokal Zotero: `http://localhost:23119/api/groups/6575633/items/{KEY}?format=csljson` (group `6575633` "Smart Watering System"), lalu bangun ulang field. Referensi implementasi: `/tmp/opencode/fix/` (`transform.py`, `odt_fields.json`, `api_items.json`).
- Verifikasi setelah edit: konversi ke PDF via `libreoffice --headless --convert-to pdf --outdir ... file.docx`, cek urutan sitasi & DAFTAR PUSTAKA, pastikan jumlah `ADDIN ZOTERO_ITEM` / `ADDIN ZOTERO_BIBL` konsisten.

## Mode LAPORAN
- Lokasi: `Laporan/C{1,2,3}/` — masing-masing berisi `Draft/` (file kerja + template) dan `Final/` (yang dikumpulkan).
- Nama Final mengikuti konvensi: `2026_Tekom_C{n}_2305110036 - Wildan Abu Bakar S.pdf`.
- Format/aturan dokumen mengacu `Panduan/PANDUAN TUGAS AKHIR DIPLOMA DAN SARJANA UNNES 2025 PUBLISH.pdf` (versi 2024 juga ada di `Dokumen_Pendukung/`).
- Status: C1, C2, C3 sudah ada `Final/`. Kerja di mode ini = perbaikan/revisi, atau lanjut C berikutnya; template per C ada di `Draft/` masing-masing.

## Mode DEVELOPMENT
Ringkasan dua target (detail lengkap di bagian bawah AGENTS.md):
- `firmware/` — ESP32 + PlatformIO: aktuator/irigasi/mikroklimat, keputusan `fuzzy.cpp`, push RTDB. Build check `pio run` wajib SUCCESS.
- `web/` — dashboard Firebase statis, deploy Cloudflare Pages auto.
Prompt yang menyebut sensor, aktuator, fuzzy, RTDB, irrigation, dashboard → DEVELOPMENT.

Structure:
- `firmware/` — PlatformIO project (ESP32). `firmware/src/RumahHijau.ino` is the entrypoint/`setup`+`loop`; all `.cpp`/`.h` files are in `firmware/src/` beside it.
- `web/` — static Firebase dashboard: `index.html` (struktur), `style.css` (light theme + responsive), `app.js` (auth, data, kontrol), `config.js`/`config.js.example` (firebaseConfig & DB_URL), `TUTORIAL.md` (mode test + jalur komersial).
- `Asset/, Dokumen_Pendukung/, Laporan/, Naskah_Publikasi/, Panduan/, Referensi/` — capstone documents (not part of the git repo).

## Tim / jobdesk (konteks capstone)
- **Eksa** — mikroklimat/VPD: blower & misting.
- **Alvin** — irigasi, perhitungan Blaney-Criddle (ETc).
- **Wildan** — dashboard monitoring & kendali: web (`web/`).
- **Fuzzy bukan milik jobdesk siapapun** — mesin keputusan bersama: input dari Eksa (mikroklimat/VPD) & Alvin (irigasi/ETc) diproses `fuzzy.cpp`, output-nya dipakai seluruh sistem.

## Build & run (firmware)
- PlatformIO project in `firmware/` (board `esp32dev`, monitor 115200). `firmware/platformio.ini` is committed. Required libs: `Adafruit SHT31`, `ArduinoJson`, `Adafruit GFX`, `Adafruit SSD1306`, plus core WiFi/HTTPClient.
- Common (run from `firmware/`): `pio run` (build), `pio run -t upload` (flash), `pio device monitor -b 115200` (serial; `Serial.begin(115200)`).
- `pio` tidak di PATH sistem; install-nya venv di `/tmp/opencode/pio-venv` — export PATH dulu: `export PATH="/tmp/opencode/pio-venv/bin:$PATH"`. (Venv bisa hilang, buat ulang dengan `python3 -m venv` + `pip install platformio`.)
- No host-side tests or lint exist for firmware; all logic is on-device. Build check: `pio run` harus SUCCESS.
- Serial port is `/dev/ttyUSB0` (group `uucp`); if the current shell can't open it, wrap commands in `newgrp uucp <<'EOF' ... EOF`.
- Perubahan firmware (mis. nambah field RTDB) mengharuskan **re-flash ESP32** (`pio run -t upload`) agar live.

## Web dashboard: build, test, deploy
- Static site — tidak ada build step. Serve lokal: run `python3 -m http.server 8000` from `web/`, atau salinan demo di `/tmp/opencode/webtest` di port lain.
- Syntax check: `node --check web/app.js web/config.js`.
- `DEMO_MODE` di `web/app.js` (`true`) = data dummy tanpa Firebase/login, untuk presentasi & uji visual. Jangan di-commit `true`.
- Cache-buster script di `web/index.html`: `app.js?v=N` — naikkan setiap ada perubahan `app.js` supaya Cloudflare CDN tidak serve versi lama.
- Deploy: git push ke `master` → Cloudflare Pages **auto-deploy** → https://smart-greenhouse-dashboard.wildanzstd.workers.dev/ . Tunggu build selesai lalu verifikasi `?v=` terbaru sudah ter-serve (`curl -s URL/ | grep -o 'app.js?v=[0-9]*'`).
- Login pakai **Firebase Auth Email/Password** (accounts di-manage di Firebase console). Persistence default `local` → auto-login seterusnya di browser itu; itu perilaku yang diinginkan, bukan bug.
- Rendering headless untuk verifikasi visual: `chromium --headless --no-sandbox --disable-gpu --virtual-time-budget=5000 --dump-dom URL` atau `--screenshot=file.png --window-size=W,H`. `--dump-dom` tidak menampilkan properti `checked` (gunakan script evaluasi bila perlu).

## Secrets (critical)
- `firmware/src/secrets.h` berisi **kredensial asli** (WiFi + akun perangkat Firebase: `FIREBASE_API_KEY`, `DEVICE_ID`, `DEVICE_EMAIL`, `DEVICE_PASSWORD`) dan **di-gitignored** (hanya `.example` yang di-commit). `firmware/src/config.h` `#include "secrets.h"`. Do NOT print/expose, do NOT commit `secrets.h`. Password devices NOT shared and NOT reused elsewhere (they end up in `firmware.bin`).
- `web/config.js` berisi firebaseConfig + DB_URL **asli dan DI-COMMIT** (sengaja: repo private + apiKey client-side bukan secret keras). Jangan jadikan repo ini public. `config.js.example` sebagai template.
- Firebase RTDB rules = **locked + per-role** (published & verified 9/9, see `web/TUTORIAL.md` §2b). Schema: `zones/{DEVICE_ID}/sensor_logs` (ESP32 writes, any logged-in user reads), `commands/{DEVICE_ID}` (admin writes, ESP32 reads+deletes), `labels/{DEVICE_ID}` (admin writes, any logged-in user reads). Admin = `mr.pman@mail.unnes.ac.id` (hardcoded in rules + `web/config.js` `ADMIN_EMAIL`); viewer = `pluto@jmail.org` (read-only). ESP32 devices = any `*@device.local` account, one per unit.
- RTDB REST accepts Firebase **ID tokens via `?auth=<token>` query ONLY** — `Authorization: Bearer <idToken>` is rejected (`Unauthorized request`); that header is for service-account OAuth2 tokens. Rules lang is RTDB, not Firestore: the variable is `auth`, **not** `request.query.auth`.
- ESP32 authenticates via Identity Toolkit REST `accounts:signInWithPassword` (`ensureToken()` in `firebase_db.cpp`), caches `idToken` ~55 min, re-login on 401. No legacy `FIREBASE_DB_SECRET` used anymore.

## Hardware gotchas (all documented in `firmware/src/config.h` — trust it)
- Aktuator pakai **modul MOSFET 15A/400W** (aktif-HIGH default; knob `MOSFET_INVERTED` di `config.h` kalau modul ternyata aktif-LOW). Blower = **2× 12 cm paralel**, dikendalikan **PWM LEDC 25 kHz** (`BLOWER_PIN`, level 0..1 dari `fuzzy.blowerLevel`). Jangan ubah polaritas tanpa cek `MOSFET_INVERTED`.
- SHT31 (I2C `0x44`) share bus I2C dengan OLED; baca-nya jauh lebih cepat dari DHT22, tapi loop **tetap `delay(2000)`** demi kestabilan push RTDB 20s & refresh OLED — jangan dipersingkat.
- Soil moisture is a **capacitive** sensor, inverted ADC: kering=ADC tinggi, basah=ADC rendah. `SOIL_DRY`/`SOIL_WET` are provisional (`firmware/src/sensor.cpp` maps raw → 0..100%); recalibrate from real dry-air / submerged readings if the values drift.
- MicroSD (`SD_CS=5`) = cadangan lokal: tiap baca di-log ke CSV, antrian disimpan saat push RTDB gagal, lalu di-backfill (cap `SD_BACKLOG_MAX=100`, 20/panggilan). `logFile` dibuka sekali lalu ditahan + flush tiap 30 baris; `sdFlushBacklog()` di-gate `SD_FLUSH_MS` (5 menit) supaya tidak menulis ulang file tiap push (wear kartu). PSU cadangan = hardware-only (lihat catatan di `config.h`).
- **WiFi reconnect**: `loop()` memanggil `connectWiFi()` tiap `WIFI_RETRY_MS` saat `WL_CONNECTED` false, lalu `syncTime()` lagi. Tanpa ini device offline permanen sampai reset. `ponytail:` `connectWiFi()` blocking s/d `WIFI_TIMEOUT` (15s) → loop ikut tertahan saat mencoba; pindahkan ke task FreeRTOS hanya jika aktuator harus responsif saat WiFi mati.
- Perintah `restart`: DELETE entri perintah **lebih dulu**, baru `ESP.restart()` — dan hanya kalau DELETE balas 2xx. Kalau tidak, perintah terbaca lagi setelah boot = reboot loop.
- ESP32 ADC pins (GPIO34) are input-only, no pull-up.

## Wiring / flow
- `loop` ordering matters: read sensors → compute VPD, irrigation (Blaney-Criddle), fuzzy decision → `controlApply` (manual override via `controlSetMist/Irig/Blower`, reset dengan `controlRelease`) → `setActuators`. If you add logic, follow this pipeline.
- Irigation has a **single source of truth**: `irrigation.shouldIrrigate` (soil<40 || ETc>6). Fuzzy no longer outputs `irigasiOn`.
- Fuzzy uses **VPD** (chili): `vpdHigh` (>~1.5 kPa, stres) boosts misting; `vpdLow` (jenuh) suppresses mist to prevent fungal disease and enables blower dehumidify. Rule tables in `firmware/src/fuzzy.cpp` — thresholds are tuning knobs.
- Sensor `valid=false` means SHT31/soil unplugged; handled with a throttled error print, all control continues.
- **Manual/Auto**: kontrol manual (`controlSetX`) menyalakan `active=true`; `controlRelease()` balik ke auto. Getter `controlIsManual()` diekspos ke RTDB sebagai field `manual_mode` di setiap push `sensor_logs` — inilah sumber kebenaran toggle **Auto** di dashboard.
- IoT wiring: ESP32 POST `zones/{DEVICE_ID}/sensor_logs/{push-key}` (fields: `ts` epoch NTP WIB, temperature, humidity, soil, vpd, et0, status_sistem, manual_mode, device) tiap ~20s; dashboard `fetch` `limitToLast=20` **dengan `?auth=<idToken>`**. Kontrol via node `commands/{DEVICE_ID}` (`{action: mist|irigasi|blower|auto|restart, value: bool}`), ESP32 delete entri setelah dieksekusi. Label greenhouse di `labels/{DEVICE_ID}` (nilai string, menang atas `ZONES[].name`).
- **Multi-zone (greenhouse-in-greenhouse)**: satu dashboard, banyak unit ESP32. `DEVICE_ID` per unit di `firmware/src/secrets.h` (WAJIB sama dengan `ZONES[].id` di `web/config.js`, kalau beda = 401). `ZONES` statis di config.js — tambah unit = tambah baris + push, rules tidak perlu diubah. `app.js` fetch per zona terpilih; badge "diam N menit" = log basi >60s (ESP32 hidup tapi tidak push).
- Dashboard UI: `app.js` punya `onSwitch()` untuk toggle aktuator & Auto; toggle mengirim command ke **zona terpilih**, state dicek dari `status_sistem` + `manual_mode`. Auto tidak bisa dimatikan langsung (hanya via tindakan aktuator manual). `applyRole()` menyembunyikan `#ctrlCard` + `#labelBtn` dan memberi badge "Mode lihat" untuk non-admin — RTDB rules yang sesungguhnya menolak, UI hanya kejelasan.
- `status_sistem` = string status fuzzy ("Normal"/"Warning"/"Kritis"/"Monitor") + suffix aktuator hidup: `" MISTING"`, `" BLOWER"`, `" IRIGASI"`. Itu dipakai dashboard untuk posisi switch & pill.

## Telegram bot → dihapus (REV.1, arsip REV.4 lama)
- `telegram_bot.{h,cpp}` sudah dihapus; semua panggilan (init/poll/alert) dibuang dari `RumahHijau.ino`. Alert suhu ekstrem (>38 °C / <10 °C) kini ke serial saja. `secrets.h` lama mungkin masih menyimpan `BOT_TOKEN`/`CHAT_ID` yang tak terpakai — harmless, jangan di-print.
- All code comments are Indonesian; keep new comments consistent with that (but defaults are no-comment unless asked).