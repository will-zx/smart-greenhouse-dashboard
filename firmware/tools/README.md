# Simulasi logika fuzzy (Octave)

Replika mesin fuzzy yang berjalan di `firmware/src/fuzzy.cpp`, dipakai untuk
menghitung duty cycle PWM blower pada berbagai kondisi dan membuat grafik.

## Menjalankan

```bash
sudo pacman -S gnuplot      # sekali saja, untuk plot headless
octave --no-gui --eval "addpath('firmware/tools'); fuzzy_sim"
```

Tanpa gnuplot, tabel hasil dan `fuzzy_rules.fis` tetap dibuat; hanya plot yang
dilewati (bukan error).

## Keluaran

| Berkas | Isi |
|---|---|
| `out/01_membership.png` | kurva trapesium tiap masukan (suhu, RH, tanah, VPD, ETc) |
| `out/02_peta_blower.png` | peta panas level blower: suhu × RH (VPD 1,5 kPa) dan suhu × VPD (RH 60 %) |
| `out/03_sapuan_duty.png` | duty blower vs kelembapan udara pada T = 30 dan 34 °C, plus ambang logika dan ambang putar kipas |
| `out/04_misting_irigasi.png` | level misting dan irigasi vs kelembapan tanah untuk dua nilai ETc |
| `fuzzy_rules.fis` | model Mamdani 3 keluaran, bisa dibuka di MATLAB `fuzzyLogicDesigner` / `readfis` |

## Yang diverifikasi skrip

Tabel kasus acuan di bagian `expect` dibandingkan dengan hasil hitungan; skrip
berhenti dengan `assert` kalau selisihnya lebih dari 5e-4. Salah satu kasusnya
memakai masukan yang sama dengan contoh perhitungan di proposal 2.2.4
(30 °C, RH 70 %, tanah 35 %, VPD 1,6 kPa → misting 0,7356), jadi skrip ini
sekaligus menguji apakah tabel rule firmware masih cocok dengan yang tertulis di
proposal.

## Batasan yang perlu diketahui

1. **Bukan fuzzy yang memilih duty.** Duty adalah `level × 255`; level sendiri
   adalah rata-rata berbobot level aturan yang aktif (`Σ w·level / Σ w`).
   Karena semua level berupa singleton, nilai duty yang muncul praktis hanya
   {0, 40 %, 70 %, 100 %}, atau campuran bila dua aturan dengan level berbeda
   aktif bersamaan.
2. **Sugeno-0, bukan Mamdani murni** di firmware. Untuk consequent segitiga
   berlebar sama, centroid hasil max-aggregation sama dengan rata-rata berbobot
   itu, jadi `.fis` yang dihasilkan cukup dekat (< 1 %), tapi angka resmi tetap
   yang keluar dari skrip ini.
3. **Duty minimum dibulatkan naik ke 20 %** (`actuator.cpp` + `BLOWER_MIN_DUTY` di
   `config.h`). Kipas hasil uji `hw_test` baru berputar di duty ~20 % dan berhenti
   di ~13 %, jadi level fuzzy di antara 0 dan 0,20 dinaikkan ke 0,20 agar kipas
   benar-benar menyala. Kasus (18 °C, RH 50 %, tanah 40 %, VPD 0,3 kPa) punya
   level 0,16 dan dikirim sebagai 51/255 = 20 %.
4. **Ada zona mati RH 55–70 %.** Tabel `BLOWER_RULES` hanya menyalakan kipas dari
   RH tinggi (≥70 %) atau RH rendah (≤55 %) atau VPD rendah (≤0,5 kPa). Di
   antaranya tidak ada aturan yang aktif, sehingga duty jatuh dari 40 % ke 0 %
   lalu naik lagi ke 85 % (lihat `out/03_sapuan_duty.png`), sehingga klaim
   "perubahan bertahap tanpa lompatan mendadak" di Tabel 3.4 baris 3 belum
   terbukti oleh konfigurasi rule saat ini.

## Catatan temuan di firmware

1. **Sudah diperbaiki (18 Okt 2026):** duty minimum kipas. Level di bawah
   `BLOWER_MIN_DUTY` (0,20) dinaikkan di `actuator.cpp` sebelum dikirim ke LEDC.
   `fuzzy.cpp` sengaja tidak diubah supaya lapisan keputusan tetap murni.
2. `BLOWER_RULES` tidak punya aturan `V_HIGH`, padahal `MIST_RULES` punya.
   Akibatnya pada RH 60 % blower **tidak bergerak sama sekali** apa pun nilai VPD
   (lihat panel kanan `out/02_peta_blower.png`), termasuk VPD 2 kPa.
3. Histeresis yang diklaim pada proposal 2.2.8 belum ada di kode: begitu
   `blowerOn` bernilai false, level dibuang. Pembulatan minimum duty bukan
   pengganti histeresis.

Kalau tabel rule diubah, ubah `fuzzy.cpp` lalu salin ulang tabelnya ke skrip ini
(nomor baris sumbernya ada di setiap blok) — nilai acuan di `expect` harus ikut
diperbarui dan `pio run` wajib SUCCESS.