import { reactive, computed } from 'vue';

// config.js (public/) dimuat sebagai script global di index.html, jadi di sini
// ZONES / DB_URL / ADMIN_EMAIL / firebaseConfig dibaca sebagai bare identifier.
//
// PENTING: `const` di top-level script klasik masuk global LEXICAL environment,
// bukan sebagai properti globalThis. Modul (file .js) tetap resolve bare
// identifier, tapi ekspresi template Vue dikompilasi jadi _ctx.ZONES yang hanya
// mencari properti di globalThis -> undefined, sehingga `v-for` di template
// mengiterasi undefined dan popup unit terender kosong. Karena itu ZONES
// diekspor di sini dan komponen WAJIB meng-import, bukan memakai bare
// `ZONES` di dalam template.
const _zones = ZONES;
export { _zones as ZONES };

// -------------------------------------------------- rentang waktu grafik ---
// Jendela waktu dua kartu grafik (iklim & air). limit = record yang di-fetch
// dari RTDB (perangkat push tiap ~20 s), bucket = lebar grup down-sample
// (detik), pollMs = interval polling. Rentang panjang dipoll lebih jarang:
// 24 jam = 4320 record ~2.6 MB sekali fetch, jadi 15 menit sekali — ditambah
// polling yang berhenti saat tab tidak terlihat, kuota Spark tetap aman.
export const TREND_RANGE = {
  '6j': { limit: 1080, bucket: 300, pollMs: 120000, label: '6 jam' },
  '24j': { limit: 4320, bucket: 600, pollMs: 900000, label: '24 jam' },
};

export const state = reactive({
  // identitas & peran
  user: null,
  isAdmin: false,

  // unit greenhouse
  currentZone: 0,
  labels: {},

  // data sensor — baris sudah dinormalisasi (lihat normLog)
  logData: [],
  lastData: null,
  logFilter: '',
  // Jendela fetch grafik = lebar rentang terpanjang (24 jam: 4320 record).
  // Jumlah entri yang DITAMPILKAN tabel log dibedakan lewat logRows di bawah.
  logLimit: TREND_RANGE['24j'].limit,
  // Rentang waktu grafik — dua kartu grafik (iklim & air) memakai state ini.
  trendRange: '24j',
  // Batas baris tabel log yang DITAMPILKAN (sisi klien, masih di dalam
  // jendela fetch), bukan lagi pemicu fetch — select "Show entries" tidak
  // perlu fetch ulang karena datanya sudah di memori.
  logRows: 20,

  // tampilan
  view: 'beranda',
  loginOpen: false,
  loginError: '',
  userMenuOpen: false,
  zoneMenuOpen: false,
  zoneMsg: { text: '', ok: false, show: false },
  cmdStatus: '',
  cmdError: '',
  // perintah yang sudah terkirim tapi belum dikonfirmasi perangkat
  pending: null,
  spinning: false,

  // koneksi
  online: false,
  // umur data (detik). null = record terakhir tidak punya field ts, jadi
  // jam perangkat belum sinkron — BUKAN "NaN menit" dan bukan pill hijau.
  age: null,
  loadError: '',

  // rename inline
  renaming: -1,
});

// ---------------------------------------------------------------- format ---
// Angka sensor dibaca ADC 12-bit lalu jadi float: 1.421642 kPa / 5.4894 mm tidak
// pernah berarti apa-apa bagi pengguna. 2 desimal cukup, TANAH/TDS dibulatkan
// karena hanya dipakai sebagai persentase/ppm bulat.
export const fmtNum = (v, d = 2) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(d) : '-';

// Volume air: mL -> liter (>= 1 L), di bawah itu tetap mL. Null = tidak ada
// data (firmware lama / device belum push) -> pemanggil tidak menampilkan apa-apa.
export const fmtMl = v => {
  if (typeof v !== 'number' || !isFinite(v)) return null;
  return v >= 1000 ? (v / 1000).toFixed(1) + ' L' : Math.round(v) + ' mL';
};

// Soil moisture = 0..100%. Sensor capacitive yang belum terpasang (pin
// menggantung) dibaca ADC tinggi -> ~100% palsu. Nilai di luar 5..95% tidak
// ditampilkan sebagai angka.
export const saneSoil = v => (typeof v === 'number' && v > 5 && v < 95) ? v : null;

export function fmtClock(ts) {
  if (typeof ts !== 'number' || ts < 100000) return '?';
  // Detik ikut karena ESP32 push tiap ~20 s: tanpa ini dua titik berdekatan
  // sering jatuh di menit yang sama dan label sumbu X terulang.
  return new Date(ts * 1000).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit' }).replace(/\./g, ':');
}

export function fmtLogTime(ts) {
  if (typeof ts !== 'number' || ts < 100000) return '-';
  return new Date(ts * 1000).toLocaleString('id-ID')
    .replace(/(\d{2})\.(\d{2})\.(\d{2})\s*$/, '$1:$2:$3');
}

// Umur data dalam bahasa sehari-hari: detik → menit → jam → hari, dengan sisa
// unit yang lebih kecil ("2 hari 15 jam"). Satu fungsi supaya label "diam ..."
// di topbar dan "Data terakhir ..." di grafik tidak pernah beda format.
// Floor, bukan round: 1439 menit harus tetap "23 jam 59 menit", bukan "24 jam".
// sec null = tidak bisa dihitung (tidak ada ts) -> "—", BUKAN "0 detik".
export function humanAge(sec) {
  if (sec === null || sec === undefined || !isFinite(sec)) return '—';
  if (sec < 0) return '0 detik';
  if (sec < 60) return sec + " detik";
  const m = Math.floor(sec / 60);
  if (m < 60) return m + " menit";
  const sisaM = m % 60;
  const h = Math.floor(m / 60);
  if (h < 24) return h + (sisaM ? " jam " + sisaM + " menit" : " jam");
  const sisaH = h % 24;
  return Math.floor(h / 24) + " hari" + (sisaH ? " " + sisaH + " jam" : "");
}

// ------------------------------------------------------------ normalisasi ---
// Kontrak data firmware (lihat firmware/src/firebase_db.cpp):
//   ts (epoch, HANYA bila NTP sinkron), time_synced (bool), temperature,
//   humidity, soil, vpd, etc (nilai ETc, dikirim juga sebagai et0), tds,
//   blower_level, misting, irigasi, status_sistem, manual_mode,
//   mist_ov / irig_ov / blower_ov, fase, vpd_target, fase_aktif,
//   fase_durasi_s, fase_elapsed_s, fase_sisa_s, kc_aktif, tmean_c, device.
// Field yang tidak terbaca TIDAK dikirim sama sekali (firmware membuang NaN),
// jadi "tidak ada" != 0 — harus tampil "-", bukan angka nol palsu.
const numOrNull = v => (typeof v === 'number' && isFinite(v)) ? v : null;

// Status override per aktuator:
//   'auto'   -> ikut fuzzy (tidak ada override)
//   'on'     -> paksa nyala
//   'off'    -> paksa mati
//   'unknown'-> ada override tapi perangkat tidak melaporkan aktuator mana
function parseOverride(v) {
  if (v === undefined || v === null) return null;
  if (typeof v === 'boolean') return v ? 'on' : 'off';
  if (typeof v === 'number') {
    if (v > 0) return 'on';
    if (v === 0) return 'off';
    return 'auto';      // -1 = ikut fuzzy
  }
  if (typeof v === 'string') {
    const s = v.trim().toLowerCase();
    // Firmware mengirim string: "auto" (ikut fuzzy), "manual" (paksa nyala),
    // "forced" (paksa mati). Angka & boolean tetap diterima supaya log lama
    // (yang belum punya field ini) tidak salah baca.
    if (['on', '1', 'true', 'manual', 'force', 'paksa'].includes(s)) return 'on';
    if (['off', '0', 'false', 'forced'].includes(s)) return 'off';
    if (['auto', '-1', 'fuzzy'].includes(s)) return 'auto';
  }
  return null;
}

// Override per aktuator.
// Firmware 61d1bad: override manual TRIS-TATA per aktuator (-1 ikut fuzzy,
// 0 paksa mati, 1 paksa nyala), tapi yang dikirim ke RTDB baru flag
// manual_mode + (belum) field per aktuator. Kalau field per aktuator
// (mist_ov / irig_ov / blower_ov) ada, itu yang dipakai.
// Fallback untuk firmware lama: manual_mode = SATU bitmask untuk semua
// aktuator, jadi status aktuator = status override-nya.
// Firmware baru hanya meng-override SATU aktuator → tanpa field per aktuator
// kita tidak bisa menebak yang mana, jadi 'unknown' (jujur, bukan menebak).
function overrideState(l, field, running) {
  const st = parseOverride(l[field]);
  if (st) return st;
  if (l.manual_mode !== true) return 'auto';
  const firmwareBaru = 'time_synced' in l || 'fase_aktif' in l || 'blower_level' in l;
  if (firmwareBaru) return 'unknown';
  return running ? 'on' : 'off';
}

export function normLog(l, key) {
  const src = l || {};
  const status = typeof src.status_sistem === 'string' ? src.status_sistem : '';
  const hasTs = typeof src.ts === 'number' && src.ts >= 100000;
  // time_synced hanya ada di firmware baru; di firmware lama absen = dianggap
  // sinkron bila ts ada.
  const synced = hasTs && src.time_synced !== false;
  const level = numOrNull(src.blower_level);
  const etc = numOrNull(src.etc !== undefined && src.etc !== null ? src.etc : src.et0);

  const row = {
    _key: key,
    ts: hasTs ? src.ts : null,
    synced,
    device: typeof src.device === 'string' ? src.device : '',
    temperature: numOrNull(src.temperature),
    humidity: numOrNull(src.humidity),
    soil: numOrNull(src.soil),
    soil2: numOrNull(src.soil2),
    soil3: numOrNull(src.soil3),
    vpd: numOrNull(src.vpd),
    etc,
    tds: numOrNull(src.tds),
    blowerLevel: level,
    // Status aktuator SESUNGGUHNYA (yang sedang berjalan) — terpisah dari
    // override. Sakelar memakai override, operator perlu tahu yang berjalan.
    misting: typeof src.misting === 'boolean' ? src.misting : status.includes('MISTING'),
    irigasi: typeof src.irigasi === 'boolean' ? src.irigasi : status.includes('IRIGASI'),
    blower: level !== null ? level > 0 : status.includes('BLOWER'),
    status,
    manual: src.manual_mode === true,
    flags: {
      sensorError: /sensor error/i.test(status),
      soilErr: status.includes('SOILERR'),
      tdsErr: status.includes('TDSERR'),
      noSync: status.includes('NOSYNC') || !synced,
      // IRRIGOFF = perangkat SENGAJA menonaktifkan irigasi otomatis (mis. debit
      // pompa & luas media belum dikalibrasi), BUKAN gangguan. Perintah
      // manual tetap jalan.
      irrigOff: status.includes('IRRIGOFF'),
    },
    phase: {
      name: typeof src.fase === 'string' ? src.fase : null,
      target: numOrNull(src.vpd_target),
      active: src.fase_aktif === true,
      duration: numOrNull(src.fase_durasi_s),
      elapsed: numOrNull(src.fase_elapsed_s),
      remaining: numOrNull(src.fase_sisa_s),
    },
    // Telemetri irigasi (firmware push). Dosis air per siklus dihitung dari
    // ETc, jadi ini yang menunjukkan "kebutuhan air -> volume air" di layar.
    // Field tidak ada (firmware lama / device belum push) -> null, dan
    // komponen tidak menampilkan apa-apa.
    // Nama "irigasiInfo" (bukan "irigasi") karena key "irigasi" sudah dipakai
    // untuk status boolean aktuator di atas.
    irigasiInfo: {
      doseMl: numOrNull(src.irigasi_dosis_ml),
      doseMs: numOrNull(src.irigasi_durasi_ms),
      budgetMl: numOrNull(src.irigasi_anggaran_ml),
      usedMl: numOrNull(src.irigasi_terpakai_ml),
      intervalMin: numOrNull(src.irigasi_jeda_menit),
      holdReason: typeof src.irigasi_alasan_tahan === 'string' ? src.irigasi_alasan_tahan : '',
    },
  };
  row.override = {
    mist: overrideState(src, 'mist_ov', row.misting),
    irigasi: overrideState(src, 'irig_ov', row.irigasi),
    blower: overrideState(src, 'blower_ov', row.blower),
  };
  return row;
}

// ------------------------------------------------------------------ zona ---
export const zoneId = () => ZONES[state.currentZone].id;
export const zoneName = () => state.labels[zoneId()] || ZONES[state.currentZone].name;

// ----------------------------------------------------------------- auth ---
let auth = null;

// Dashboard terbuka untuk semua orang: data sensor dibaca tanpa login, dan
// hanya kontrol aktuator / ganti nama greenhouse yang butuh akun admin.
export function initAuth() {
  firebase.initializeApp(firebaseConfig);
  auth = firebase.auth();
  // onAuthStateChanged juga menangani login/keluar saat aplikasi sudah terbuka.
  auth.onAuthStateChanged(user => setIdentity(user));
}

function setIdentity(user) {
  const email = user && user.email ? user.email : null;
  state.user = email;
  state.isAdmin = !!email && email === ADMIN_EMAIL;
}

// ----------------------------------------------------------------- data ---
// Pembacaan sensor_logs boleh tanpa login (RTDB rules .read: true), jadi
// token hanya ditambahkan bila sudah login. Penulisan (commands/labels)
// tetap memakai token dan hanya admin yang diizinkan rules.
async function apiRead(path) {
  let token = null;
  try {
    if (auth && auth.currentUser) token = await auth.currentUser.getIdToken();
  } catch (e) { /* tamu: lanjut tanpa token */ }
  // Path boleh sudah membawa query string sendiri (mis. sensor_logs.json
  // ?orderBy=...&limitToLast=...), jadi token harus ditempel dengan '&' kalau
  // sudah ada '?'. Kalau salah pakai '?', limitToLast dapat "20?auth=..." dan
  // RTDB membalas 400 "limitToLast must be an integer".
  return `${DB_URL}/${path}${token ? (path.includes('?') ? '&' : '?') + 'auth=' + token : ''}`;
}

export async function loadLabels() {
  try {
    const res = await fetch(await apiRead('labels.json'));
    if (!res.ok) return;
    state.labels = (await res.json()) || {};
  } catch (e) {
    console.warn("Label tidak terbaca:", e.message);
  }
}

export async function fetchDataLogs() {
  try {
    const path = `zones/${zoneId()}/sensor_logs.json?orderBy="$key"&limitToLast=${state.logLimit}`;
    const res = await fetch(await apiRead(path));
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const obj = await res.json();
    if (!obj) throw new Error('kosong');

    // Urut berdasarkan ts, bukan kunci push-ID. Kunci RTDB urut saat apa yang
    // ditulis, tapi record lama yang diputar ulang dari backlog MicroSD tetap
    // dapat kunci baru - jadi kunci != waktu. Record TANPA ts (jam perangkat
    // belum sinkron) diletakkan di akhir: mereka yang terbaru.
    const rows = Object.entries(obj).map(([k, v]) => normLog(v, k));
    rows.sort((a, b) => {
      const at = a.ts === null ? Infinity : a.ts;
      const bt = b.ts === null ? Infinity : b.ts;
      if (at === bt) return 0;
      return at - bt;
    });

    state.lastData = rows[rows.length - 1] || null;
    if (!state.lastData) throw new Error('kosong');

    state.logData = rows;

    // Offline bukan cuma fetch gagal: log terakhir yang basi juga berarti
    // ESP32 yang hidup tapi tidak push (padam/offline).
    state.age = state.lastData.ts === null
      ? null
      : Math.floor(Date.now() / 1000) - state.lastData.ts;
    state.online = true;
    state.loadError = '';

    // Perintah yang terkirim: konfirmasi (dari nilai terpasang) atau gagal.
    checkPending();
  } catch (err) {
    console.error("Gagal memuat data:", err.message);
    state.online = false;
    state.loadError = err.message;
  }
}

// ----------------------------------------------- data grafik (down-sample) ---
// Down-sample untuk grafik: dirata-ratakan per bucket (60 s / 300 s). Ditaruh
// di store, bukan di komponen, supaya kedua kartu grafik menampilkan data
// yang sama persis. null tidak ikut dirata-rata — rata-rata tetap null ->
// Chart.js menggambar celah, bukan 0 palsu.
const FLAT_FIELDS = ['temperature', 'humidity', 'vpd', 'etc', 'vpdTarget', 'budgetMl', 'soil2', 'soil3'];

// Field trendData dipetakan flat dari baris normLog (grafik cuma butuh angka).
function flatRow(r) {
  return {
    ts: r.ts,
    temperature: r.temperature,
    humidity: r.humidity,
    vpd: r.vpd,
    soil2: r.soil2,
    soil3: r.soil3,
    etc: r.etc,
    vpdTarget: r.phase.target,
    budgetMl: r.irigasiInfo.budgetMl,
    usedMl: r.irigasiInfo.usedMl,
  };
}

const avgOf = (rows, k) => {
  let s = 0, n = 0;
  for (const r of rows) {
    const v = r[k];
    if (typeof v === 'number' && isFinite(v)) { s += v; n++; }
  }
  return n ? s / n : null;
};
const lastOf = (rows, k) => {
  for (let i = rows.length - 1; i >= 0; i--) {
    const v = rows[i][k];
    if (typeof v === 'number' && isFinite(v)) return v;
  }
  return null;
};

export const trendData = computed(() => {
  const bucket = TREND_RANGE[state.trendRange].bucket;
  const groups = new Map();   // slot -> baris flat dalam satu bucket
  const out = [];
  for (const r of state.logData) {
    const f = flatRow(r);
    // Tanpa ts (jam perangkat belum sync) tidak masuk bucket: semuanya akan
    // jatuh ke slot 0 (1970) dan sumbu X kacau. Disimpan sebagai titik
    // sendiri di akhir — sama seperti tampilan "?" sebelumnya.
    if (f.ts === null) continue;
    const slot = Math.floor(f.ts / bucket);
    if (!groups.has(slot)) groups.set(slot, []);
    groups.get(slot).push(f);
  }
  for (const [slot, rows] of groups) {
    const g = { ts: slot * bucket };
    for (const k of FLAT_FIELDS) g[k] = avgOf(rows, k);
    // Kumulatif tidak boleh dirata-rata: tiga titik 20 s berisi 0/0/3764 kalau
    // dirata-rata jadi 1255 — yang benar nilai TERAKHIR 3764.
    g.usedMl = lastOf(rows, 'usedMl');
    out.push(g);
  }
  for (const r of state.logData) if (r.ts === null) out.push(flatRow(r));
  return out;
});

let timer = null;
export function startPolling() {
  loadLabels().then(() => {
    fetchDataLogs();
    startTimer();
  });
  document.addEventListener('visibilitychange', onVisibility);
}
export function stopPolling() {
  clearInterval(timer);
  timer = null;
  document.removeEventListener('visibilitychange', onVisibility);
}

function startTimer() {
  if (timer) clearInterval(timer);
  // Interval mengikuti rentang grafik: 6 jam = 2 menit, 24 jam = 15 menit.
  timer = setInterval(fetchDataLogs, TREND_RANGE[state.trendRange].pollMs);
}

// Hemat kuota: tab tidak terlihat = tidak ada yang memantau -> polling
// berhenti. Saat kembali visible, di-fetch sekali supaya data langsung segar.
function onVisibility() {
  if (document.visibilityState === 'hidden') {
    clearInterval(timer);
    timer = null;
  } else {
    fetchDataLogs();
    startTimer();
  }
}

// Ganti rentang grafik (1j/6j) — dipakai pill di header kedua kartu grafik.
export function setTrendRange(r) {
  if (!TREND_RANGE[r] || r === state.trendRange) return;
  state.trendRange = r;
  state.logLimit = TREND_RANGE[r].limit;
  // Kosongkan jendela lama supaya grafik tidak menampilkan titik rentang lama
  // dengan rentang baru (fetch ulang mengisinya seketika).
  state.logData = [];
  fetchDataLogs();
  startTimer();   // interval polling ikut rentang baru
}

// ------------------------------------------------------------- kendali ---
// Nama aktuator -> label Indonesia (dipakai di pesan).
const ACT_NAMES = { mist: 'Misting', irigasi: 'Irigasi', blower: 'Blower' };

// Status override yang tampil: dari perangkat, ditambah efek optimistis selama
// perintah masih menunggu konfirmasi. Optimistik HANYA boleh mengubah tampilan
// OVERRIDE — status aktuator (ON/OFF) selalu dari perangkat, supaya dashboard
// tidak pernah mengarang apa yang sedang berjalan.
export const overrideStates = computed(() => {
  const base = state.lastData
    ? state.lastData.override
    : { mist: 'auto', irigasi: 'auto', blower: 'auto' };
  const eff = { ...base };
  const p = state.pending;
  if (p) {
    if (p.action === 'auto') {
      eff.mist = 'auto'; eff.irigasi = 'auto'; eff.blower = 'auto';
    } else if (p.action in eff) {
      eff[p.action] = p.value ? 'on' : 'auto';
    }
  }
  return eff;
});

// Ringkasan override manual: aktuator mana yang sedang di-override.
export const manualInfo = computed(() => {
  const ov = overrideStates.value;
  const items = [];
  for (const k of ['mist', 'irigasi', 'blower']) {
    if (ov[k] === 'on') items.push({ key: k, name: ACT_NAMES[k], mode: 'paksa nyala' });
    else if (ov[k] === 'off') items.push({ key: k, name: ACT_NAMES[k], mode: 'paksa mati' });
  }
  const unknown = Object.values(ov).includes('unknown');
  return { items, unknown, any: items.length > 0 || unknown };
});

// Sumber kebenaran posisi sakelar: status OVERRIDE per aktuator (bukan status
// aktuator). Log lama (sebelum field override dikirim) jatuh ke heuristik
// manual_mode.
export const actuators = computed(() => {
  const d = state.lastData;
  const ov = overrideStates.value;
  if (!d) {
    return { mist: false, irigasi: false, blower: false, level: null, override: ov, manual: false, status: '', flags: null };
  }
  return {
    mist: d.misting,
    irigasi: d.irigasi,
    blower: d.blower,
    level: d.blowerLevel,
    override: ov,
    manual: d.manual,
    status: d.status,
    flags: d.flags,
  };
});

// Cocok-filter penuh (tanpa batas tampil) — dipakai pesan "dari N baris".
export const matchedLogs = computed(() => {
  const f = state.logFilter;
  if (!f) return state.logData;
  return state.logData.filter(l => (fmtLogTime(l.ts) + ' ' + (l.status || '')).toLowerCase().includes(f));
});

// Tabel hanya menampilkan logRows baris terakhir dari jendela fetch
// (180/1080 record untuk grafik) supaya tidak memanjang tak terkendali.
export const filteredLogs = computed(() => matchedLogs.value.slice(-state.logRows));

// Badge status aktif mengikuti isi teksnya, bukan warnanya.
export const pillClass = s =>
  (s.includes("MISTING") || s.includes("BLOWER") || s.includes("IRIGASI")) ? 'pill pill-on' : 'pill pill-idle';

// --------------------------------------------------------- perintah RTDB ---
// Firmware membaca commands/{DEVICE_ID}/{entri} = {action, value, unit?}
// (pollCommands() mengambil anak paling lama lalu menghapus entri itu).
// Entri ditulis sebagai ANAK dengan kunci eksplisit (mirip push-ID) supaya
// urutannya jelas dan tidak bergantung pada push-ID yang dibuat server.
// Catatan: bentuk lama (POST {action, value} ke commands/{DEVICE_ID}) juga
// sebenarnya bisa dibaca firmware, karena POST di REST Firebase setara
// push() — objek itu berakhir sebagai anak ber-push-ID. Kunci eksplisit
// dipilih agar antrean perintah jelas urutannya, bukan karena bentuk lama
// tidak terbaca.
const CMD_TIMEOUT_MS = 40000;   // poll 5 s + push 20 s + fetch 10 s + buffer

// Kunci entri perintah: mirip push-ID Firebase (urut waktu, unik).
let seq = 0;
function cmdKey() {
  seq = (seq + 1) % 1000;
  return '-' + Date.now().toString(36) + String(seq).padStart(3, '0') +
    Math.random().toString(36).slice(2, 8);
}

// Label perintah untuk pesan di UI.
function cmdLabel(cmd) {
  if (cmd.action === 'auto') return 'Kembali ke Auto';
  if (cmd.action === 'set_vpd') return 'Target VPD';
  if (cmd.action === 'set_fase') return cmd.value ? 'Nama fase' : 'Nonaktifkan setpoint';
  if (cmd.action === 'set_durasi') return 'Durasi fase';
  if (cmd.action === 'fase_reset') return 'Reset penghitung fase';
  return ACT_NAMES[cmd.action] || cmd.action;
}

// Apakah perangkat sudah mengeksekusi perintah ini? Konfirmasi diambil dari nilai
// yang DIPUSH perangkat (override terpasang / nilai fase terpasang), bukan dari
// balasan HTTP — HTTP 200 cuma berarti "perintah sampai ke RTDB".
function cmdConfirmed(cmd, row) {
  if (!row) return false;
  switch (cmd.action) {
    case 'auto':
      return row.manual === false;
    case 'mist': case 'irigasi': case 'blower': {
      const ov = row.override[cmd.action];
      if (ov === 'on' || ov === 'off' || ov === 'auto') {
        return cmd.value ? ov === 'on' : ov !== 'on';
      }
      // 'unknown' (firmware tanpa status override per aktuator): konfirmasi
      // paling banter dari status aktuator saat ini.
      return cmd.value ? row[cmd.action === 'mist' ? 'misting' : cmd.action] === true : row.manual === false;
    }
    case 'set_vpd':
      return row.phase.target !== null && Math.abs(row.phase.target - cmd.value) < 0.01;
    case 'set_fase':
      return cmd.value ? row.phase.name === cmd.value : row.phase.active === false;
    case 'set_durasi':
      return row.phase.duration !== null && row.phase.duration === cmd.seconds;
    case 'fase_reset':
      return row.phase.elapsed !== null && row.phase.elapsed <= 5;
    default:
      return false;
  }
}

// Perintah yang dikirim: apakah sudah dieksekusi, atau sudah lewat batas waktu?
function checkPending() {
  const p = state.pending;
  if (!p) return;
  if (cmdConfirmed(p, state.lastData)) {
    state.cmdStatus = 'Perintah ' + cmdLabel(p) + ' dieksekusi perangkat.';
    state.cmdError = '';
    state.pending = null;
    return;
  }
  if (Date.now() > p.deadline) {
    // Jangan kirim ulang otomatis: aktuator bisa aktif lagi (risiko
    // actuation berulang). Laporkan terus terang, jangan diam-diam revert.
    state.cmdError = 'Perintah ' + cmdLabel(p) + ' BELUM dieksekusi — perangkat tidak merespons dalam ' +
      Math.round(CMD_TIMEOUT_MS / 1000) + ' detik. Tidak dikirim ulang otomatis.';
    state.cmdStatus = '';
    state.pending = null;
  }
}

export async function sendCommand(action, value, extra = {}) {
  // Tamu tidak punya UI kontrol, tapi tetap dijaga agar tidak bisa menulis.
  if (!auth || !auth.currentUser) {
    state.cmdStatus = 'Kamu sedang masuk sebagai tamu — login untuk kontrol';
    return;
  }
  if (!state.isAdmin) {
    state.cmdStatus = 'Hanya admin yang boleh mengirim perintah';
    return;
  }
  const body = { action: action, value: value };
  if (extra.unit) body.unit = extra.unit;
  state.cmdStatus = 'Mengirim perintah ' + cmdLabel({ action, value }) + '…';
  state.cmdError = '';
  try {
    const token = await auth.currentUser.getIdToken();
    const res = await fetch(`${DB_URL}/commands/${zoneId()}/${cmdKey()}.json?auth=${token}`, {
      method: 'PUT',
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    state.pending = {
      action, value,
      seconds: extra.seconds ?? null,
      sentAt: Date.now(),
      deadline: Date.now() + CMD_TIMEOUT_MS,
    };
    state.cmdStatus = 'Perintah ' + cmdLabel({ action, value }) + ' terkirim — menunggu konfirmasi perangkat…';
  } catch (e) {
    console.error('Gagal kirim perintah:', e.message);
    state.cmdStatus = '';
    state.cmdError = 'Gagal mengirim perintah: ' + e.message;
  }
}

// Sakelar aktuator: NYALA = paksa nyala, MATI = lepas override (kembali ke
// fuzzy). Sakelar MATI bukan "paksa mati" — override manual di firmware
// expires sendiri setelah 10 menit.
export function onSwitch(action, on) {
  sendCommand(action, on);
}

// Tombol "Kembali ke Auto": semua aktuator kembali ke fuzzy (controlRelease).
export function releaseAll() {
  sendCommand('auto', true);
}

// ------------------------------------------------------------------ fase ---
// Setelan fase & target VPD (lihat firmware/src/phase.cpp).
export const VPD_TARGET_MIN = 0.3;
export const VPD_TARGET_MAX = 2.5;

export function setVpdTarget(v) {
  const n = Number(v);
  if (!isFinite(n) || n < VPD_TARGET_MIN || n > VPD_TARGET_MAX) {
    state.cmdError = 'Target VPD harus antara ' + VPD_TARGET_MIN + ' dan ' + VPD_TARGET_MAX + ' kPa';
    return;
  }
  sendCommand('set_vpd', n);
}

export function setPhaseName(name) {
  sendCommand('set_fase', String(name || '').trim());
}

export function setPhaseDuration(value, unit) {
  const n = Number(value);
  if (!isFinite(n) || n < 0) {
    state.cmdError = 'Durasi harus angka >= 0';
    return;
  }
  const mult = { detik: 1, menit: 60, jam: 3600, hari: 86400 }[unit] || 1;
  const sec = Math.round(n * mult);
  if (sec > 31536000) {
    state.cmdError = 'Durasi maksimum 1 tahun';
    return;
  }
  sendCommand('set_durasi', n, { unit, seconds: sec });
}

export function resetPhaseCounter() {
  sendCommand('fase_reset', true);
}

// --------------------------------------------------------------- ekspor ---
// Ekspor CSV dari data yang SUDAH termuat di memori (klien, tanpa backend).
// Tidak menghapus apa pun dari RTDB: rules tidak mengizinkan, dan itu memang
// bukan urusan dashboard.
const csvCell = v => {
  const s = v === null || v === undefined ? '' : String(v);
  return /[",;\n]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s;
};

export function exportCsv() {
  const rows = state.logData;
  if (!rows.length) {
    state.cmdStatus = 'Belum ada data untuk diekspor';
    return;
  }
  const head = ['waktu', 'suhu_C', 'kelembapan_pct', 'tanah_pct', 'vpd_kPa', 'etc_mm_hari',
    'tds_ppm', 'status', 'manual_mode', 'fase', 'vpd_target_kPa', 'fase_aktif',
    'durasi_s', 'elapsed_s', 'sisa_s', 'device'];
  const body = rows.map(r => [
    r.ts ? new Date(r.ts * 1000).toISOString() : '',
    r.temperature, r.humidity, r.soil, r.vpd, r.etc, r.tds,
    r.status, r.manual ? 'YA' : 'TIDAK',
    r.phase.name, r.phase.target, r.phase.active ? 'YA' : 'TIDAK',
    r.phase.duration, r.phase.elapsed, r.phase.remaining, r.device,
  ].map(csvCell).join(','));
  // BOM supaya Excel membaca UTF-8 (nama fase/label bisa mengandung huruf
  // beraksen) dengan benar.
  const blob = new Blob(['﻿' + [head.join(',')].concat(body).join('\n')],
    { type: 'text/csv;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `smartgh_${zoneId()}_${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.csv`;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  state.cmdStatus = `${rows.length} baris diekspor ke CSV`;
}

// ---------------------------------------------------------------- login ---
export function submitLogin(email, pass) {
  return auth.signInWithEmailAndPassword(email, pass)
    .then(() => { state.loginOpen = false; state.loginError = ''; })
    .catch(e => {
      // Jangan bocorkan "user tidak ada" vs "password salah" — itu aiding
      // user enumeration. Detail asli hanya di console.
      console.warn('Login gagal:', e.code || e.message);
      state.loginError = 'Email atau password salah.';
      throw e;
    });
}

export function signOut() {
  auth.signOut().catch(e => console.error("Gagal logout:", e));
}

// ------------------------------------------------------- ganti nama unit ---
// Memakai input inline, BUKAN prompt(): prompt() diblokir di beberapa konteks
// (iframe, embedded browser) sehingga tombolnya jadi tampak tidak mer-respons.
// Enter = simpan, Esc atau klik ke luar = batal.
export async function saveRename(i, val) {
  const id = ZONES[i].id;
  const ok = await sendLabel(val, id);
  if (!ok) {
    showZoneMsg("Gagal menyimpan nama: akses ditolak");
  } else {
    state.labels[id] = val;
    showZoneMsg("Nama greenhouse disimpan", true);
  }
}

async function sendLabel(value, id) {
  if (!auth || !auth.currentUser) return false;   // tamu tidak boleh menulis
  try {
    const token = await auth.currentUser.getIdToken();
    // null menghapus label -> dashboard kembali ke nama default.
    const res = await fetch(`${DB_URL}/labels/${id}.json?auth=${token}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(value)
    });
    return res.ok;
  } catch (e) {
    console.error(e);
    return false;
  }
}

let zoneMsgTimer = null;
export function showZoneMsg(text, ok = false) {
  state.zoneMsg = { text, ok, show: true };
  clearTimeout(zoneMsgTimer);
  zoneMsgTimer = setTimeout(() => { state.zoneMsg.show = false; }, 3000);
}

// ---------------------------------------------------------- ganti unit ---
export function switchZone(i) {
  if (i === state.currentZone) return;
  state.currentZone = i;
  state.lastData = null;
  state.logData = [];
  state.logFilter = '';
  state.online = false;
  state.age = null;
  state.pending = null;
  state.cmdStatus = '';
  state.cmdError = '';
  fetchDataLogs();
}

// ------------------------------------------------------------- refresh ---
export async function refreshWithSpin(btn) {
  state.spinning = true;
  const t0 = Date.now();
  try {
    await fetchDataLogs();
  } finally {
    // Putar minimal ~0.5 s supaya perputarannya terlihat, walau fetch selesai
    // secepatnya.
    setTimeout(() => { state.spinning = false; }, Math.max(0, 500 - (Date.now() - t0)));
  }
}
