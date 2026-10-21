<script setup>
import { ref, nextTick, computed } from 'vue';
import { state, ZONES, switchZone, saveRename, zoneName, humanAge } from '../store.js';

const statusText = computed(() => state.age === null
  ? 'Jam perangkat belum sinkron'
  : `Data terakhir masuk ${humanAge(state.age)} lalu`);
// dipakai di aria-label baris zona aktif

// ZONES WAJIB di-import dari store. Bare `ZONES` di template selalu undefined:
// config.js dideklarasi dengan `const` (global lexical, bukan properti
// globalThis), sedangkan template Vue dikompilasi jadi _ctx.ZONES.

const editing = ref(-1);
const draft = ref('');
const inputEl = ref(null);

async function startRename(i, row) {
  editing.value = i;
  draft.value = state.labels[ZONES[i].id] || ZONES[i].name;
  await nextTick();
  inputEl.value?.focus();
  inputEl.value?.select();
}

async function finish(save) {
  const i = editing.value;
  if (i < 0) return;
  editing.value = -1;
  const val = draft.value.trim();
  if (save && val) await saveRename(i, val);
}
</script>

<template>
  <div class="absolute right-0 top-[calc(100%+6px)] z-50 w-[290px] max-w-[calc(100vw-24px)] rounded-xl border border-border bg-card p-1.5 shadow-lg">
    <p class="px-2.5 py-1.5 text-[11px] font-semibold uppercase tracking-wide text-muted">Pilih greenhouse</p>

    <div v-for="(z, i) in ZONES" :key="z.id">
      <div
        class="flex items-center rounded-lg transition-colors"
        :class="i === state.currentZone ? 'bg-accent-soft text-accent-dark' : 'hover:bg-card-2'"
      >
        <!-- Nama + id unit. mode edit: input inline (prompt() diblokir di
             iframe sehingga tombolnya jadi tampak tidak berrespons). -->
        <template v-if="editing === i">
          <input
            ref="inputEl"
            v-model="draft"
            class="m-1.5 min-w-0 flex-1 rounded-md border border-border bg-white px-2 py-1 text-[13px] text-ink outline-none focus:border-accent"
            :aria-label="`Nama greenhouse ${z.id}`"
            @keydown.enter.prevent="finish(true)"
            @keydown.esc.prevent="finish(false)"
            @blur="finish(true)"
          >
        </template>
        <button
          v-else
          type="button"
          class="flex min-w-0 flex-1 items-center gap-2 px-2.5 py-2 text-left"
          :aria-label="i === state.currentZone ? `${state.labels[z.id] || z.name}, ${statusText}` : undefined"
          @click="switchZone(i); $emit('close')"
        >
          <span class="flex min-w-0 flex-col">
            <span class="flex min-w-0 items-baseline gap-1.5">
              <span class="truncate text-[13px] font-semibold">{{ state.labels[z.id] || z.name }}</span>
              <!-- Umur data hanya diketahui untuk zona yang SEDANG DIPANTAU
                   (hanya zona itu yang di-fetch), jadi ditampilkan di samping
                   nama unit aktif saja. Menempelkannya di semua unit akan
                   menyesatkan. Ini juga journeysscreen sentuh: Unlike `title`,
                   teks ini selalu terbaca tanpa hover. -->
              <span v-if="i === state.currentZone" class="shrink-0 text-[10.5px] font-normal text-muted">
                {{ !state.online ? '· gagal baca' : (state.age === null ? '· jam belum sinkron' : '· ' + humanAge(state.age) + ' lalu') }}
              </span>
            </span>
            <span class="font-mono text-[10.5px] text-muted">{{ z.id }}</span>
          </span>
          <svg v-if="i === state.currentZone" class="z-check ml-auto shrink-0 text-accent" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>
        </button>

        <!-- Ikon pensil hanya untuk admin (RTDB rules yang sesungguhnya
             menolak penulisan non-admin; ini kejelasan saja). -->
        <button
          v-if="state.isAdmin && editing !== i"
          type="button"
          class="mr-1 shrink-0 rounded-md p-1.5 text-muted hover:bg-accent/10 hover:text-accent-dark"
          title="Ganti nama greenhouse"
          :aria-label="`Ganti nama greenhouse ${z.id}`"
          @click="startRename(i)"
        >
          <svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/></svg>
        </button>
      </div>
    </div>

    <p v-if="state.zoneMsg.show" class="mx-2 mt-1 text-[11.5px]" :class="state.zoneMsg.ok ? 'text-ok' : 'text-danger'">
      {{ state.zoneMsg.text }}
    </p>

    <p class="mt-1.5 border-t border-border px-2.5 pt-2 text-[11px] text-muted">
      Satu unit ESP32 per greenhouse. Data yang dipantau dan dikontrol mengikuti pilihan di atas.
    </p>
  </div>
</template>
