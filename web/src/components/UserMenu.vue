<script setup>
import { state, signOut } from '../store.js';

// Ikon incognito untuk tamu (topi + kacamata).
const INCOGNITO = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M2.6 13.2c4.6-1.5 14.2-1.5 18.8 0"/><path d="M7.8 13 9.4 7.6h5.2L16.2 13"/><path d="M5.2 16.6 5 15.2"/><path d="M18.8 16.6 19 15.2"/><circle cx="7.4" cy="18.2" r="2.7"/><circle cx="16.6" cy="18.2" r="2.7"/><path d="M10.1 18.2h3.8"/></svg>';

const initial = u => u.charAt(0).toUpperCase();
const local = u => u.split('@')[0];
</script>

<template>
  <div class="relative">
    <button
      type="button"
      class="flex items-center gap-2.5 rounded-lg py-1 pl-1 pr-2 text-white transition-opacity hover:opacity-85"
      :aria-expanded="state.userMenuOpen"
      @click="state.userMenuOpen = !state.userMenuOpen"
    >
      <span
        class="grid h-8 w-8 shrink-0 place-items-center overflow-hidden rounded-full bg-accent-soft text-[13px] font-bold text-accent-dark [&>svg]:h-[19px] [&>svg]:w-[19px]"
        v-html="state.user ? initial(state.user) : INCOGNITO"
      />
      <span class="flex flex-col leading-tight max-[700px]:hidden">
        <b class="text-[12px] font-bold">{{ state.user ? local(state.user) : 'Tamu' }}</b>
        <small class="text-[10.5px] text-white/75">{{ state.isAdmin ? 'Admin' : (state.user ? 'User' : 'Hanya lihat') }}</small>
      </span>
    </button>

    <div
      v-if="state.userMenuOpen"
      class="absolute right-0 top-[calc(100%+6px)] z-50 min-w-[200px] overflow-hidden rounded-xl border border-border bg-card shadow-lg"
    >
      <div class="flex flex-wrap items-center justify-between gap-1.5 border-b border-border bg-card-2 px-3.5 py-3">
        <span class="min-w-0 truncate text-[12.5px] font-semibold text-ink">{{ state.user || 'Tamu' }}</span>
        <span
          class="shrink-0 rounded-full px-2 py-0.5 text-[10.5px] font-bold uppercase tracking-wide"
          :class="state.isAdmin ? 'border border-accent bg-accent-soft text-accent' : 'border border-border text-muted'"
        >{{ state.isAdmin ? 'Admin' : (state.user ? 'User' : 'Hanya lihat') }}</span>
      </div>

      <!-- Tamu melihat "Login", admin melihat "Keluar". -->
      <button
        v-if="!state.user"
        type="button"
        class="flex w-full items-center gap-2.5 px-3.5 py-3 text-left text-[13px] font-semibold text-danger hover:bg-danger-soft"
        @click="state.userMenuOpen = false; state.loginOpen = true"
      >
        <svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"/><path d="M10 17l5-5-5-5"/><path d="M15 12H3"/></svg>
        Login
      </button>
      <button
        v-else
        type="button"
        class="flex w-full items-center gap-2.5 px-3.5 py-3 text-left text-[13px] font-semibold text-danger hover:bg-danger-soft"
        @click="signOut()"
      >
        <svg viewBox="0 0 24 24" width="15" height="15" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><path d="M16 17l5-5-5-5"/><path d="M21 12H9"/></svg>
        Keluar
      </button>
    </div>
  </div>
</template>
