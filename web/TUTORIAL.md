# Tutorial: Firebase RTDB + Auth + Telegram + GitHub Pages

Arsitektur yang dipakai:

```
ESP32 --20s POST--> sensor_logs (auto-push key) <--fetch 10s-- dashboard (web/)
ESP32 <--5s poll-- commands/{device_id} <--POST(bttn)-- dashboard
ESP32 --DELETE--> commands/{device_id}/{key} (selesai dieksekusi)
```

Dashboard `web/` tiga-terpisah (tanpa toolchain):
- `index.html` — struktur + login overlay
- `style.css` — semua styling
- `app.js` — logika (Firebase Auth + fetch/control + Chart.js)
- `config.js` — `firebaseConfig`, `DB_URL`, `DEVICE_ID` (**jangan commit** — ada di `.gitignore`)
- `config.js.example` — template yang di-commit (isi nilaimu, simpan ulang sebagai `config.js`)

## 1. Set up Firebase Realtime Database + Auth (15 menit)

1. Buka https://console.firebase.google.com → **Add project** (nama bebas). Firebase di luar
   "no-cost Spark plan" tidak butuh kartu kredit.
2. Kiri: **Build** → **Realtime Database** → **Create database** → pilih region terdekat
   (mis. `asia-southeast1`).
3. Salin **database URL** (bentuknya `https://<project-id>-default-rtdb.firebaseio.com`):
   - Tempel ke `firmware/src/config.h` sebagai `FIREBASE_DB_URL`.
   - Tempel juga ke `web/config.js` sebagai `DB_URL`.
4. **Aktifkan Authentication** (dashboard kini butuh login):
   - Kiri: **Build** → **Authentication** → **Get started**.
   - **Sign-in method** → aktifkan **Email/Password** → **Save**.
   - Tab **Users** → **Add user** → buat akun (email + password) untuk login dashboard.
5. **Get firebaseConfig** untuk `web/config.js`:
   - Kiri: ikon gerigi **Project settings** → **Your apps** → **</>** (web).
   - Salin blok `firebaseConfig` ke `web/config.js` (contoh di `config.js.example`).
   - Isi juga `DB_URL` dan `DEVICE_ID` di file yang sama.

## 2. Rules Realtime Database

### 2a. Mode demo/test (dipakai capstone sekarang)

Mengizinkan siapa pun membaca & menulis — diperlukan supaya **ESP32 bisa push data** dan
dashboard baca data tanpa auth. Pilih **Start in test mode** saat bikin DB, atau di
**Realtime Database → Rules**, tempel lalu **Publish**:

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

> ⚠️ Ini publik — siapa pun dengan URL bisa baca/tulis. Cocok untuk demo capstone.

### 2b. Jalur komersial (upgrade keamanan saat akan rilis)

Saat siap produksi/komersial, perketat rules dengan **Firebase Auth**. Dashboard saat ini
sudah punya login Email/Password (Firebase SDK) — aktifkan di **Authentication → Sign-in method
→ Email/Password**, buat user, lalu **Authorized domains** tambahkan domain hosting dashboard
(mis. `<project>.pages.dev`).

Browser login SDK akan lolos `request.auth != null`. Untuk **ESP32**, dua opsi:
- Jaga rules mengizinkan keduanya: `request.auth != null || request.query.auth == '<legacy_secret>'`
  (legacy secret hanya ada di project lama; isi `FIREBASE_DB_SECRET` di `firmware/src/secrets.h`).
- Jika project baru tanpa legacy secret, migrasikan ESP32 ke **Firebase Auth REST + service account
  (JWT)** — lanjutkan riset sebelum drop ke produksi.

Contoh rules aman (browser login + legacy secret ESP32):

```json
{
  "rules": {
    ".read": "request.auth != null || request.query.auth == 'LEGACY_DB_SECRET_MU'",
    ".write": "request.auth != null || request.query.auth == 'LEGACY_DB_SECRET_MU'"
  }
}
```

> Tidak ada SQL jadi tidak ada RLS seperti Supabase; keamanan diatur lewat rules GUI.

## 3. Telegram (dapatkan CHAT_ID)

1. Di Telegram, cari bot kamu (`@<namabot>`) → tekan **Start**.
2. Kirim pesan apa saja (misal `halo`).
3. Di PC: buka `https://api.telegram.org/bot<BOT_TOKEN>/getUpdates`. Tokenmu sudah ada di `firmware/src/secrets.h` (`BOT_TOKEN`). Cari `"chat":{"id":123456789`.
4. Paste angka itu ke `CHAT_ID` di `firmware/src/secrets.h`. Re-flash ESP32 → log `Telegram bot enabled`, bot kirim "Online!" ke kamu.

## 4. Deploy dashboard ke GitHub Pages

> Layout baru pakai beberapa file (`index.html`, `style.css`, `app.js`, `config.js`).
> Salin **semua** file dari folder `web/` ke repo hosting kamu.

1. Repo GitHub kamu (mis. `will-zx/smart-watering-system`).
2. Salin seluruh isi folder `web/` ke folder root (atau folder mana pun yang dipakai Pages).
   **Pastikan `web/config.js` ikut tersalin** — tanpanya dashboard tidak bisa init Firebase.
3. Di GitHub: **Settings** → **Pages** → Source **Deploy from a branch** → branch `main`, folder `/ (root)` → **Save**.
4. Tunggu 1-2 menit, buka `https://<username>.github.io/smart-watering-system/` → halaman login muncul → masuk dengan akun dari langkah 1.4.

## 5. Pastikan berjalan

- **Serial ESP32**: muncul `[fb] push log gagal: HTTP xxx` jika ada masalah.
  - `401` → legacy secret salah/aturan `.write` menolak jalur `?auth=`. Cek `FIREBASE_DB_SECRET` & rules.
  - `404` → URL database salah (cek `FIREBASE_DB_URL`).
  - Tidak ada log push sama sekali → ESP32 tidak bisa egress HTTPS (cek router/jaringan), atau rules tidak mengizinkan jalur legacy secret.
- **Dashboard** → halaman **login muncul tapi tidak bisa masuk** → cek provider Email/Password aktif & akun dibuat.
- **Dashboard login OK tapi kosong** → `DB_URL` di `config.js` harus persis database URL console; pastikan ESP32 sudah push minimal satu log (tunggu ~20s) dan field-nya cocok (`temperature`, `humidity`, `soil`, `vpd`, `et0`, `ts`). Cek console browser (`F12`).

## 6. Alur kontrol (dari web ke relay)

1. Klik ON/OFF di dashboard → `POST` JSON ke `commands/{device_id}` (push key otomatis) dengan token SDK.
2. ESP32 `pollCommands()` tiap 5 dtk → ambil 1 perintah terlama (push-ID Firebase terurut waktu)
   → `controlSet*` / `controlRelease` → `DELETE` entri itu.
3. Manual override aktif sampai ada perintah `auto` (semua kembalikan ke fuzzy).

Perintah yang didukung ESP32: `mist`, `irigasi`, `blower`, `auto`, `restart`.

## 7. Catatan data sensor

`pushSensorLog` kini mengirim `soil` (kelembapan tanah %) bersama `temperature`, `humidity`,
`vpd`, `et0`, `ts`. Dashboard menampilkan `soil` di card metrik "Tanah" dan kolom tabel.
