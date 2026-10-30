<script setup>
import { computed } from 'vue';
import { state, actuators, manualInfo, switchOn, onSwitch, releaseAll } from '../store.js';

const a = computed(() => actuators.value);
const mi = computed(() => manualInfo.value);

// Telemetri irigasi: hanya durasi pompa dan jeda minimum. Dosis harian,
// anggaran, dan air terpakai sudah tampil di kartu metrik — jangan diulang.
// Field tidak ada (firmware lama) -> bloknya disembunyikan, bukan angka kosong.
const ig = computed(() => state.lastData?.irigasiInfo || null);
const igLine = computed(() => {
  const r = ig.value;
  if (!r) return null;
  const parts = [];
  if (r.doseMs) parts.push(Math.round(r.doseMs / 1000) + ' s pompa');
  if (r.intervalMin) parts.push('jeda min ' + Math.round(r.intervalMin / 60) + ' jam');
  return parts.length ? parts.join(' \u00b7 ') : null;
});

// Kartu Kontrol tetap terlihat untuk semua orang (=status aktuator berguna untuk
// dipantau), tetapi sakelarnya disembunyikan bila belum login admin.
//
// `icon` = array path. Blower butuh ini: satu bilah digambar 3× lalu dirotasi
// 120°. Path lama mengulang path yang SAMA tanpa rotasi, jadi yang tampil
// gumpalan, bukan kipas. Bilahnya "sapu melingkar" (swept) — kelopok simetris
// justru terbaca sebagai bunga, bukan kipas.
const BLADE = 'M12 12c-.4-2.6.4-4.8 2.6-5.9 1.9-.9 4-.2 4.8 1.6.8 1.9-.1 4-1.9 4.9-1.6.8-3.2.3-4.4-.6';
const spin = d => [0, 120, 240].map(r => ({ d, t: `rotate(${r} 12 12)` }));

// Teks mode override (baris kecil di bawah nama aktuator).
//   auto  -> ikut fuzzy (tidak ada override)
//   on    -> paksa nyala
//   off   -> paksa mati
//   unknown -> ada override tapi perangkat tidak menyebut aktuator mana
const OV_TEXT = {
  auto: 'Auto (ikut fuzzy)',
  on: 'Paksa nyala',
  off: 'Paksa mati',
  unknown: 'Override (tidak dilaporkan)',
};

// Posisi sakelar = kehendak operator (langsung bergerak saat diklik, lalu
// menyesuaikan record perangkat). Warna track hanya nyala/mati; mode override
// (auto / paksa nyala / paksa mati) dibawa teks subline + banner di atas.
const TIP = (nama, aksi) =>
  `Sakelar ${nama}: posisi = perintah yang barusan dikirim, lalu disesuaikan dengan ` +
  `laporan perangkat. ${aksi}. Override berakhir sendiri setelah 10 menit; ` +
  'kembali ke fuzzy: tombol "Kembali ke Auto".';

const items = computed(() => [
  { key: 'mist', label: 'Misting', on: a.value.mist, icon: [{ d: 'M4 15a7 7 0 1 1 11.7-7H17a4.5 4.5 0 0 1 2.5 8.2M8 16v1.5M12 16v2M16 16v1.5' }],
    tip: TIP('misting', 'Nyala = paksa nyala, mati = paksa mati') },
  { key: 'irigasi', label: 'Irigasi', on: a.value.irigasi, icon: [{ d: 'M7 3s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7zM17 10s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7z' }],
    tip: TIP('irigasi', 'Nyala = paksa nyala, mati = paksa mati') + ' Paksa nyala menjalankan SATU siklus (FILL → FERT → DIST); paksa mati membatalkan siklus.' },
  { key: 'blower', label: 'Blower', on: a.value.blower, icon: spin(BLADE), duty: a.value.level,
    tip: TIP('blower', 'Nyala = paksa nyala, mati = paksa mati') + ' Duty di kanan = keluaran fuzzy yang berjalan.' },
]);

// Teks mode override per aktuator: 'on' | 'off' | 'auto' | 'unknown'.
const ovOf = k => a.value.override[k] || 'auto';
</script>

<template>
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
        Kontrol Aktuator
      </h2>
      <span class="text-xs text-muted">{{ state.isAdmin ? 'Kontrol remot via Firebase RTDB' : 'Hanya lihat — login untuk kontrol manual' }}</span>
    </div>

    <!-- Indikator mode manual: aktuator mana yang sedang di-override.
         Override manual kedaluwarsa sendiri setelah 10 menit, jadi tidak ada
         tombol "kembali ke auto" yang wajib ditekan. -->
    <div
      v-if="mi.any"
      class="mb-3 rounded-lg border border-accent bg-accent-soft px-3 py-2 text-[11.5px] leading-snug text-accent-dark"
    >
      <b>Mode manual aktif</b>
      <span v-if="mi.items.length"> — {{ mi.items.map(i => i.name + ' (' + i.mode + ')').join(', ') }}</span>
      <span v-else> — aktuator yang di-override tidak dilaporkan perangkat</span>
      <span class="block text-muted">Override expires sendiri setelah 10 menit, lalu aktuator kembali ke fuzzy.</span>
    </div>

    <!-- IRRIGOFF: irigasi otomatis sengaja dimatikan di perangkat (debit pompa
         belum dikalibrasi). Ini kondisi yang disengaja, bukan gangguan —
         tampil juga untuk tamu karena menyangkut kondisi perangkat. -->
    <div
      v-if="state.lastData && state.lastData.flags && state.lastData.flags.irrigOff"
      class="mb-3 rounded-lg border border-warn bg-warn-soft px-3 py-2 text-[11.5px] leading-snug text-warn"
    >
      <b>Irigasi otomatis dimatikan di perangkat</b> — debit pompa belum dikalibrasi. Perintah manual tetap bisa dijalankan.
    </div>

    <!-- Irigasi alasan tahan: peringatan (kuning), bukan info kecil.
         Kondisi perangkat -> tampil juga untuk tamu. -->
    <div
      v-if="ig && ig.holdReason"
      class="mb-3 rounded-lg border border-warn bg-warn-soft px-3 py-2 text-[11.5px] leading-snug text-warn"
    >
      <b>Siklus irigasi ditahan</b> — {{ ig.holdReason }}
    </div>

    <!-- Peringatan non-blocking: air tetap dialirkan, jangan sebut "ditahan". -->
    <div
      v-if="ig && ig.warning"
      class="mb-3 rounded-lg border border-warn bg-warn-soft px-3 py-2 text-[11.5px] leading-snug text-warn"
    >
      <b>Peringatan irigasi</b> — {{ ig.warning }}
    </div>

    <!-- Telemetri irigasi: dosis dari ETc, durasi pompa, jeda minimum, dan
         progres siklus (anggaran harian vs terpakai hari ini).
         disembunyikan kalau perangkat belum mengirimnya. -->
    <div
      v-if="igLine"
      class="mb-3 rounded-lg border border-border bg-card-2 px-3 py-2 text-[11px] leading-snug text-muted"
    >
      {{ igLine }}
    </div>

    <div class="flex flex-col gap-2">
      <div v-for="it in items" :key="it.key" class="flex items-center gap-2.5 rounded-lg border border-border bg-card-2 px-3.5 py-2.5">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path v-for="(p, pi) in it.icon" :key="pi" :d="p.d" :transform="p.t" /></svg>
        <div class="min-w-0">
          <h3 class="whitespace-nowrap text-[13.5px] font-semibold">{{ it.label }}</h3>
          <!-- Status aktuator (yang SEDANG berjalan) terpisah dari status
               override — operator perlu tahu apa yang benar-benar jalan. -->
          <span class="block text-[10.5px] text-muted">
            {{ OV_TEXT[ovOf(it.key)] }}
            <span class="opacity-70">· status {{ it.on ? 'MENYALA' : 'MATI' }}</span>
          </span>
        </div>

        <div class="ml-auto flex items-baseline gap-2.5">
          <!-- Duty blower = keluaran fuzzy (PWM), huruf kecil di bawah status. -->
          <span v-if="it.duty !== undefined && it.duty !== null" class="text-[11.5px] text-muted">Duty {{ Math.round(it.duty * 100) }} %</span>
          <span class="text-[15px] font-bold" :class="it.on ? 'text-ok' : 'text-danger'">{{ it.on ? 'ON' : 'OFF' }}</span>

          <!-- Sakelar hanya untuk admin. Kartu kontrol tetap terlihat untuk
               semua orang (status ON/OFF berguna dipantau), tapi tanpa login
               tidak boleh ada kontrol yang bisa diklik — sama seperti
               `body.read-only .switch { display:none }` di style.css lama.
               Penolakan sesungguhnya tetap di RTDB rules + sendCommand().

               Posisi knob = kehendak operator: langsung bergerak saat diklik,
               lalu menyesuaikan record perangkat. Warna track hanya nyala/mati
               (mode dibawa teks subline di atas). Warna saja tidak terbaca
               pembaca layar, jadi input diberi aria-label aksi berikutnya. -->
          <label
            v-if="state.isAdmin"
            class="switch relative inline-block h-[18px] w-[34px] shrink-0 cursor-pointer"
            :title="it.tip"
          >
            <input
              type="checkbox" class="sr-only"
              :checked="switchOn[it.key]"
              :aria-label="`${it.label}: ${switchOn[it.key] ? 'matikan (paksa mati)' : 'nyalakan (paksa nyala)'}`"
              @change="onSwitch(it.key, $event.target.checked)"
            >
            <span
              class="absolute inset-0 rounded-full transition-colors duration-200"
              :class="switchOn[it.key] ? 'bg-accent-dark' : 'bg-track'"
            />
            <span
              class="absolute left-0.5 top-0.5 h-[14px] w-[14px] rounded-full bg-white transition-transform duration-200"
              :class="switchOn[it.key] && 'translate-x-[16px]'"
            />
          </label>
        </div>
      </div>
    </div>

    <!-- "Kembali ke Auto" SELALU ada (admin): override dilepas untuk semua
         aktuator sekaligus. Sakelar per aktuator hanya melepas override
         aktuator itu. -->
    <button
      v-if="state.isAdmin"
      type="button"
      class="mt-3 inline-flex w-full items-center justify-center gap-1.5 rounded-lg border border-border bg-card px-3 py-2 text-[12.5px] font-semibold text-ink hover:bg-card-2 disabled:opacity-50"
      title="Lepas override manual semua aktuator — semua kembali ke fuzzy"
      @click="releaseAll()"
    >
      <svg class="text-accent" viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 3v6h-6"/></svg>
      Kembali ke Auto
    </button>

    <p v-if="state.cmdStatus" class="mt-3 min-h-[14px] text-center text-[11px] font-semibold text-muted">{{ state.cmdStatus }}</p>
    <p v-if="state.cmdError" class="mt-1 min-h-[14px] text-center text-[11px] font-semibold text-danger">{{ state.cmdError }}</p>
  </div>
</template>
