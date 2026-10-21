<script setup>
import { computed } from 'vue';
import { state, actuators, manualInfo, onSwitch, releaseAll } from '../store.js';

const a = computed(() => actuators.value);
const mi = computed(() => manualInfo.value);

// Kartu Kontrol tetap terlihat untuk semua orang (=status aktuator berguna untuk
// dipantau), tetapi sakelarnya disembunyikan bila belum login admin.
//
// `icon` = array path. Blower butuh ini: satu bilah digambar 3× lalu dirotasi
// 120°. Path lama mengulang path yang SAMA tanpa rotasi, jadi yang tampil
// gumpalan, bukan kipas. Bilahnya "sapu melingkar" (swept) — kelopok simetris
// justru terbaca sebagai bunga, bukan kipas.
const BLADE = 'M12 12c-.4-2.6.4-4.8 2.6-5.9 1.9-.9 4-.2 4.8 1.6.8 1.9-.1 4-1.9 4.9-1.6.8-3.2.3-4.4-.6';
const spin = d => [0, 120, 240].map(r => ({ d, t: `rotate(${r} 12 12)` }));

// Sakelar = status OVERRIDE, bukan status aktuator.
//   auto  -> sakelar mati, aktuator ikut fuzzy
//   on    -> sakelar nyala, aktuator dipaksa nyala
//   off   -> paksa mati (hanya mungkin di firmware lama)
//   unknown -> ada override tapi perangkat tidak menyebut aktuator mana
const OV_TEXT = {
  auto: 'Auto (ikut fuzzy)',
  on: 'Paksa nyala',
  off: 'Paksa mati',
  unknown: 'Override (tidak dilaporkan)',
};

const items = computed(() => [
  { key: 'mist', label: 'Misting', on: a.value.mist, icon: [{ d: 'M4 15a7 7 0 1 1 11.7-7H17a4.5 4.5 0 0 1 2.5 8.2M8 16v1.5M12 16v2M16 16v1.5' }],
    tip: 'Sakelar nyala = misting dipaksa nyala. Sakelar mati = override dilepas, kembali ke fuzzy.' },
  { key: 'irigasi', label: 'Irigasi', on: a.value.irigasi, icon: [{ d: 'M7 3s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7zM17 10s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7z' }],
    tip: 'Sakelar nyala = irigasi dipaksa nyala untuk SATU siklus (FILL → FERT → DIST), lalu status kembali ke MATI.' },
  { key: 'blower', label: 'Blower', on: a.value.blower, icon: spin(BLADE), duty: a.value.level,
    tip: 'Sakelar nyala = blower dipaksa nyala (duty 100%). Sakelar mati = kembali ke fuzzy. Duty shown = keluaran fuzzy yang berjalan.' },
]);

// Sakelar menyala kalau aktuator itu di-override "paksa nyala".
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
          <span class="text-[15px] font-bold text-danger" :class="it.on && 'text-ok'">{{ it.on ? 'ON' : 'OFF' }}</span>

          <!-- Sakelar hanya untuk admin. Kartu kontrol tetap terlihat untuk
               semua orang (status ON/OFF berguna dipantau), tapi tanpa login
               tidak boleh ada kontrol yang bisa diklik — sama seperti
               `body.read-only .switch { display:none }` di style.css lama.
               Penolakan sesungguhnya tetap di RTDB rules + sendCommand().

               Sakelar menampilkan status OVERRIDE: nyala = "paksa nyala",
               mati = "override dilepas, kembali ke fuzzy" (BUKAN "paksa mati"). -->
          <label
            v-if="state.isAdmin"
            class="switch relative inline-block h-[18px] w-[34px] shrink-0 cursor-pointer"
            :title="it.tip"
          >
            <input
              type="checkbox" class="sr-only"
              :checked="ovOf(it.key) === 'on'"
              @change="onSwitch(it.key, $event.target.checked)"
            >
            <span
              class="absolute inset-0 rounded-full transition-colors duration-200"
              :class="ovOf(it.key) === 'on' ? 'bg-accent-dark' : 'bg-track'"
            />
            <span
              class="absolute left-0.5 top-0.5 h-[14px] w-[14px] rounded-full bg-white transition-transform duration-200"
              :class="ovOf(it.key) === 'on' && 'translate-x-[16px]'"
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
