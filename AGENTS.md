# AGENTS.md

Smart watering/greenhouse control firmware + dashboard for an ESP32 (PlatformIO + Arduino framework), targeting **chili cultivation** (Kc cabai, VPD sayuran terlindung).

Structure:
- `firmware/` — PlatformIO project (ESP32). `firmware/src/RumahHijau.ino` is the entrypoint/`setup`+`loop`; all `.cpp`/`.h` files are in `firmware/src/` beside it.
- `web/` — static Firebase dashboard: `index.html` (struktur), `style.css` (light theme + responsive), `app.js` (auth, data, kontrol), `config.js`/`config.js.example` (firebaseConfig & DB_URL), `TUTORIAL.md` (mode test + jalur komersial).
- `Asset/, Dokumen_Pendukung/, Laporan/, Naskah_Publikasi/, Panduan/, Referensi/` — capstone documents (not part of the git repo).

## Tim / jobdesk (konteks capstone)
- **Eksa** — mikroklimat/VPD: blower & misting, aturan fuzzy.
- **Alvin** — irigasi, perhitungan Blaney-Criddle (ETc).
- **Wildan** — dashboard monitoring & kendali (bagian `web/`).

## Build & run (firmware)
- PlatformIO project in `firmware/` (board `esp32dev`, monitor 115200). `firmware/platformio.ini` is committed. Required libs: `DHTesp`, `ArduinoJson`, `Adafruit GFX`, `Adafruit SSD1306`, plus core WiFi/HTTPClient.
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
- `firmware/src/secrets.h` berisi **kredensial asli** (WiFi, Telegram `BOT_TOKEN`, `CHAT_ID`, `FIREBASE_DB_SECRET`) dan **di-gitignored** (hanya `.example` yang di-commit). `firmware/src/config.h` `#include "secrets.h"`. Do NOT print/expose, do NOT commit `secrets.h`.
- `web/config.js` berisi firebaseConfig + DB_URL **asli dan DI-COMMIT** (sengaja: repo private + apiKey client-side bukan secret keras). Jangan jadikan repo ini public. `config.js.example` sebagai template.
- Telegram bot disabled until `CHAT_ID` bukan `"YOUR_CHAT_ID"`. Asumsikan disabled.
- Firebase RTDB rules = **test mode** (`.read/.write true`) — keputusan user. Jalur komersial (auth + legacy secret ESP32) didokumentasikan di `web/TUTORIAL.md`. Note: legacy `FIREBASE_DB_SECRET` kemungkinan tidak tersedia di project baru.

## Hardware gotchas (all documented in `firmware/src/config.h` — trust it)
- Relays are **active-LOW**: `LOW` = ON, `HIGH` = OFF (`firmware/src/actuator.cpp`). Never invert these.
- DHT22 needs **>2s between reads** — the loop `delay(2000)` covers it; don't shorten it.
- Soil moisture is a **capacitive** sensor, inverted ADC: kering=ADC tinggi, basah=ADC rendah. `SOIL_DRY`/`SOIL_WET` are provisional (`firmware/src/sensor.cpp` maps raw → 0..100%); recalibrate from real dry-air / submerged readings if the values drift.
- ESP32 ADC pins (GPIO34) are input-only, no pull-up.

## Wiring / flow
- `loop` ordering matters: read sensors → compute VPD, irrigation (Blaney-Criddle), fuzzy decision → `controlApply` (manual override via `controlSetMist/Irig/Blower`, reset dengan `controlRelease`) → `setActuators`. If you add logic, follow this pipeline.
- Irigation has a **single source of truth**: `irrigation.shouldIrrigate` (soil<40 || ETc>6). Fuzzy no longer outputs `irigasiOn`.
- Fuzzy uses **VPD** (chili): `vpdHigh` (>~1.5 kPa, stres) boosts misting; `vpdLow` (jenuh) suppresses mist to prevent fungal disease and enables blower dehumidify. Rule tables in `firmware/src/fuzzy.cpp` — thresholds are tuning knobs.
- Sensor `valid=false` means DHT22/soil unplugged; handled with a throttled error print, all control continues.
- **Manual/Auto**: kontrol manual (`controlSetX`) menyalakan `active=true`; `controlRelease()` balik ke auto. Getter `controlIsManual()` diekspos ke RTDB sebagai field `manual_mode` di setiap push `sensor_logs` — inilah sumber kebenaran toggle **Auto** di dashboard.
- IoT wiring: ESP32 POST `sensor_logs/{push-key}` (fields: `ts` epoch NTP WIB, temperature, humidity, soil, vpd, et0, status_sistem, manual_mode) tiap ~20s; dashboard `fetch` `limitToLast=20`. Kontrol via node `commands/{DEVICE_ID}` (`{action: mist|irigasi|blower|auto|restart, value: bool}`), ESP32 delete entri setelah dieksekusi. `DEVICE_ID=esp32-greenhouse`.
- Dashboard UI: `app.js` punya `onSwitch()` untuk toggle aktuator & Auto; toggle mengirim command, state dicek dari `status_sistem` + `manual_mode`. Auto tidak bisa dimatikan langsung (tidak ada perintah "auto off"); hanya via tindakan aktuator manual.
- `status_sistem` = string status fuzzy ("Normal"/"Warning"/"Kritis"/"Monitor") + suffix aktuator hidup: `" MISTING"`, `" BLOWER"`, `" IRIGASI"`. Itu dipakai dashboard untuk posisi switch & pill.

## Telegram bot (kanal kontrol kedua)
- `firmware/src/telegram_bot.cpp` — kontrol & monitoring via chat Telegram, selain web dashboard. Aktif hanya jika `secrets.h` berisi `BOT_TOKEN`/`CHAT_ID` asli (bukan placeholder); default **disabled** (`initTelegram()` set `botEnabled=false`).
- `pollTelegram()` dipanggil tiap `BOT_CHECK_INTERVAL` di `loop`; command dikirim sebagai pesan chat biasa:
  - `/status` — laporan lengkap (suhu, kelembaban, soil, VPD, status fuzzy, Misting/Irigasi/Blower ON/OFF).
  - `/mist on|off` · `/irigasi on|off` · `/blower on|off` — override manual (parsing sederhana: memuat kata "on").
  - `/auto` — balik ke mode otomatis (`controlRelease()`).
  - `/restart` — `ESP.restart()`.
- `sendTelegramAlert()` dipakai `RumahHijau.ino` saat suhu ekstrem (>38 °C atau <10 °C).
- Berbagi pipeline `controlSetX`/`controlRelease` yang sama dengan web dashboard → override via Telegram juga menyalakan `manual_mode` (toggle Auto di dashboard ikut off).
- All code comments are Indonesian; keep new comments consistent with that (but defaults are no-comment unless asked).