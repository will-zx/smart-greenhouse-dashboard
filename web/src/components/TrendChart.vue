<script setup>
import { ref, onMounted, onUnmounted, watch } from 'vue';
import { Chart, LineController, LineElement, PointElement, LinearScale, CategoryScale, Legend, Tooltip } from 'chart.js';
import { state, fmtClock, humanAge, TREND_RANGE, setTrendRange, trendData, zoneName, refreshWithSpin } from '../store.js';

// Registrasi manual (bukan chart.js/auto) supaya bundel hanya membawa yang dipakai.
Chart.register(LineController, LineElement, PointElement, LinearScale, CategoryScale, Legend, Tooltip);

// Grafik membaca palet yang sama dengan Tailwind, jadi tidak ada warna yang
// "melenceng" dari skema dashboard.
const cssVar = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const C = {
  danger: cssVar('--color-danger'), accent: cssVar('--color-accent'),
  warn: cssVar('--color-warn'), ok: cssVar('--color-ok'),
  muted: cssVar('--color-muted'), card2: cssVar('--color-card-2'),
};

function line(label, color, yAxisID, extra = {}) {
  return {
    label, yAxisID, data: [],
    borderColor: color, pointBackgroundColor: color,
    pointRadius: 3, pointHoverRadius: 5, borderWidth: 2, tension: 0,
    ...extra,
  };
}

// Satu kartu grafik: suhu & RH di sumbu masing-masing, VPD di sumbu kanan
// kedua (Chart.js menumpuk dua sumbu berposisi 'right').
// Target VPD sengaja TIDAK digambar: nilainya sudah terbaca di kartu Fase &
// Setpoint VPD (PhasePanel), dan garis kedua dengan warna sama hanya membuat
// VPD hasil ukur lebih sulit dibaca.
const datasets = [
  line('Suhu (°C)', C.danger, 'y'),
  line('Kelembapan (%)', C.accent, 'y1'),
  line('VPD (kPa)', C.ok, 'y2'),
];
// Field di trendData yang mengisi tiap dataset, urut sesuai datasets di atas.
const KEYS = ['temperature', 'humidity', 'vpd'];

// Di layar sempit judul sumbu kanan (dua-duanya) dihilangkan supaya area grafik
// tidak habis untuk teks; tick angkanya tetap tampil. Ambang sama dengan tinggi
// kartu (max-[700px]) sehingga satu breakpoint saja yang perlu diingat.
const NARROW = 700;
let narrow = false;

// suggestedMin/Max, bukan min/max: rentang tetap supaya kemiringan garis jujur.
// Tanpa ini Chart.js auto-zoom dan 0.5°C terlihat seperti lonjakan besar.
const scales = {
  x: {
    grid: { color: C.card2 },
    ticks: { color: C.muted, maxTicksLimit: 8, maxRotation: 0, autoSkipPadding: 12 }
  },
  y: {
    type: 'linear', position: 'left', suggestedMin: 20, suggestedMax: 40,
    grid: { color: C.card2 }, border: { display: false },
    ticks: { color: C.danger, maxTicksLimit: 6 },
    title: { display: true, text: 'Suhu (°C)', color: C.danger, font: { weight: 600 } }
  },
  y1: {
    type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 100,
    grid: { drawOnChartArea: false }, border: { display: false },
    ticks: { color: C.accent, maxTicksLimit: 6 },
    title: { display: true, text: 'Kelembapan (%)', color: C.accent, font: { weight: 600 } }
  },
  y2: {
    type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 4,
    grid: { drawOnChartArea: false }, border: { display: false },
    ticks: { color: C.ok, maxTicksLimit: 6 },
    title: { display: true, text: 'VPD (kPa)', color: C.ok, font: { weight: 600 } }
  },
};

const el = ref(null);
let chart;

// Sumbu X sudah menunjuk waktunya, jadi cukup "nilai + satuan" - tidak perlu
// mengulang label. Satuan ikut sumbu tempat seri itu digambar.
const UNIT = { y: '°C', y1: '%', y2: 'kPa' };
const label = c =>
  ' ' + c.dataset.label.replace(/\s*\(.*\)/, '') + ': ' +
  c.parsed.y.toFixed(2) + ' ' + (UNIT[c.dataset.yAxisID] || '');

// Judul sumbu kanan hanya berguna kalau ada ruang; di ponsel dua judul itu
// memakan ~1/3 lebar grafik dan mendorong sisa ruang mati di kiri.
function applyNarrow() {
  if (!chart) return;
  const show = !narrow;
  chart.options.scales.y1.title.display = show;
  chart.options.scales.y2.title.display = show;
  chart.update('none');
}
function onResize() {
  const nowNarrow = window.innerWidth < NARROW;
  if (nowNarrow === narrow) return;
  narrow = nowNarrow;
  applyNarrow();
}

onMounted(() => {
  narrow = window.innerWidth < NARROW;
  chart = new Chart(el.value, {
    type: 'line',
    data: { labels: [], datasets },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false },
      // Legenda di bawah-tengah seperti grafik referensi: lebih rapi dan
      // tidak memakan ruang di dekat judul kartu.
      plugins: {
        legend: {
          position: 'bottom',
          labels: { usePointStyle: true, pointStyle: 'circle', boxWidth: 7, boxHeight: 7, padding: 18, font: { weight: 600 } }
        },
        tooltip: {
          padding: 10, boxWidth: 8, boxHeight: 8, usePointStyle: true,
          callbacks: { label }
        }
      },
      scales,
    }
  });
  applyNarrow();
  sync();
  window.addEventListener('resize', onResize);
});

// Data berubah tiap polling (2 mnt / 15 mnt) dan saat ganti rentang -> grafik
// ikut tanpa render ulang manual, karena trendData computed dari store.
watch(trendData, sync);

function sync() {
  if (!chart) return;
  // Sumbu X: jam saja — label penuh tidak muat. Record tanpa ts (jam perangkat
  // belum sinkron) tetap dapat label "?".
  chart.data.labels = trendData.value.map(l => fmtClock(l.ts));
  // Field yang tidak ada (record lama / sensor invalid) -> null, bukan 0:
  // Chart.js menggambar celah, jadi tidak ada titik 0 palsu di grafik.
  chart.data.datasets.forEach((ds, i) => { ds.data = trendData.value.map(l => l[KEYS[i]]); });
  chart.update();
}

onUnmounted(() => {
  window.removeEventListener('resize', onResize);
  chart?.destroy();
});
</script>

<template>
  <!-- Tanpa mt-5: jarak antar kartu sudah diberikan oleh gap-5 di section
       App.vue. mt-5 pernah ada saat grafik menempel langsung di baris metrik;
       setelah Panel Fase disisipkan, margin itu menumpuk dan jadi jarak
       dobel hanya di kartu ini. -->
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v16a2 2 0 0 0 2 2h16"/><path d="M7 14l4-4 4 4 5-6"/></svg>
        Grafik Iklim
        <span class="font-medium text-muted">{{ zoneName() }}</span>
      </h2>
      <div class="flex items-center gap-2">
        <!-- Rentang grafik sekaligus jendela fetch & interval poll. -->
        <div class="flex items-center rounded-lg border border-border bg-card p-0.5" role="group" aria-label="Rentang waktu grafik">
          <button v-for="(r, k) in TREND_RANGE" :key="k" type="button"
                  class="rounded-md px-2.5 py-1 text-xs font-semibold transition-colors"
                  :class="state.trendRange === k ? 'bg-accent text-white' : 'text-muted hover:bg-card-2'"
                  @click="setTrendRange(k)">{{ r.label }}</button>
        </div>
        <button type="button" class="inline-flex items-center gap-1.5 rounded-lg border border-border bg-card px-3 py-1.5 text-xs font-semibold text-ink hover:bg-card-2" title="Muat ulang data sensor" @click="refreshWithSpin()">
          <svg class="text-accent" :class="state.spinning && 'animate-spin'" viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 3v6h-6"/></svg>
          Refresh
        </button>
      </div>
    </div>

    <div class="relative h-[320px] max-[700px]:h-[240px]">
      <canvas ref="el" />
    </div>

    <!-- Umur data di bawah grafik, bukan di header: pengguna bisa membedakan
         "sensor diam 40 detik" dari "dashboardnya yang macet". -->
    <p class="mt-3 min-h-4 text-xs text-muted">
      <template v-if="state.online">
        <template v-if="state.age === null">Jam perangkat belum sinkron — record terakhir tidak punya field ts</template>
        <template v-else>Data terakhir {{ humanAge(state.age) }} lalu</template>
      </template>
    </p>
  </div>
</template>
