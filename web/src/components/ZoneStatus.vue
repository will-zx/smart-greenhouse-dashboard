<script setup>
import { computed } from 'vue';
import { state, humanAge, zoneId } from '../store.js';

// Pill unit menampilkan UMUR DATA, bukan nama unit: di topbar sempit (360px)
// yang penting adalah kesegaran data, sedangkan nama unit sudah ada di judul
// kartu dan di popup daftar.
//
// Record tanpa field ts (jam perangkat belum sinkron) punya warna sendiri:
// TIDAK boleh hijau (seolah-olah segar) dan tidak boleh "NaN menit" —
// pork: perangkat hidup, tapi jamnya belum sinkron.
//
// Warna teks HANYA ditentukan per status: kalau kelas dasar tombol juga
// membawa `text-white`, dua utility warna teks bertabrakan di elemen yang sama
// dan pemenangnya ditentukan urutan di CSS hasil build — bukan urutan class.
//
// Hover TIDAK boleh memakai latar beralfa (putih/hitam transparan): pill ini
// duduk di atas topbar biru, dan lapisan transparan tembus ke induk sehingga
// latarnya berubah jadi biru — teks krem di atas biru praktis hilang (1,03:1).
// Hover sekarang satu aturan di kelas dasar tombol: filter brightness-95, yang
// meredupkan warna pill itu sendiri tanpa mencampurnya dengan apa pun di
// belakangnya. `transition` (bukan transition-colors) supaya filter ikut
// beranimasi.
const OLD = 'bg-warn-soft text-warn border-warn-line';
const DARK = 'bg-off text-white border-white/30';

// Tiga tingkat kesegaran data, memakai token yang sama dengan .pill-warn di
// tabel log (krem = peringatan ringan, bukan kuning menyala yang bertabrakan
// dengan biru topbar):
//   segar (<=60 s)          hijau  bg-ok / teks putih
//   data lama (>60 s)       krem   bg-warn-soft / teks warn, tepi warn-line
//   gagal baca / >1 hari    merah  bg-off / teks putih
const cls = computed(() => {
  if (!state.online) return DARK;
  if (state.age === null) return OLD;
  if (state.age > 86400) return DARK;
  if (state.age > 60) return OLD;
  return 'bg-ok text-white border-white/30';
});

const label = computed(() => {
  if (!state.online) return 'gagal baca';
  if (state.age === null) return 'jam belum sinkron';
  return humanAge(state.age);
});

// Teks lengkap untuk hover (desktop) dan pembaca layar. Di layar sentuh `title`
// tidak muncul — teks penuhnya tampil di ZoneMenu, di samping nama unit aktif.
const ket = computed(() => {
  if (!state.online) return 'Gagal membaca data greenhouse';
  if (state.age === null) return 'Jam perangkat belum sinkron — record terakhir tidak punya field ts';
  return `Data terakhir masuk ${humanAge(state.age)} lalu`;
});
</script>

<template>
  <!-- Wrapper, bukan button, yang jadi flex item: button punya overflow-hidden
       (untuk truncate) sehingga tooltip di dalam-nya ikut terpotong. Wrapper
       ini yang harus bisa menyusut. -->
  <div class="relative flex min-w-0 flex-1">
    <button
      type="button"
      class="flex min-w-0 flex-1 items-center gap-1.5 overflow-hidden rounded-lg border px-2.5 py-1.5 text-[13px] font-semibold transition hover:brightness-95 max-[420px]:pr-1.5"
      :class="cls"
      :title="ket"
      :aria-label="ket"
      @click="$emit('toggle')"
    >
      <svg class="shrink-0" viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <path d="M3 10.5 12 3l9 7.5"/><path d="M5 9.5V21h14V9.5"/><path d="M9.5 21v-6h5v6"/>
      </svg>
      <!-- flex-1 WAJIB: tanpa itu basisnya auto sehingga span ikut lebar
           konten, `truncate` tidak pernah aktif, dan teks panjang (mis.
           "23 jam 59 menit") meluber keluar button lalu menabrak avatar.
           flex-1 = flex:1 1 0% → basis nol, teks selalu terpotong ellipsis. -->
      <span class="min-w-0 flex-1 truncate">{{ label }}</span>
      <svg class="hidden shrink-0 min-[420px]:block" viewBox="0 0 24 24" width="12" height="12" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <path d="M6 9l6 6 6-6"/>
      </svg>
    </button>
  </div>
</template>