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

export const state = reactive({
  // identitas & peran
  user: null,
  isAdmin: false,

  // unit greenhouse
  currentZone: 0,
  labels: {},

  // data sensor
  logData: [],
  lastData: null,
  optimistic: null,
  logFilter: '',
  logLimit: 20,

  // tampilan
  view: 'beranda',
  loginOpen: false,
  loginError: '',
  userMenuOpen: false,
  zoneMenuOpen: false,
  zoneMsg: { text: '', ok: false, show: false },
  cmdStatus: '',
  spinning: false,

  // koneksi
  online: false,
  age: 0,
  loadError: '',

  // rename inline
  renaming: -1,
});

// ---------------------------------------------------------------- format ---
// Angka sensor dibaca ADC 12-bit lalu jadi float: 1.421642 kPa / 5.4894 mm tidak
// pernah berarti apa-apa bagi pengguna. 2 desimal cukup, TANAH/TDS dibulatkan
// karena hanya dipakai sebagai persentase/ppm bulat.
export const fmtNum = (v, d = 2) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(d) : '-';

// Soil moisture = 0..100%. Sensor capacitive yang belum terpasang (pin
// menggantung) dibaca ADC tinggi -> ~100% palsu. Nilai di luar 5..95% tidak
// ditampilkan sebagai angka.
export const saneSoil = v => (typeof v === 'number' && v > 5 && v < 95) ? v : null;

export function fmtClock(ts) {
  if (typeof ts !== 'number' || ts < 100000) return '-';
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
export function humanAge(sec) {
  if (sec < 60) return sec + " detik";
  const m = Math.floor(sec / 60);
  if (m < 60) return m + " menit";
  const sisaM = m % 60;
  const h = Math.floor(m / 60);
  if (h < 24) return h + (sisaM ? " jam " + sisaM + " menit" : " jam");
  const sisaH = h % 24;
  return Math.floor(h / 24) + " hari" + (sisaH ? " " + sisaH + " jam" : "");
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
    // dapat kunci baru - jadi kunci != waktu. Urut ts juga membetulkan
    // kolom waktu di tabel (terbaru di atas) dan sumbu X grafik.
    const data = Object.values(obj).sort((a, b) => (a.ts || 0) - (b.ts || 0));

    state.lastData = data[data.length - 1];
    if (!state.lastData) throw new Error('kosong');

    // Device sudah push log yang mencerminkan perintah kita -> tampilan
    // optimistis tidak diperlukan lagi. Kalau belum, tahan sampai batas
    // waktu (poll perintah 5 s + push 20 s + fetch 10 s) supaya switch
    // tidak balik sendiri ke posisi lama.
    if (state.optimistic) {
      if (state.lastData.manual_mode === state.optimistic.manual_mode) {
        state.optimistic = null;
      } else if (Date.now() < state.optimistic.until) {
        Object.assign(state.lastData, state.optimistic);
      } else {
        state.optimistic = null;
      }
    }

    state.logData = data;
    // Offline bukan cuma fetch gagal: log terakhir yang basi juga berarti
    // ESP32 yang hidup tapi tidak push (padam/offline).
    const age = Math.floor(Date.now() / 1000) - state.lastData.ts;
    state.online = true;
    state.age = age;
    state.loadError = '';
  } catch (err) {
    console.error("Gagal memuat data:", err.message);
    state.online = false;
    state.loadError = err.message;
  }
}

let timer = null;
export function startPolling() {
  loadLabels().then(() => {
    fetchDataLogs();
    timer = setInterval(fetchDataLogs, 10000);
  });
}
export function stopPolling() {
  clearInterval(timer);
  timer = null;
}

// -------------------------------------------------------------- turunan ---
// Sumber kebenaran posisi sakelar: field keluaran fuzzy. Log lama (sebelum
// field ini dikirim) masih jatuh ke status_sistem.
export const actuators = computed(() => {
  const d = state.lastData;
  if (!d) return { mist: false, irigasi: false, blower: false, auto: true, level: null };
  const s = d.status_sistem || "";
  const on = (v, fallback) => (typeof v === 'boolean' ? v : fallback);
  const level = typeof d.blower_level === 'number' ? d.blower_level : null;
  return {
    mist: on(d.misting, s.includes("MISTING")),
    irigasi: on(d.irigasi, s.includes("IRIGASI")),
    // Blower hidup bila tingkatnya > 0 (PWM); tidak ada field boolean terpisah.
    blower: level !== null ? level > 0 : s.includes("BLOWER"),
    level,
    auto: d.manual_mode !== true,
  };
});

export const filteredLogs = computed(() => {
  const f = state.logFilter;
  if (!f) return state.logData;
  return state.logData.filter(l => (fmtLogTime(l.ts) + ' ' + (l.status_sistem || '')).toLowerCase().includes(f));
});

// Badge status aktif mengikuti isi teksnya, bukan warnanya.
export const pillClass = s =>
  (s.includes("MISTING") || s.includes("BLOWER") || s.includes("IRIGASI")) ? 'pill pill-on' : 'pill pill-idle';

// ------------------------------------------------------------- kendali ---
// Terapkan perintah ke tampilan segera. Sumber kebenaran tetap perangkat;
// ini hanya jembatan sampai log baru (yang memuat manual_mode/status_sistem)
// tiba — lihat penahan `optimistic` di fetchDataLogs().
function applyLocalCommand(action, value) {
  if (!state.lastData) return;
  let manual = true;
  let status = state.lastData.status_sistem || "";
  if (action === "auto") {
    manual = false;   // controlRelease() -> kembali ke fuzzy
  } else {
    const kw = action === "irigasi" ? "IRIGASI" : (action === "blower" ? "BLOWER" : "MISTING");
    if (value) {
      if (!status.includes(kw)) status += " " + kw;
    } else {
      status = status.split(" " + kw).join("");
    }
  }
  state.optimistic = { manual_mode: manual, status_sistem: status, until: Date.now() + 35000 };
  state.lastData.manual_mode = manual;
  state.lastData.status_sistem = status;
}

function flash(msg, ms = 4000) {
  state.cmdStatus = msg;
  setTimeout(() => { if (state.cmdStatus === msg) state.cmdStatus = ''; }, ms);
}

export async function sendCommand(action, value) {
  // Tamu tidak punya UI kontrol, tapi tetap dijaga agar tidak bisa menulis.
  if (!auth || !auth.currentUser) {
    flash("Kamu sedang masuk sebagai tamu — login untuk kontrol");
    return;
  }
  flash("Mengirim perintah...");
  // Terapkan ke tampilan SEBELUM menunggu jaringan. Kalau teks menunggu
  // balasan RTDB (ratusan ms), switch dan tulisan ON/OFF terlihat bertentangan.
  applyLocalCommand(action, value);
  try {
    const token = await auth.currentUser.getIdToken();
    const res = await fetch(`${DB_URL}/commands/${zoneId()}.json?auth=${token}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ action: action, value: value })
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    // Device mengeksekusi <=5 s lalu mengonfirmasi lewat push (<=20 s).
    flash("Perintah " + action + " terkirim ke " + zoneId() + " (dieksekusi hingga 5 detik)");
  } catch (e) {
    // Perintah gagal: kembalikan tampilan ke kondisi sebelum diklik.
    applyLocalCommand(action, !value);
    flash("Gagal kirim: " + e.message);
  }
}

export function onSwitch(action, on) {
  if (action === "auto") {
    if (!on) {
      flash("Auto tidak bisa dimatikan langsung — geser aktuator manual untuk keluar dari auto");
      return;
    }
    sendCommand('auto', true);
    return;
  }
  sendCommand(action, on);
}

// ---------------------------------------------------------------- login ---
export function submitLogin(email, pass) {
  return auth.signInWithEmailAndPassword(email, pass)
    .then(() => { state.loginOpen = false; state.loginError = ''; })
    .catch(e => { state.loginError = e.message; throw e; });
}

export function signOut() {
  auth.signOut().catch(e => console.error("Gagal logout:", e));
}

// ------------------------------------------------------- ganti nama unit ---
// Memakai input inline, BUKAN prompt(): prompt() diblokir di beberapa konteks
// (iframe, embedded browser) sehingga tombolnya jadi tampak tidak berrespons.
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
  state.optimistic = null;
  // Buang sisa data greenhouse sebelumnya supaya tabel/grafik tidak sempat
  // menampilkan unit lama selagi data yang baru dimuat.
  state.logData = [];
  state.logFilter = '';
  state.online = false;
  state.age = 0;
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
