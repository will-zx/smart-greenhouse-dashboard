<script setup>
import { ref } from 'vue';
import { state, submitLogin } from '../store.js';

const email = ref('');
const pass = ref('');

async function submit() {
  try {
    await submitLogin(email.value.trim(), pass.value);
    email.value = '';
    pass.value = '';
  } catch { /* pesan error sudah ditulis ke state.loginError */ }
}
</script>

<template>
  <!-- Modal login. Tidak pernah tampil otomatis: dashboard terbuka untuk siapa
       pun, login hanya dibutuhkan untuk kontrol & manajemen greenhouse. -->
  <div
    v-if="state.loginOpen"
    class="fixed inset-0 z-[60] flex items-center justify-center bg-slate-900/45"
    @click.self="state.loginOpen = false"
  >
    <div class="relative w-[360px] max-w-[90%] rounded-xl border border-border bg-card p-7 shadow-lg">
      <button
        type="button"
        class="absolute right-2.5 top-2.5 grid h-[30px] w-[30px] place-items-center rounded-lg bg-none text-muted hover:bg-card-2 hover:text-ink"
        title="Batal" aria-label="Batal"
        @click="state.loginOpen = false; state.loginError = ''"
      >
        <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M18 6 6 18M6 6l12 12"/></svg>
      </button>

      <h2 class="mb-1 text-lg font-bold text-ink">Masuk sebagai admin</h2>
      <p class="mb-5 text-[13px] text-muted">Dashboard dapat dilihat tanpa login. Kontrol aktuator dan pengelolaan greenhouse memerlukan akun admin.</p>

      <form @submit.prevent="submit">
        <label for="loginEmail" class="mb-1 mt-3 block text-xs font-semibold text-muted">Email</label>
        <input id="loginEmail" v-model="email" type="email" required autocomplete="username"
               class="w-full rounded-lg border border-border bg-white px-3 py-2.5 text-sm text-ink outline-none transition-colors focus:border-accent focus:shadow-[0_0_0_2px_rgba(21,101,192,.14)]">

        <label for="loginPass" class="mb-1 mt-3 block text-xs font-semibold text-muted">Password</label>
        <input id="loginPass" v-model="pass" type="password" required autocomplete="currentpassword"
               class="w-full rounded-lg border border-border bg-white px-3 py-2.5 text-sm text-ink outline-none transition-colors focus:border-accent focus:shadow-[0_0_0_2px_rgba(21,101,192,.14)]">

        <button type="submit" class="mt-5 w-full rounded-lg bg-accent py-2.5 text-sm font-semibold text-white hover:bg-accent-dark">Masuk</button>
      </form>

      <p class="mt-3 min-h-4 text-xs text-danger">{{ state.loginError }}</p>
    </div>
  </div>
</template>
