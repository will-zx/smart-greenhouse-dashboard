<script setup>
import { onMounted, onUnmounted } from 'vue';
import { state, initAuth, startPolling, stopPolling } from '../store.js';
import ZoneStatus from './ZoneStatus.vue';
import ZoneMenu from './ZoneMenu.vue';
import UserMenu from './UserMenu.vue';
import LoginModal from './LoginModal.vue';
import Metrics from './Metrics.vue';
import Controls from './Controls.vue';
import PhasePanel from './PhasePanel.vue';
import TrendChart from './TrendChart.vue';
import LogTable from './LogTable.vue';
import SoilCard from './SoilCard.vue';

onMounted(() => {
  initAuth();
  startPolling();
});

onUnmounted(stopPolling);

// Klik di luar popup menutupnya; Escape menutup semuanya.
function onDocClick() {
  state.userMenuOpen = false;
  state.zoneMenuOpen = false;
}
function onKey(e) {
  if (e.key !== 'Escape') return;
  state.loginOpen = false;
  state.userMenuOpen = false;
  state.zoneMenuOpen = false;
}
onMounted(() => {
  document.addEventListener('click', onDocClick);
  document.addEventListener('keydown', onKey);
});
onUnmounted(() => {
  document.removeEventListener('click', onDocClick);
  document.removeEventListener('keydown', onKey);
});

const toggleView = () => { state.view = state.view === 'log' ? 'beranda' : 'log'; };
</script>

<template>
  <div class="flex min-h-screen flex-col bg-bg">
    <!-- Topbar -->
    <header class="sticky top-0 z-40 flex h-16 items-center gap-5 bg-accent px-6 text-white max-[700px]:h-[58px] max-[700px]:gap-2.5 max-[700px]:px-3.5">
      <div class="flex shrink-0 items-center gap-2.5 max-[700px]:gap-2">
        <!-- greenhouse.png dipakai sebagai CSS mask supaya warnanya ikut
            currentColor (putih di topbar biru), sama seperti .brand-icon lama
             di style.css:43-51. -->
        <span class="brand-icon h-7 w-7 shrink-0 max-[700px]:h-6 max-[700px]:w-6" aria-hidden="true" />
        <span class="text-sm font-extrabold leading-[1.15] tracking-[1px] max-[700px]:text-[11px] max-[700px]:tracking-[.8px]">SMART<br>GREENHOUSE</span>
      </div>

      <!-- min-w-0 wajib: tanpa itu pill unit + tombol Log + avatar tidak mau
           menyusut, sehingga topbar melebar 7px di layar 360px dan halaman
           bisa digeser horizontal. -->
      <div class="ml-auto flex min-w-0 items-center gap-3 max-[700px]:gap-2">
        <!-- Satu-satunya navigasi: toggle Beranda <-> Log Sensor. -->
        <button
          type="button"
          class="flex items-center gap-1.5 rounded-lg px-3 py-1.5 text-[13px] font-semibold transition-colors max-[700px]:px-2.5"
          :class="state.view === 'log' ? 'bg-white text-accent-dark' : 'text-white hover:bg-white/10'"
          :aria-expanded="state.view === 'log'"
          aria-controls="view-log"
          @click="toggleView"
        >
          <svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3 5h18M3 10h18M3 15h18M3 20h10"/><circle cx="19" cy="15" r="1.6"/><circle cx="16" cy="20" r="1.6"/></svg>
          <span class="max-[700px]:hidden">{{ state.view === 'log' ? 'Beranda' : 'Log Sensor' }}</span>
        </button>

        <!-- `flex` WAJIB, bukan hanya `min-w-0`: sebagai wrapper display:block,
             tombol pill (display:flex) memakai width:auto dan TIDAK bisa menyusut
             melebihi lebar induknya — teksnya lalu meluber keluar dan menabrak
             avatar. Menjadikan wrapper flex membuat tombol jadi flex item yang
             bisa menyusut, sehingga `truncate` di dalamnya benar-benar bekerja. -->
        <div class="relative flex min-w-0" @click.stop>
          <ZoneStatus @toggle="state.zoneMenuOpen = !state.zoneMenuOpen" />
          <ZoneMenu v-if="state.zoneMenuOpen" @close="state.zoneMenuOpen = false" />
        </div>

        <div class="shrink-0" @click.stop>
          <UserMenu />
        </div>
      </div>
    </header>

    <main class="flex-1 p-[22px] max-[900px]:px-3.5 max-[900px]:py-4">
      <!-- ============ BERANDA ============ -->
      <section v-show="state.view === 'beranda'" class="flex flex-col gap-5">
        <!-- Kolom kontrol PROPORSIONAL (46%), bukan lebar tetap: supaya kartu
             metrik tidak melebar berlebihan di jendela besar dan kedua kolom
             berakhir di tinggi yang sama. align-items sengaja default (stretch)
             — pinggiran bawah kedua kolom harus rata. Sama seperti .home-top
             di style.css lama:310-321. -->
        <div id="view-beranda" class="grid gap-5 min-[900px]:grid-cols-[minmax(0,1fr)_minmax(300px,46%)]">
          <Metrics />
          <Controls />
        </div>
        <PhasePanel />
        <SoilCard />
        <TrendChart />
        <!-- Grafik Air: komponen yang sama, dataset berbeda (mode="air"). -->
        <TrendChart mode="air" />
      </section>

      <!-- ============ LOG SENSOR ============ -->
      <section id="view-log" v-show="state.view === 'log'">
        <LogTable />
      </section>
    </main>

    <LoginModal />
  </div>
</template>
