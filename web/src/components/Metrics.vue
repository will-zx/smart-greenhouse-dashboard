<script setup>
import { computed } from 'vue';
import { state, fmtNum, fmtMl } from '../store.js';

const d = computed(() => state.lastData);
const ig = computed(() => d.value?.irigasiInfo || null);

// Status probe tanah: soilN = jumlah probe valid (dari firmware). Firmware lama
// tidak mengirimnya -> sudah dihitung di store dari field yang ada, jadi di
// sini tidak perlu menebak lagi. Tidak ada data -> tidak tampil apa pun.
const soilHint = computed(() => {
  const n = d.value?.soilN;
  if (typeof n !== 'number') return null;
  if (n === 3) return { text: '3/3 probe', cls: 'text-muted' };
  if (n >= 1) return { text: n + '/3 probe', cls: 'text-warn' };
  return { text: 'probe tidak terdeteksi', cls: 'text-danger' };
});

// Sisa anggaran = anggaran harian - terpakai hari ini. Hanya dihitung kalau
// kedua angkanya ada; kalau salah satu tidak dikirim (firmware lama) barisnya
// disembunyikan, bukan dianggap 0.
const sisaAir = computed(() => {
  const b = ig.value?.budgetMl;
  const u = ig.value?.usedMl;
  if (typeof b !== 'number' || typeof u !== 'number') return null;
  const sisa = b - u;
  return { text: sisa > 0 ? 'sisa ' + fmtMl(sisa) : 'anggaran habis', cls: sisa > 0 ? 'text-muted' : 'text-warn' };
});

// Satu array + v-for, bukan 8 blok markup yang almost identik.
// Nilai yang tidak terbaca TIDAK ada di record (firmware membuang NaN) ->
// tampil "-", bukan 0,00 palsu.
const metrics = computed(() => {
  const row = d.value;
  const list = [
    { key: 'temp', label: 'Suhu', value: fmtNum(row?.temperature), unit: '°C', icon: 'M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z' },
    { key: 'hum', label: 'Kelembapan', value: fmtNum(row?.humidity), unit: '%', icon: 'M12 3s6 7 6 11a6 6 0 0 1-12 0c0-4 6-11 6-11z' },
    // Tanah ditampilkan apa adanya (firmware sudah clamp 0..100); null -> "-".
    { key: 'soil', label: 'Tanah', value: fmtNum(row?.soil, 0), unit: '%', hint: soilHint.value, icon: 'M7 20h10M10 20c5.5-2.5.8-6.4 3-10M9.5 9.4c1.1.8 1.8 2.2 2.3 3.7-2 .4-3.5.4-4.8-.3-1.2-.6-2.3-1.9-3-4.2 2.8-.5 4.4 0 5.5.8zM14.1 6a7 7 0 0 0-1.1 4c1.9-.1 3.3-.6 4.3-1.4 1-1 1.6-2.3 1.7-4.6-2.7.1-4 1-4.9 2z' },
    { key: 'vpd', label: 'VPD', value: fmtNum(row?.vpd), unit: 'kPa', icon: 'm12 14 4-4M3.34 19a10 10 0 1 1 17.32 0' },
    // ETc = ET0 x Kc. Firmware mengirim nilainya di field et0 (nilai ETc sudah
    // dihitung di dalam), label di sini ETc supaya sama dengan LCD & proposal.
    { key: 'etc', label: 'ETc', value: fmtNum(row?.etc), unit: 'mm/hari', icon: 'M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z' },
    { key: 'tds', label: 'TDS', value: fmtNum(row?.tds, 0), unit: 'ppm', icon: 'M9 3h6M10 3v6.5L4.6 18a2 2 0 0 0 1.7 3h11.4a2 2 0 0 0 1.7-3L14 9.5V3M7.5 15h9' },
  ];

  // Dua kartu air: anggaran harian (dari ETc) dan yang sudah dialirkan hari ini.
  // Hanya muncul kalau perangkat mengirim field-nya — firmware lama tidak.
  const budget = fmtMl(ig.value?.budgetMl);
  if (budget) list.push({
    key: 'budget', label: 'Dosis harian', value: budget, unit: '',
    hint: sisaAir.value, icon: 'M12 2.7S6.5 9 6.5 13a5.5 5.5 0 0 0 11 0c0-4-5.5-10.3-5.5-10.3zM9 13.5a3 3 0 0 0 3 3',
  });
  const used = fmtMl(ig.value?.usedMl);
  if (used) list.push({
    key: 'used', label: 'Air terpakai', value: used, unit: '',
    hint: { text: 'hari ini', cls: 'text-muted' },
    icon: 'M4 20h16M6 20V9l6-5 6 5v11M10 20v-6h4v6',
  });

  return list;
});
</script>

<template>
  <!-- 6 metrik dasar + 2 kartu air (kalau ada) -> 2 kolom x 3 baris di >=900px.
        2x3 juga menyeimbangkan tinggi dengan kartu Kontrol di sebelahnya.
        Sama seperti .home-top .metrics di style.css lama:319-321.
        Di 700-899px jadi 3 kolom, di bawah itu 2 kolom. -->
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
        {{ m.value }}<small v-if="m.unit" class="text-[13px] font-semibold text-muted max-[420px]:text-[11px]"> {{ m.unit }}</small>
      </div>
      <!-- Baris kecil opsional (status probe tanah, sisa anggaran, "hari ini").
           Hanya kartu yang punya keterangan yang menampilkannya. -->
      <p v-if="m.hint" class="mt-[3px] text-[10.5px] font-semibold leading-snug" :class="m.hint.cls">{{ m.hint.text }}</p>
    </div>
  </div>
</template>
