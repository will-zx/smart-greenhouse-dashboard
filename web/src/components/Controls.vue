<script setup>
import { computed } from 'vue';
import { state, actuators, onSwitch } from '../store.js';

const a = computed(() => actuators.value);

// Kartu Kontrol tetap terlihat untuk semua orang (=status aktuator berguna untuk
// dipantau), tetapi sakelarnya disembunyikan bila belum login admin.
//
// `icon` = array path. Blower butuh ini: satu bilah digambar 3× lalu dirotasi
// 120°. Path lama mengulang path yang SAMA tanpa rotasi, jadi yang tampil
// gumpalan, bukan kipas. Bilahnya "sapu melingkar" (swept) — kelopok simetris
// justru terbaca sebagai bunga, bukan kipas.
const BLADE = 'M12 12c-.4-2.6.4-4.8 2.6-5.9 1.9-.9 4-.2 4.8 1.6.8 1.9-.1 4-1.9 4.9-1.6.8-3.2.3-4.4-.6';
const spin = d => [0, 120, 240].map(r => ({ d, t: `rotate(${r} 12 12)` }));

const items = computed(() => [
  { key: 'mist', label: 'Misting', on: a.value.mist, icon: [{ d: 'M4 15a7 7 0 1 1 11.7-7H17a4.5 4.5 0 0 1 2.5 8.2M8 16v1.5M12 16v2M16 16v1.5' }] },
  { key: 'irigasi', label: 'Irigasi', on: a.value.irigasi, icon: [{ d: 'M7 3s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7zM17 10s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7z' }] },
  { key: 'blower', label: 'Blower', on: a.value.blower, icon: spin(BLADE), duty: a.value.level },
  { key: 'auto', label: 'Auto', on: a.value.auto, icon: [{ d: 'M21 12a9 9 0 1 1-3-6.7M21 3v5h-5' }] },
]);
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

    <div class="flex flex-col gap-2">
      <div v-for="it in items" :key="it.key" class="flex items-center gap-2.5 rounded-lg border border-border bg-card-2 px-3.5 py-2.5">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path v-for="(p, pi) in it.icon" :key="pi" :d="p.d" :transform="p.t" /></svg>
        <h3 class="whitespace-nowrap text-[13.5px] font-semibold">{{ it.label }}</h3>

        <div class="ml-auto flex items-baseline gap-2.5">
          <!-- Duty blower = keluaran fuzzy (PWM), huruf kecil di bawah status. -->
          <span v-if="it.duty !== undefined && it.duty !== null" class="text-[11.5px] text-muted">Duty {{ Math.round(it.duty * 100) }} %</span>
          <span class="text-[15px] font-bold text-danger" :class="it.on && 'text-ok'">{{ it.on ? 'ON' : 'OFF' }}</span>

          <!-- Sakelar hanya untuk admin. Kartu kontrol tetap terlihat untuk
               semua orang (status ON/OFF berguna dipantau), tapi tanpa login
               tidak boleh ada kontrol yang bisa diklik — sama seperti
               `body.read-only .switch { display:none }` di style.css lama.
               Penolakan sesungguhnya tetap di RTDB rules + sendCommand(). -->
          <label
            v-if="state.isAdmin"
            class="switch relative inline-block h-[18px] w-[34px] shrink-0 cursor-pointer"
          >
            <input
              type="checkbox" class="sr-only"
              :checked="it.on"
              @change="onSwitch(it.key, $event.target.checked)"
            >
            <span
              class="absolute inset-0 rounded-full transition-colors duration-200"
              :class="it.on ? 'bg-accent-dark' : 'bg-track'"
            />
            <span
              class="absolute left-0.5 top-0.5 h-[14px] w-[14px] rounded-full bg-white transition-transform duration-200"
              :class="it.on && 'translate-x-[16px]'"
            />
          </label>
        </div>
      </div>
    </div>

    <p class="mt-3 min-h-[14px] text-center text-[11px] font-semibold text-muted">{{ state.cmdStatus }}</p>
  </div>
</template>
