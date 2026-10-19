<script setup>
import { state, filteredLogs, fmtLogTime, fmtNum, saneSoil, pillClass, fetchDataLogs, refreshWithSpin, zoneName } from '../store.js';

const setLimit = e => {
  state.logLimit = parseInt(e.target.value, 10) || 20;
  fetchDataLogs();   // jumlah baris berubah -> ambil ulang dari RTDB
};
</script>

<template>
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3 max-[420px]:flex-col max-[420px]:items-start">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>
        Histori Data Log Sensor
        <span class="font-medium text-muted">{{ zoneName() }}</span>
      </h2>
      <button type="button" class="inline-flex items-center gap-1.5 rounded-lg border border-border bg-card px-3 py-1.5 text-xs font-semibold text-ink hover:bg-card-2" title="Muat ulang data sensor" @click="refreshWithSpin()">
        <svg class="text-accent" :class="state.spinning && 'animate-spin'" viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 3v6h-6"/></svg>
        Refresh
      </button>
    </div>

    <div class="mb-3 flex flex-wrap items-center gap-3">
      <label class="flex items-center gap-1.5 text-[12.5px] text-muted" for="showEntries">
        Show
        <select id="showEntries" :value="state.logLimit" class="rounded-md border border-border bg-card px-1.5 py-[3px] font-inherit text-[12.5px] text-ink"
                @change="setLimit">
          <option :value="10">10</option>
          <option :value="20">20</option>
          <option :value="50">50</option>
        </select>
        entries
      </label>
      <input type="search" v-model="state.logFilter" class="rounded-lg border border-border bg-card px-3 py-[7px] text-[12.5px] text-ink outline-none focus:border-accent focus:shadow-[0_0_0_2px_rgba(21,101,192,.14)] max-[700px]:w-full"
             placeholder="Search: waktu atau status" aria-label="Cari log">
    </div>

    <div class="relative overflow-x-auto rounded-lg border border-border">
      <table class="w-full border-collapse text-[13px] max-[420px]:text-[12px]">
        <thead>
          <tr>
            <th class="sticky left-0 z-2 whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">Waktu (lokal)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">Suhu (°C)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">Kelembapan (%)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">Tanah (%)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">VPD (kPa)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">ET<sub class="text-[.75em]">0</sub> (mm/hari)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">TDS (ppm)</th>
            <th class="whitespace-nowrap border-b-2 border-border bg-card-2 px-3.5 py-2.5 text-left text-[11px] font-semibold uppercase tracking-[.4px] text-muted max-[420px]:px-2.5">Status Sistem</th>
          </tr>
        </thead>
        <tbody>
          <tr v-if="state.loadError">
            <td colspan="8" class="px-3.5 py-6 text-center text-danger">
              Gagal memuat data ({{ state.loadError }}) — coba Refresh
            </td>
          </tr>
          <tr v-else-if="!filteredLogs.length">
            <td colspan="8" class="px-3.5 py-6 text-center text-muted">
              {{ state.logFilter ? 'Tidak ada baris yang cocok' : 'Belum ada data' }}
            </td>
          </tr>
          <template v-else>
            <!-- terbaru di atas -->
            <tr v-for="log in [...filteredLogs].reverse()" :key="log.ts" class="hover:bg-accent-soft">
              <td class="sticky left-0 z-1 whitespace-nowrap border-b border-border bg-card px-3.5 py-2.5 max-[420px]:px-2.5">{{ fmtLogTime(log.ts) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(log.temperature) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(log.humidity) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(saneSoil(log.soil), 0) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(log.vpd) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(log.et0) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 font-semibold text-ink max-[420px]:px-2.5">{{ fmtNum(log.tds, 0) }}</td>
              <td class="whitespace-nowrap border-b border-border px-3.5 py-2.5 max-[420px]:px-2.5">
                <span :class="pillClass(log.status_sistem || '')">{{ log.status_sistem }}</span>
              </td>
            </tr>
          </template>
        </tbody>
      </table>
    </div>

    <p class="mt-3 min-h-4 text-xs text-muted">
      {{ filteredLogs.length
        ? (state.logFilter
            ? `Menampilkan ${filteredLogs.length} dari ${state.logData.length} baris yang cocok`
            : `Menampilkan ${filteredLogs.length} baris terbaru`)
        : '' }}
    </p>
  </div>
</template>
