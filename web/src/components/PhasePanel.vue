<script setup>
import { ref, computed, watch } from 'vue';
import {
  state, setVpdTarget, setPhaseName, setPhaseDuration, resetPhaseCounter,
  VPD_TARGET_MIN, VPD_TARGET_MAX,
} from '../store.js';

// Nilai yang TERPASANG = yang dipush firmware (phase.vpd_target, phase.name,
// phase.duration, ...). Nilai yang DIMINTA = isi form. Bedanya penting:
// perangkat bisa menolak (mis. target di luar rentang) atau belum merespons.
const phase = computed(() => state.lastData?.phase || null);

const vpdInput = ref('');
const nameInput = ref('');
const durInput = ref('');
const durUnit = ref('hari');

watch(() => state.lastData?._key, () => {
  const p = state.lastData?.phase;
  if (!p) return;
  // Isi form mengikuti nilai terpasang, TAPI hanya kalau pemakai belum
  // mengetik sesuatu (jangan menimpa input yang sedang diisi).
  if (vpdInput.value === '' && p.target !== null) vpdInput.value = String(p.target);
  if (nameInput.value === '' && p.name) nameInput.value = p.name;
});

// Durasi dalam detik -> bahasa sehari-hari.
function fmtDur(s) {
  if (s === null || s === undefined || !isFinite(s)) return '-';
  if (s <= 0) return 'tanpa batas';
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  const parts = [];
  if (d) parts.push(d + ' hari');
  if (h) parts.push(h + ' jam');
  if (m && !d) parts.push(m + ' menit');
  if (sec && !d && !h && !m) parts.push(sec + ' detik');
  return parts.length ? parts.join(' ') : '0 detik';
}

const inputCls = 'w-full rounded-lg border border-border bg-white px-2.5 py-1.5 text-[12.5px] text-ink outline-none focus:border-accent focus:shadow-[0_0_0_2px_rgba(21,101,192,.14)]';
const btnCls = 'inline-flex items-center justify-center gap-1.5 rounded-lg border border-border bg-card px-3 py-1.5 text-[12px] font-semibold text-ink hover:bg-card-2 disabled:opacity-50';
</script>

<template>
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3v18M3 12h18"/><circle cx="12" cy="12" r="9"/></svg>
        Fase &amp; Setpoint VPD
      </h2>
      <span class="text-xs text-muted">{{ state.isAdmin ? 'Setpoint mengoreksi fuzzy, bukan menggantikannya' : 'Hanya lihat — login admin untuk mengubah' }}</span>
    </div>

    <!-- Nilai TERPASANG (dari perangkat) -->
    <div class="mb-4 grid grid-cols-2 gap-2.5 min-[700px]:grid-cols-4">
      <div class="rounded-lg border border-border bg-card-2 px-3 py-2">
        <div class="text-[10.5px] font-semibold uppercase tracking-wide text-muted">Fase aktif</div>
        <div class="mt-0.5 text-[13.5px] font-bold text-ink">{{ phase?.name || '-' }}</div>
        <div class="text-[10.5px] text-muted">{{ phase?.active ? 'setpoint aktif' : 'nonaktif (fuzzy penuh)' }}</div>
      </div>
      <div class="rounded-lg border border-border bg-card-2 px-3 py-2">
        <div class="text-[10.5px] font-semibold uppercase tracking-wide text-muted">Target VPD terpasang</div>
        <div class="mt-0.5 text-[13.5px] font-bold text-ink">{{ phase?.target !== null && phase?.target !== undefined ? phase.target.toFixed(2) + ' kPa' : '-' }}</div>
        <div class="text-[10.5px] text-muted">rentang {{ VPD_TARGET_MIN }}–{{ VPD_TARGET_MAX }} kPa</div>
      </div>
      <div class="rounded-lg border border-border bg-card-2 px-3 py-2">
        <div class="text-[10.5px] font-semibold uppercase tracking-wide text-muted">Durasi terpasang</div>
        <div class="mt-0.5 text-[13.5px] font-bold text-ink">{{ fmtDur(phase?.duration) }}</div>
        <div class="text-[10.5px] text-muted">sisa {{ fmtDur(phase?.remaining) }}</div>
      </div>
      <div class="rounded-lg border border-border bg-card-2 px-3 py-2">
        <div class="text-[10.5px] font-semibold uppercase tracking-wide text-muted">Waktu fase berjalan</div>
        <div class="mt-0.5 text-[13.5px] font-bold text-ink">{{ fmtDur(phase?.elapsed) }}</div>
        <div class="text-[10.5px] text-muted">sejak durasi terakhir dipasang</div>
      </div>
    </div>

    <!-- Form: admin saja -->
    <div v-if="state.isAdmin" class="grid gap-3 min-[700px]:grid-cols-3">
      <div>
        <label class="mb-1 block text-[11.5px] font-semibold text-muted" for="vpdTarget">Target VPD (kPa)</label>
        <div class="flex gap-2">
          <input id="vpdTarget" v-model="vpdInput" type="number" :min="VPD_TARGET_MIN" :max="VPD_TARGET_MAX" step="0.1"
                 class="w-full min-w-0 rounded-lg border border-border bg-white px-2.5 py-1.5 text-[12.5px] text-ink outline-none focus:border-accent"
                 :placeholder="phase?.target != null ? String(phase.target) : '1.0'">
          <button type="button" class="shrink-0 rounded-lg bg-accent px-3 py-1.5 text-[12px] font-semibold text-white hover:bg-accent-dark" @click="setVpdTarget(vpdInput)">Terapkan</button>
        </div>
      </div>

      <div>
        <label class="mb-1 block text-[11.5px] font-semibold text-muted" for="faseName">Nama fase</label>
        <div class="flex gap-2">
          <input id="faseName" v-model="nameInput" type="text" maxlength="23" class="w-full min-w-0 rounded-lg border border-border bg-white px-2.5 py-1.5 text-[12.5px] text-ink outline-none focus:border-accent"
                 :placeholder="phase?.name || 'Vegetatif'">
          <button type="button" class="shrink-0 rounded-lg bg-accent px-3 py-1.5 text-[12px] font-semibold text-white hover:bg-accent-dark" @click="setPhaseName(nameInput)">Terapkan</button>
        </div>
      </div>

      <div>
        <label class="mb-1 block text-[11.5px] font-semibold text-muted" for="faseDurasi">Durasi fase</label>
        <div class="flex gap-2">
          <input id="faseDurasi" v-model="durInput" type="number" min="0" step="1" class="w-full min-w-0 rounded-lg border border-border bg-white px-2.5 py-1.5 text-[12.5px] text-ink outline-none focus:border-accent" placeholder="14">
          <select v-model="durUnit" class="shrink-0 rounded-lg border border-border bg-white px-2 py-1.5 text-[12.5px] text-ink outline-none focus:border-accent">
            <option value="detik">detik</option>
            <option value="menit">menit</option>
            <option value="jam">jam</option>
            <option value="hari">hari</option>
          </select>
          <button type="button" class="shrink-0 rounded-lg bg-accent px-3 py-1.5 text-[12px] font-semibold text-white hover:bg-accent-dark" @click="setPhaseDuration(durInput, durUnit)">Terapkan</button>
        </div>
      </div>
    </div>

    <!-- Aksi fase -->
    <div v-if="state.isAdmin" class="mt-3 flex flex-wrap gap-2">
      <button type="button" :class="btnCls" title="Mulai ulang penghitung durasi fase dari nol" @click="resetPhaseCounter()">Reset penghitung</button>
      <button type="button" :class="btnCls" title="Matikan mode setpoint/fase — keputusan kembali sepenuhnya ke fuzzy" @click="setPhaseName('')">Nonaktifkan setpoint</button>
    </div>

    <p class="mt-3 min-h-[14px] text-[11px] leading-snug text-muted">
      Setpoint VPD mengoreksi keluaran fuzzy: VPD di atas target → misting didorong, VPD di bawah target → blower
      didorong proporsional. Di dalam pita target, fuzzy yang menentukan.
    </p>
    <!-- Status perintah: "terkirim" != "dieksekusi". Konfirmasi diambil dari
         nilai terpasang yang dipush firmware, bukan dari balasan HTTP. -->
    <p v-if="state.cmdStatus" class="mt-1 min-h-[14px] text-[11px] font-semibold text-muted">{{ state.cmdStatus }}</p>
    <p v-if="state.cmdError" class="mt-1 min-h-[14px] text-[11px] font-semibold text-danger">{{ state.cmdError }}</p>
  </div>
</template>
