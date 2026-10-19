<script setup>
import { ref, onMounted, onUnmounted, watch } from 'vue';
import { Chart, LineController, LineElement, PointElement, LinearScale, CategoryScale, Legend, Tooltip } from 'chart.js';
import { state, fmtClock, fmtNum, saneSoil, humanAge, zoneName, refreshWithSpin } from '../store.js';

// Registrasi manual (bukan chart.js/auto) supaya bundel hanya membawa yang dipakai.
Chart.register(LineController, LineElement, PointElement, LinearScale, CategoryScale, Legend, Tooltip);

// Grafik membaca palet yang sama dengan Tailwind, jadi tidak ada warna yang
// "melenceng" dari skema dashboard.
const cssVar = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();

const el = ref(null);
let chart;

onMounted(() => {
  chart = new Chart(el.value, {
    type: 'line',
    data: {
      labels: [],
      datasets: [
        { label: 'Suhu (°C)', data: [], borderColor: cssVar('--color-danger'), pointBackgroundColor: cssVar('--color-danger'), pointRadius: 3, pointHoverRadius: 5, borderWidth: 2, tension: 0, yAxisID: 'y' },
        { label: 'Kelembapan (%)', data: [], borderColor: cssVar('--color-accent'), pointBackgroundColor: cssVar('--color-accent'), pointRadius: 3, pointHoverRadius: 5, borderWidth: 2, tension: 0, yAxisID: 'y1' }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      // Legenda diletakkan di bawah-tengah seperti grafik referensi: lebih
      // rapi dan tidak memakan ruang di dekat judul kartu.
      plugins: {
        legend: {
          position: 'bottom',
          labels: { usePointStyle: true, pointStyle: 'circle', boxWidth: 7, boxHeight: 7, padding: 18, font: { weight: 600 } }
        },
        tooltip: {
          padding: 10, boxWidth: 8, boxHeight: 8, usePointStyle: true,
          callbacks: {
            // Sumbu X sudah menunjuk waktunya, jadi cukup tampilkan
            // "label + nilai + satuan" - tidak perlu mengulang label.
            label: c => ' ' + c.dataset.label.replace(/\s*\(.*\)/, '') + ': ' +
              c.parsed.y.toFixed(2) + ' ' + (c.dataset.yAxisID === 'y' ? '°C' : '%')
          }
        }
      },
      scales: {
        // suggestedMin/Max, bukan min/max: rentang tetap supaya kemiringan
        // garis jujur. Tanpa ini Chart.js auto-zoom dan 0.5°C terlihat
        // seperti lonjakan besar.
        x: {
          grid: { color: cssVar('--color-card-2') },
          ticks: { color: cssVar('--color-muted'), maxTicksLimit: 8, maxRotation: 0, autoSkipPadding: 12 }
        },
        y: {
          type: 'linear', position: 'left', suggestedMin: 20, suggestedMax: 40,
          grid: { color: cssVar('--color-card-2') },
          border: { display: false },
          ticks: { color: cssVar('--color-danger'), maxTicksLimit: 6 },
          title: { display: true, text: 'Suhu (°C)', color: cssVar('--color-danger'), font: { weight: 600 } }
        },
        y1: {
          type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 100,
          grid: { drawOnChartArea: false },
          border: { display: false },
          ticks: { color: cssVar('--color-accent'), maxTicksLimit: 6 },
          title: { display: true, text: 'Kelembapan (%)', color: cssVar('--color-accent'), font: { weight: 600 } }
        }
      }
    }
  });
  sync();
});

// Data berubah setiap 10 s (polling) -> grafik ikut, tanpa render ulang manual.
watch(() => state.logData, sync);

function sync() {
  if (!chart) return;
  // Sumbu X: jam saja — label penuh ("11/10/2026, 22.27.15") tidak muat.
  chart.data.labels = state.logData.map(l => fmtClock(l.ts));
  chart.data.datasets[0].data = state.logData.map(l => l.temperature);
  chart.data.datasets[1].data = state.logData.map(l => l.humidity);
  chart.update();
}

onUnmounted(() => chart?.destroy());
</script>

<template>
  <div class="mt-5 rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v16a2 2 0 0 0 2 2h16"/><path d="M7 14l4-4 4 4 5-6"/></svg>
        Grafik Tren Sensor
        <span class="font-medium text-muted">{{ zoneName() }}</span>
      </h2>
      <button type="button" class="inline-flex items-center gap-1.5 rounded-lg border border-border bg-card px-3 py-1.5 text-xs font-semibold text-ink hover:bg-card-2" title="Muat ulang data sensor" @click="refreshWithSpin()">
        <svg class="text-accent" :class="state.spinning && 'animate-spin'" viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 3v6h-6"/></svg>
        Refresh
      </button>
    </div>

    <div class="relative h-[320px] max-[700px]:h-[240px]">
      <canvas ref="el" />
    </div>

    <!-- Umur data di bawah grafik, bukan di header: pengguna bisa membedakan
         "sensor diam 40 detik" dari "dashboardnya yang macet". -->
    <p class="mt-3 min-h-4 text-xs text-muted">
      {{ state.online && typeof state.age === 'number' ? `Data terakhir ${humanAge(state.age)} lalu` : '' }}
    </p>
  </div>
</template>
