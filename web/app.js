// true = mode demo (data dummy, tanpa Firebase/auth, cocok untuk presentasi).
// false = mode produksi (Firebase Auth + RTDB live).
const DEMO_MODE = false;

let auth = null;
let currentZone = 0;      // index ke ZONES
let labels = {};          // { deviceId: "Nama tanaman" } dari RTDB
let isAdmin = false;
let zoneTimer = null;
let logData = [];         // log terakhir yang sudah difilter (untuk render tabel)
let logFilter = '';       // teks pencarian log
let logLimit = 20;        // berapa baris yang diambil dari RTDB (dropdown "Show N entries")

document.addEventListener('DOMContentLoaded', () => {
    if (DEMO_MODE) {
        document.getElementById('app').classList.remove('hidden');
        document.getElementById('login').classList.add('hidden');
        document.getElementById('onlineBadge').classList.remove('on', 'off');
        document.getElementById('onlineBadge').classList.add('demo');
        document.getElementById('onlineText').textContent = 'Demo Mode';
        applyRole(ADMIN_EMAIL);   // demo: tampilkan UI admin
        buildZoneSelect();
        loadDemoData();
        return;
    }

    // Firebase Auth: tanda tangan Email/Password sebelum data dimuat.
    // config.js menyediakan firebaseConfig, DB_URL, ZONES, ADMIN_EMAIL.
    // Persistence default = local: sesi tersimpan di browser, auto-login tiap buka.
    firebase.initializeApp(firebaseConfig);
    auth = firebase.auth();

    auth.onAuthStateChanged(user => {
        const app = document.getElementById('app');
        const login = document.getElementById('login');
        if (user) {
            app.classList.remove('hidden');
            login.classList.add('hidden');
            document.getElementById('userEmail').textContent = user.email;
            document.getElementById('userAvatar').textContent = (user.email || '?').charAt(0).toUpperCase();
            document.getElementById('userName').textContent = (user.email || '?').split('@')[0];
            applyRole(user.email);
            startDashboard();
        } else {
            app.classList.add('hidden');
            login.classList.remove('hidden');
        }
    });

    document.getElementById('loginForm').addEventListener('submit', async (e) => {
        e.preventDefault();
        const err = document.getElementById('loginError');
        err.textContent = '';
        const email = document.getElementById('loginEmail').value.trim();
        const pass = document.getElementById('loginPass').value;
        try {
            await auth.signInWithEmailAndPassword(email, pass);
        } catch (e2) {
            err.textContent = e2.message;
        }
    });
});

// Avatar: klik membuka menu akun, klik di luar menutupnya.
document.addEventListener('DOMContentLoaded', () => {
    const chip = document.getElementById('userChip');
    const menu = document.getElementById('userMenu');
    chip.addEventListener('click', e => {
        const open = menu.hidden;
        menu.hidden = !open;
        chip.setAttribute('aria-expanded', String(open));
        e.stopPropagation();
    });
    document.addEventListener('click', () => {
        menu.hidden = true;
        chip.setAttribute('aria-expanded', 'false');
        closeZoneMenu();
    });
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape') { menu.hidden = true; closeZoneMenu(); }
    });
});

// Navigasi sidebar: 3 view (Beranda / Kontrol / Log Sensor) tanpa router —
// cukup tampilkan satu section dan geser penanda aktif. Sidebar jadi drawer
// off-canvas di layar kecil, dibuka lewat tombol hamburger.
const VIEW_TITLES = { beranda: 'Beranda', kontrol: 'Kontrol', log: 'Log Sensor' };
document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.nav-item').forEach(item => {
        item.addEventListener('click', e => {
            e.preventDefault();
            switchView(item.dataset.view);
            closeNav();
        });
    });

    const burger = document.getElementById('burger');
    burger.addEventListener('click', toggleNav);
    document.getElementById('navBackdrop').addEventListener('click', closeNav);

    // Ikon Refresh berputar selama pemuatan. Dipanggil lewat listener (bukan
    // onclick inline) supaya kita bisa tahu kapan fetch selesai.
    document.getElementById('refreshBtn').addEventListener('click', async () => {
        const btn = document.getElementById('refreshBtn');
        btn.classList.add('spinning');
        const t0 = Date.now();
        try {
            await fetchDataLogs();
        } finally {
            // Putar minimal ~0.5 s supaya perputarannya terlihat, walau fetch
            // selesai secepatnya.
            setTimeout(() => btn.classList.remove('spinning'), Math.max(0, 500 - (Date.now() - t0)));
        }
    });

    // Search + "Show N entries" hanya relevan di halaman Log.
    document.getElementById('logSearch').addEventListener('input', e => {
        logFilter = e.target.value.trim().toLowerCase();
        renderLogTable();
    });
    document.getElementById('showEntries').addEventListener('change', e => {
        logLimit = parseInt(e.target.value, 10) || 20;
        // Demo tidak punya auth/RTDB untuk diambil ulang; cukup render ulang.
        if (DEMO_MODE) renderLogTable();
        else fetchDataLogs();   // jumlah baris berubah -> ambil ulang dari RTDB
    });
});

function switchView(name) {
    Object.keys(VIEW_TITLES).forEach(v => {
        document.getElementById('view-' + v).classList.toggle('hidden', v !== name);
    });
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.toggle('active', item.dataset.view === name);
    });
    document.getElementById('pageTitle').textContent = VIEW_TITLES[name] || 'Beranda';
}

const isNarrow = () => window.matchMedia('(max-width: 900px)').matches;

// Satu tombol, dua perilaku mengikuti lebar layar: di desktop sidebar
// disembunyikan (nav-closed, konten melebar), di layar kecil sidebar jadi
// drawer yang muncul lewat nav-open + backdrop.
function toggleNav() {
    const open = isNarrow()
        ? document.body.classList.toggle('nav-open')
        : document.body.classList.toggle('nav-closed');
    document.getElementById('burger').setAttribute('aria-expanded', String(open));
}

function closeNav() {
    if (!isNarrow()) return;      // di desktop pilihan menu tidak menutup sidebar
    document.body.classList.remove('nav-open');
    document.getElementById('burger').setAttribute('aria-expanded', 'false');
}

function signOut() {
    if (DEMO_MODE) return;
    auth.signOut().catch(e => console.error("Gagal logout:", e));
}

function zoneId() {
    return ZONES[currentZone].id;
}

function zoneName() {
    return labels[zoneId()] || ZONES[currentZone].name;
}

// Kontrol aktuator & set label hanya untuk admin. RTDB rules yang sesungguhnya
// menolak non-admin; ini menyembunyikan UI supaya tidak membingungkan.
function applyRole(email) {
    isAdmin = (email === ADMIN_EMAIL);
    document.getElementById('ctrlCard').classList.toggle('hidden', !isAdmin);
    const badge = document.getElementById('roleBadge');
    badge.textContent = isAdmin ? 'Admin' : 'Mode lihat';
    badge.classList.toggle('admin', isAdmin);
    document.getElementById('userRole').textContent = isAdmin ? 'Super Admin' : 'Mode Lihat';
}

// Grafik membaca palet yang sama dengan style.css, jadi tidak ada warna
// yang "melenceng" dari skema dashboard.
const cssVar = n => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
Chart.defaults.color = cssVar('--muted');
Chart.defaults.borderColor = cssVar('--border');

const sensorChart = new Chart(document.getElementById('sensorChart').getContext('2d'), {
    type: 'line',
    data: {
        labels: [],
        datasets: [
            { label: 'Suhu (°C)', data: [], borderColor: cssVar('--danger'), backgroundColor: 'rgba(220,38,38,0.08)', pointRadius: 2, tension: 0.3, yAxisID: 'y' },
            { label: 'Kelembapan (%)', data: [], borderColor: cssVar('--accent'), backgroundColor: 'rgba(37,99,235,0.08)', pointRadius: 2, tension: 0.3, yAxisID: 'y1' }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'index', intersect: false },
        scales: {
            // suggestedMin/Max, bukan min/max: rentang tetap supaya kemiringan
            // garis jujur. Tanpa ini Chart.js auto-zoom dan 0.5°C terlihat
            // seperti lonjakan besar.
            x: { grid: { color: cssVar('--card-2') }, ticks: { color: cssVar('--muted'), maxTicksLimit: 8, maxRotation: 0, autoSkipPadding: 12 } },
            y: { type: 'linear', position: 'left', suggestedMin: 20, suggestedMax: 40, grid: { color: cssVar('--card-2') }, ticks: { color: cssVar('--muted') }, title: { display: true, text: 'Suhu (°C)', color: cssVar('--danger'), font: { weight: 600 } } },
            y1: { type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 100, grid: { drawOnChartArea: false }, ticks: { color: cssVar('--muted') }, title: { display: true, text: 'Kelembapan (%)', color: cssVar('--accent'), font: { weight: 600 } } }
        }
    }
});

let lastData = null;
// Perintah manual yang sudah kita kirim tapi belum tentu tercermin di log
// perangkat (device push tiap 20 s). Dipakai supaya switch/Auto langsung
// berubah saat diklik, dan tidak "balik sendiri" di fetch berikutnya.
let optimistic = null;

function startDashboard() {
    buildZoneSelect();
    loadLabels().then(() => {
        fetchDataLogs();
        zoneTimer = setInterval(fetchDataLogs, 10000);
    });
}

// Daftar unit greenhouse di dalam popup. Satu unit ESP32 = satu baris;
// memilih baris menukar seluruh data yang dipantau & dikontrol.
function buildZoneSelect() {
    document.getElementById('zoneBtnName').textContent = zoneName();
    const list = document.getElementById('zoneList');
    list.innerHTML = '';
    ZONES.forEach((z, i) => {
        const row = document.createElement('div');
        row.className = 'zone-row' + (i === currentZone ? ' active' : '');
        row.innerHTML = `
            <button type="button" class="z-pick">
                <span class="z-text">
                    <span class="z-name"></span>
                    <span class="z-id"></span>
                </span>
                ${i === currentZone ? '<svg class="z-check" viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>' : ''}
            </button>
            ${isAdmin ? '<button type="button" class="z-edit" title="Ganti nama greenhouse" aria-label="Ganti nama greenhouse"><svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/></svg></button>' : ''}`;
        row.querySelector('.z-name').textContent = labels[z.id] || z.name;
        row.querySelector('.z-id').textContent = z.id;
        row.querySelector('.z-pick').addEventListener('click', () => { switchZone(i); closeZoneMenu(); });
        const edit = row.querySelector('.z-edit');
        if (edit) edit.addEventListener('click', () => renameZone(i));
        list.appendChild(row);
    });
    // Nama unit yang sedang dipantau, supaya jelas data di halaman ini milik
    // greenhouse yang mana (bukan jumlah unit).
    document.getElementById('zoneCount').textContent = zoneName();
}

function switchZone(i) {
    if (i === currentZone) return;
    currentZone = i;
    lastData = null;
    optimistic = null;
    // Buang sisa data greenhouse sebelumnya supaya tabel/grafik tidak sempat
    // menampilkan unit lama selagi data yang baru dimuat.
    logData = [];
    logFilter = '';
    document.getElementById('logSearch').value = '';
    sensorChart.data.labels = [];
    sensorChart.data.datasets.forEach(d => d.data = []);
    sensorChart.update();
    renderLogTable();
    setOnline(false);
    buildZoneSelect();
    fetchDataLogs();
}

function openZoneMenu() {
    const m = document.getElementById('zoneMenu');
    m.hidden = false;
    document.getElementById('zoneBtn').setAttribute('aria-expanded', 'true');
}

function closeZoneMenu() {
    document.getElementById('zoneMenu').hidden = true;
    document.getElementById('zoneBtn').setAttribute('aria-expanded', 'false');
}

document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('zoneBtn').addEventListener('click', e => {
        e.stopPropagation();
        const m = document.getElementById('zoneMenu');
        if (m.hidden) openZoneMenu(); else closeZoneMenu();
    });
    // Klik di luar popup menutupnya (sudah ditangani listener global di bawah).
    document.getElementById('zoneMenu').addEventListener('click', e => e.stopPropagation());
});

async function loadLabels() {
    if (DEMO_MODE) return;
    try {
        const token = await auth.currentUser.getIdToken();
        const res = await fetch(`${DB_URL}/labels.json?auth=${token}`);
        if (!res.ok) return;
        labels = (await res.json()) || {};
    } catch (e) {
        console.warn("Label tidak terbaca:", e.message);
    }
}

// Ganti nama greenhouse. Dipanggil dari tombol pensil di daftar unit, jadi
// boleh untuk unit mana pun — bukan hanya yang sedang dipantau.
function renameZone(i = currentZone) {
    const id = ZONES[i].id;
    const n = prompt('Nama greenhouse untuk ' + id + ':', labels[id] || ZONES[i].name);
    if (!n) return;
    const val = n.trim();
    if (!val) return;
    const status = document.getElementById('cmdStatus');
    sendLabel(val, id).then(ok => {
        if (!ok) status.textContent = "Gagal menyimpan nama: akses ditolak";
        else labels[id] = val;
        buildZoneSelect();
        if (i === currentZone) setOnlineText();
        setTimeout(() => status.textContent = "", 4000);
    });
}

// Badge koneksi ikut memuat nama greenhouse, jadi harus disegarkan setiap
// nama berubah (setelah ganti nama atau ganti unit).
function setOnlineText() {
    if (document.getElementById('onlineBadge').classList.contains('demo')) return;
    const text = document.getElementById('onlineText');
    if (text.textContent.includes('offline')) text.textContent = zoneName() + ' offline';
    else text.textContent = zoneName() + ' online · ' + new Date().toLocaleTimeString('id-ID');
}

async function sendLabel(value, id) {
    if (DEMO_MODE) return true;
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

function loadDemoData() {
    const now = Math.floor(Date.now() / 1000);
    const logs = [];
    const base = [
        [29.1, 62, 38, 1.42, 5.2, 412, "Normal"],
        [29.8, 58, 33, 1.58, 5.5, 438, "Normal MISTING"],
        [30.4, 55, 30, 1.72, 5.8, 455, "Normal MISTING"],
        [30.1, 59, 28, 1.66, 5.6, 470, "Normal BLOWER"],
        [28.7, 66, 25, 1.35, 5.1, 488, "Normal IRIGASI"],
        [27.9, 70, 41, 1.18, 4.9, 905, "Normal"],
        [27.4, 74, 44, 1.04, 4.6, 940, "Normal"],
        [26.8, 78, 47, 0.88, 4.3, 972, "Normal BLOWER"]
    ];
    base.forEach((b, i) => {
        logs.push({
            ts: now - (base.length - 1 - i) * 20,
            temperature: b[0],
            humidity: b[1],
            soil: b[2],
            vpd: b[3],
            et0: b[4],
            tds: b[5],
            status_sistem: b[6],
            manual_mode: false
        });
    });

    lastData = logs[logs.length - 1];
    renderLogs(logs);
    updateActuatorStates();
    updateFreshness(20);
    document.getElementById('userEmail').textContent = 'demo@smartgreenhouse.local';
    document.getElementById('userAvatar').textContent = 'D';
    document.getElementById('userName').textContent = 'demo';
    const badge = document.getElementById('onlineText');
    badge.textContent = 'Demo Mode';
    document.getElementById('onlineBadge').classList.remove('off', 'on');
    document.getElementById('onlineBadge').classList.add('demo');
}

function renderLogs(data) {
    // Angka sensor dibaca ADC 12-bit lalu jadi float: 1.421642 kPa / 5.4894 mm
    // tidak pernah berarti apa-apa bagi pengguna. 2 desimal cukup, TANAH/TDS
    // dibulatkan karena hanya dipakai sebagai persentase/ppm bulat.
    const f = (v, d = 2) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(d) : '-';
    document.getElementById('mv-temp').textContent = f(lastData.temperature);
    document.getElementById('mv-hum').textContent = f(lastData.humidity);
    document.getElementById('mv-vpd').textContent = f(lastData.vpd);
    document.getElementById('mv-et0').textContent = f(lastData.et0);
    document.getElementById('mv-tds').textContent = f(lastData.tds, 0);
    document.getElementById('mv-soil').textContent = f(saneSoil(lastData.soil), 0);

    // Sumbu X: jam saja — label penuh ("11/10/2026, 22.27.15") tidak muat.
    sensorChart.data.labels = data.map(l => fmtClock(l.ts));
    sensorChart.data.datasets[0].data = data.map(l => l.temperature);
    sensorChart.data.datasets[1].data = data.map(l => l.humidity);
    sensorChart.update();

    logData = data;
    renderLogTable();
}

// Tabel log di-filter client-side: baris yang sudah termuat (10/20/50,
// tergantung dropdown "Show N entries") tidak perlu fetch ulang saat mengetik.
function renderLogTable() {
    const f = (v, d = 2) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(d) : '-';
    const rows = logFilter
        ? logData.filter(l => (fmtLogTime(l.ts) + ' ' + (l.status_sistem || '')).toLowerCase().includes(logFilter))
        : logData;

    const tableBody = document.getElementById('logTableBody');
    tableBody.innerHTML = '';   // sekaligus menghapus baris "Gagal memuat" dari fetch sebelumnya
    if (!rows.length) {
        tableBody.innerHTML = `<tr><td colspan="8" class="empty">${logFilter ? 'Tidak ada baris yang cocok' : 'Belum ada data'}</td></tr>`;
        document.getElementById('logFoot').textContent = '';
        return;
    }
    [...rows].reverse().forEach(log => {
        const s = log.status_sistem || "";
        let pill = '<span class="pill idle">' + s + '</span>';
        if (s.includes("MISTING")) pill = '<span class="pill mist">' + s + '</span>';
        if (s.includes("BLOWER")) pill = '<span class="pill blower">' + s + '</span>';
        if (s.includes("IRIGASI")) pill = '<span class="pill irigasi">' + s + '</span>';

        tableBody.innerHTML += `
            <tr>
                <td>${fmtLogTime(log.ts)}</td>
                <td class="num">${f(log.temperature)}</td>
                <td class="num">${f(log.humidity)}</td>
                <td class="num">${f(saneSoil(log.soil), 0)}</td>
                <td class="num">${f(log.vpd)}</td>
                <td class="num">${f(log.et0)}</td>
                <td class="num">${f(log.tds, 0)}</td>
                <td>${pill}</td>
            </tr>`;
    });
    document.getElementById('logFoot').textContent =
        logFilter ? `Menampilkan ${rows.length} dari ${logData.length} baris yang cocok` : `Menampilkan ${rows.length} baris terbaru`;
}

// Soil moisture = 0..100%. Sensor capacitive yang belum terpasang (pin
// menggantung) dibaca ADC tinggi -> ~100% palsu. Nilai di luar 5..95% tidak
// ditampilkan sebagai angka.
function saneSoil(v) {
    return (typeof v === 'number' && v > 5 && v < 95) ? v : null;
}

function fmtClock(ts) {
    if (typeof ts !== 'number' || ts < 100000) return '-';
    // Detik ikut karena ESP32 push tiap ~20 s: tanpa ini dua titik berdekatan
    // sering jatuh di menit yang sama dan label sumbu X terulang.
    return new Date(ts * 1000).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function fmtLogTime(ts) {
    if (typeof ts !== 'number' || ts < 100000) return '-';
    return new Date(ts * 1000).toLocaleString('id-ID');
}

async function fetchDataLogs() {
    try {
        const token = await auth.currentUser.getIdToken();
        const res = await fetch(`${DB_URL}/zones/${zoneId()}/sensor_logs.json?orderBy="$key"&limitToLast=${logLimit}&auth=${token}`);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const obj = await res.json();
        if (!obj) throw new Error('kosong');

        const data = Object.entries(obj)
            .sort((a, b) => a[0].localeCompare(b[0]))
            .map(([, v]) => v);

        lastData = data[data.length - 1];
        if (!lastData) throw new Error('kosong');

        // Device sudah push log yang mencerminkan perintah kita -> tampilan
        // optimistis tidak diperlukan lagi. Kalau belum, tahan sampai batas
        // waktu (poll perintah 5 s + push 20 s + fetch 10 s) supaya switch
        // tidak balik sendiri ke posisi lama.
        if (optimistic) {
            if (lastData.manual_mode === optimistic.manual_mode) {
                optimistic = null;
            } else if (Date.now() < optimistic.until) {
                Object.assign(lastData, optimistic);
            } else {
                optimistic = null;
            }
        }

        renderLogs(data);
        updateActuatorStates();   // posisi switch & Auto dari status_sistem + manual_mode
        // Offline bukan cuma fetch gagal: log terakhir yang basi juga berarti
        // ESP32 yang hidup tapi tidak push (padam/offline).
        const age = Math.floor(Date.now() / 1000) - lastData.ts;
        setOnline(true, age);
        updateFreshness(age);
    } catch (err) {
        console.error("Gagal memuat data:", err.message);
        setOnline(false);
        const cell = document.getElementById('logEmpty');
        if (cell) { cell.textContent = "Gagal memuat data (" + err.message + ") — coba Refresh"; cell.classList.add('error'); }
        document.getElementById('freshness').textContent = '';
    }
}

// Umur data terakhir di bawah grafik: pengguna bisa membedakan "sensor diam
// 40 detik" dari "dashboardnya yang macet".
function updateFreshness(age) {
    const el = document.getElementById('freshness');
    if (typeof age !== 'number') { el.textContent = ''; return; }
    el.textContent = age < 60
        ? "Data terakhir " + age + " detik lalu"
        : "Data terakhir " + Math.round(age / 60) + " menit lalu";
}

function updateActuatorStates() {
    if (!lastData) return;
    const s = lastData.status_sistem || "";
    document.getElementById('sw-mist').checked = s.includes("MISTING");
    document.getElementById('sw-blower').checked = s.includes("BLOWER");
    document.getElementById('sw-irigasi').checked = s.includes("IRIGASI");
    setCtrlState('mist', s.includes("MISTING"));
    setCtrlState('blower', s.includes("BLOWER"));
    setCtrlState('irigasi', s.includes("IRIGASI"));

    const manual = lastData.manual_mode === true;
    setCtrlState('auto', !manual);
    document.getElementById('sw-auto').checked = !manual;
}

// Kartu kontrol: status "ON"/"OFF" di tengah kartu (gaya referensi), hijau
// saat aktif dan merah saat mati.
function setCtrlState(key, on) {
    const el = document.getElementById('state-' + key);
    el.textContent = on ? "ON" : "OFF";
    el.classList.toggle('on', !!on);
}

function setOnline(ok, age) {
    const badge = document.getElementById('onlineBadge');
    const text = document.getElementById('onlineText');
    badge.classList.remove('on', 'off', 'demo');
    badge.classList.add(ok ? 'on' : 'off');
    const name = zoneName();
    text.textContent = ok
        ? name + ' online · ' + new Date().toLocaleTimeString('id-ID')
        : name + ' offline';
    if (ok && typeof age === 'number' && age > 60) {
        text.textContent = name + ' diam ' + Math.round(age / 60) + ' menit';
        badge.classList.remove('on');
        badge.classList.add('off');
    }
}

// Terapkan perintah ke tampilan segera. Sumber kebenaran tetap perangkat;
// ini hanya jembatan sampai log baru (yang memuat manual_mode/status_sistem)
// tiba — lihat penahan `optimistic` di fetchDataLogs().
function applyLocalCommand(action, value) {
    if (!lastData) return;
    let manual = true;
    let status = lastData.status_sistem || "";
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
    optimistic = { manual_mode: manual, status_sistem: status, until: Date.now() + 35000 };
    lastData.manual_mode = manual;
    lastData.status_sistem = status;
    updateActuatorStates();
}

async function sendCommand(action, value) {
    const status = document.getElementById('cmdStatus');
    status.textContent = "Mengirim perintah...";
    // Terapkan ke tampilan SEBELUM menunggu jaringan. Browser sudah mencentang
    // checkbox seketika saat diklik; kalau teks menunggu balasan RTDB (ratusan
    // ms), switch dan tulisan ON/OFF terlihat bertentangan.
    applyLocalCommand(action, value);
    if (DEMO_MODE) {
        status.textContent = action === "auto"
            ? "Demo: mode otomatis (semua kembalikan ke fuzzy)"
            : "Demo: " + action + " " + (value ? "ON" : "OFF");
        setTimeout(() => status.textContent = "", 4000);
        return;
    }
    try {
        const token = await auth.currentUser.getIdToken();
        const res = await fetch(`${DB_URL}/commands/${zoneId()}.json?auth=${token}`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ action: action, value: value })
        });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        // Device mengeksekusi <=5 s lalu mengonfirmasi lewat push (<=20 s).
        status.textContent = "Perintah " + action + " terkirim ke " + zoneId() + " (dieksekusi hingga 5 detik)";
    } catch (e) {
        // Perintah gagal: kembalikan tampilan ke kondisi sebelum diklik.
        applyLocalCommand(action, !value);
        status.textContent = "Gagal kirim: " + e.message;
    }
    setTimeout(() => status.textContent = "", 4000);
}

function onSwitch(action, on) {
    if (action === "auto") {
        if (!on) {
            const status = document.getElementById('cmdStatus');
            status.textContent = "Auto tidak bisa dimatikan langsung — geser aktuator manual untuk keluar dari auto";
            setTimeout(() => status.textContent = "", 4000);
            updateActuatorStates();
            return;
        }
        sendCommand('auto', true);
        return;
    }
    sendCommand(action, on);
}
