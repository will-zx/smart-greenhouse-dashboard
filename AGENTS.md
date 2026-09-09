# AGENTS.md

Smart watering/greenhouse control firmware + dashboard for an ESP32 (PlatformIO + Arduino framework), targeting **chili cultivation** (Kc cabai, VPD sayuran terlindung).

Structure:
- `firmware/` — PlatformIO project (ESP32). `firmware/src/RumahHijau.ino` is the entrypoint/`setup`+`loop`; all `.cpp`/`.h` files are in `firmware/src/` beside it.
- `web/` — static Firebase dashboard (`web/index.html`), reads the same RTDB the firmware pushes to.
- `Asset/, Dokumen_Pendukung/, Laporan/, Naskah_Publikasi/, Panduan/, Referensi/` — capstone documents (not part of the git repo).

## Build & run (firmware)
- PlatformIO project in `firmware/` (board `esp32dev`, monitor 115200). `firmware/platformio.ini` is committed. Required libs: `DHTesp`, `ArduinoJson`, `Adafruit GFX`, `Adafruit SSD1306`, plus core WiFi/HTTPClient.
- Common (run from `firmware/`): `pio run` (build), `pio run -t upload` (flash), `pio device monitor -b 115200` (serial; `Serial.begin(115200)`).
- No host-side tests or lint exist; all logic is on-device.
- Serial port is `/dev/ttyUSB0` (group `uucp`); if the current shell can't open it, wrap commands in `newgrp uucp <<'EOF' ... EOF`.

## Hardware gotchas (all documented in `firmware/src/config.h` — trust it)
- Relays are **active-LOW**: `LOW` = ON, `HIGH` = OFF (`firmware/src/actuator.cpp`). Never invert these.
- DHT22 needs **>2s between reads** — the loop `delay(2000)` covers it; don't shorten it.
- Soil moisture is a **capacitive** sensor, inverted ADC: kering=ADC tinggi, basah=ADC rendah. `SOIL_DRY`/`SOIL_WET` are provisional (`firmware/src/sensor.cpp` maps raw → 0..100%); recalibrate from real dry-air / submerged readings if the values drift.
- ESP32 ADC pins (GPIO34) are input-only, no pull-up.

## Secrets (critical)
- `firmware/src/config.h` contains **real WiFi credentials, and a real Telegram `BOT_TOKEN`**. It is committed to the repo. Do NOT add new secrets, do NOT print/expose them further, and avoid copying them into logs or other files. Do NOT make this repo public unless secrets are moved to env.
- Telegram bot is disabled until `CHAT_ID` is set to something other than `"YOUR_CHAT_ID"`. Assume it's disabled.
- WiFi failures are non-fatal: the system `continue(s) offline` (Telegram/DB only init when connected). NTP (`configTime`, WIB) is set only after WiFi connects; before sync the DB push is skipped so no 1970 timestamps land.

## Wiring / flow
- `loop` ordering matters: read sensors → compute VPD, irrigation (Blaney-Criddle), fuzzy decision → `controlApply` (manual override via `controlSetMist/Irig/Blower`, reset with `controlRelease`) → `setActuators`. If you add logic, follow this pipeline.
- Irigation has a **single source of truth**: `irrigation.shouldIrrigate` (soil<40 || ETc>6). Fuzzy no longer outputs `irigasiOn`.
- Fuzzy uses **VPD** (chili): `vpdHigh` (>~1.5 kPa, stres) boosts misting; `vpdLow` (jenuh) suppresses mist to prevent fungal disease and enables blower dehumidify. Rule tables in `firmware/src/fuzzy.cpp` — thresholds are tuning knobs.
- Sensor `valid=false` means DHT22/soil unplugged; handled with a throttled error print, all control continues.
- Dashboard (`web/index.html`) reads RTDB; `ts` in `sensor_logs` is NTP epoch seconds.
- All code comments are Indonesian; keep new comments consistent with that (but defaults are no-comment unless asked).