<script setup>
import { computed } from 'vue';
import { state, fmtNum, saneSoil } from '../store.js';

const d = computed(() => state.lastData);

// Satu array + v-for, bukan 6 blok markup yang almost identik.
// Nilai yang tidak terbaca TIDAK ada di record (firmware membuang NaN) ->
// tampil "-", bukan 0,00 palsu.
const metrics = computed(() => [
  { key: 'temp', label: 'Suhu', value: fmtNum(d.value?.temperature), unit: '°C', d: 2, icon: 'M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z' },
  { key: 'hum', label: 'Kelembapan', value: fmtNum(d.value?.humidity), unit: '%', d: 2, icon: 'M12 3s6 7 6 11a6 6 0 0 1-12 0c0-4 6-11 6-11z' },
  { key: 'soil', label: 'Tanah', value: fmtNum(saneSoil(d.value?.soil), 0), unit: '%', d: 0, icon: 'M7 20h10M10 20c5.5-2.5.8-6.4 3-10M9.5 9.4c1.1.8 1.8 2.2 2.3 3.7-2 .4-3.5.4-4.8-.3-1.2-.6-2.3-1.9-3-4.2 2.8-.5 4.4 0 5.5.8zM14.1 6a7 7 0 0 0-1.1 4c1.9-.1 3.3-.6 4.3-1.4 1-1 1.6-2.3 1.7-4.6-2.7.1-4 1-4.9 2z' },
  { key: 'vpd', label: 'VPD', value: fmtNum(d.value?.vpd), unit: 'kPa', d: 2, icon: 'm12 14 4-4M3.34 19a10 10 0 1 1 17.32 0' },
  // ETc = ET0 x Kc. Firmware mengirim nilainya di field et0 (nilai ETc sudah
  // dihitung di dalam), label di sini ETc supaya sama dengan LCD & proposal.
  { key: 'etc', label: 'ETc', value: fmtNum(d.value?.etc), unit: 'mm/hari', d: 2, sub: true, icon: 'M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z' },
  { key: 'tds', label: 'TDS', value: fmtNum(d.value?.tds, 0), unit: 'ppm', d: 0, icon: 'M9 3h6M10 3v6.5L4.6 18a2 2 0 0 0 1.7 3h11.4a2 2 0 0 0 1.7-3L14 9.5V3M7.5 15h9' },
]);
</script>

<template>
  <!-- 6 metrik -> grid 3x2 di layar lebar (bukan auto-fit, yang memecah
       6 kartu jadi 5+1 dan menyisakan baris kosongsendiri). -->
  <!-- 6 metrik -> 2 kolom x 3 baris di >=900px (bukan auto-fit, yang memecah
       6 kartu jadi 4+2 / 5+1 dan terlihat tidak rapi). 2x3 juga menyeimbangkan
       tinggi dengan kartu Kontrol di sebelahnya sehingga tidak ada ruang mati
       di bawah. Sama seperti .home-top .metrics di style.css lama:319-321.
       Di 700-899px jadi 3x2, di bawah itu 2x3. -->
  <div class="grid grid-cols-2 gap-3.5 min-[700px]:grid-cols-3 min-[900px]:grid-cols-2 max-[420px]:gap-2">
    <div
      v-for="m in metrics" :key="m.key"
      class="rounded-xl border border-border bg-card px-[18px] py-4 shadow-[0_1px_2px_rgba(16,24,40,.04)] max-[420px]:px-3.5 max-[420px]:py-3"
    >
      <div class="flex items-center gap-1.5 text-[11px] font-semibold text-muted max-[420px]:text-[10px]">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path :d="m.icon" /></svg>
        {{ m.label }}
      </div>
      <div class="mt-2 text-[28px] font-bold leading-[1.1] text-ink max-[700px]:text-[25px] max-[420px]:text-[21px]">
        {{ m.value }}<small class="text-[13px] font-semibold text-muted max-[420px]:text-[11px]"> {{ m.unit }}</small>
      </div>
    </div>
  </div>
</template>
