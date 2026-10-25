<script setup>
import { computed } from 'vue';
import { state, fmtNum } from '../store.js';

const d = computed(() => state.lastData);

// soil = rata-rata probe valid dari perangkat (jangan dihitung ulang client).
// soil2/soil3 hanya ada kalau probe terbaca; record lama (sebelum b9c5e3a) tidak
// punya keduanya -> null, tidak digambar 0.
const probes = computed(() => {
  const r = d.value;
  if (!r) return [];
  const out = [];
  if (r.soil !== null) out.push({ no: 1, value: r.soil, main: true });
  if (r.soil2 !== null) out.push({ no: 2, value: r.soil2 });
  if (r.soil3 !== null) out.push({ no: 3, value: r.soil3 });
  return out;
});

const mainValue = computed(() => probes.value.find(p => p.main)?.value ?? null);
</script>

<template>
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-3 flex items-center justify-between">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7 20h10M10 20c5.5-2.5.8-6.4 3-10M9.5 9.4c1.1.8 1.8 2.2 2.3 3.7-2 .4-3.5.4-4.8-.3-1.2-.6-2.3-1.9-3-4.2 2.8-.5 4.4 0 5.5.8zM14.1 6a7 7 0 0 0-1.1 4c1.9-.1 3.3-.6 4.3-1.4 1-1 1.6-2.3 1.7-4.6-2.7.1-4 1-4.9 2z"/></svg>
        Kelembapan Tanah
      </h2>
      <span class="text-[20px] font-bold text-ink">
        {{ fmtNum(mainValue, 0) }} %
      </span>
    </div>
    <p class="mb-3 text-[11px] text-muted">
      Nilai utama = rata-rata probe tanah yang valid (dari perangkat, keputusan fuzzy).
    </p>

    <div v-if="probes.length" class="space-y-1">
      <div v-for="p in probes" :key="p.no" class="flex items-center justify-between text-[11.5px]">
        <span class="text-muted">Probe {{ p.no }}{{ p.main ? ' (utama)' : '' }}</span>
        <span class="font-semibold">{{ fmtNum(p.value, 0) }} %</span>
      </div>
    </div>
    <div v-else class="mt-2 text-[11px] text-muted">
      Belum ada data kelembapan tanah (record lama / probe belum terpasang).
    </div>
  </div>
</template>
