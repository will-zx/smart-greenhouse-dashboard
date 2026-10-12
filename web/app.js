// true = mode demo (data dummy, tanpa Firebase/auth, cocok untuk presentasi).
// false = mode produksi (Firebase Auth + RTDB live).
const DEMO_MODE = false;

let auth = null;
let currentZone = 0;      // index ke ZONES
let labels = {};          // { deviceId: "Nama tanaman" } dari RTDB
let isAdmin = false;
let zoneTimer = null;

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
    });
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape') menu.hidden = true;
    });
});

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
    document.getElementById('labelBtn').classList.toggle('hidden', !isAdmin);
    const badge = document.getElementById('roleBadge');
    badge.textContent = isAdmin ? 'Admin' : 'Mode lihat';
    badge.classList.toggle('admin', isAdmin);
}

Chart.defaults.color = '#64748b';
Chart.defaults.borderColor = '#dfe6ee';

const sensorChart = new Chart(document.getElementById('sensorChart').getContext('2d'), {
    type: 'line',
    data: {
        labels: [],
        datasets: [
            { label: 'Suhu (°C)', data: [], borderColor: '#c62828', backgroundColor: 'rgba(198,40,40,0.10)', pointRadius: 2, tension: 0.3, yAxisID: 'y' },
            { label: 'Kelembapan (%)', data: [], borderColor: '#0277bd', backgroundColor: 'rgba(2,119,189,0.10)', pointRadius: 2, tension: 0.3, yAxisID: 'y1' }
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
            x: { grid: { color: '#eef2f7' }, ticks: { color: '#64748b', maxTicksLimit: 8, maxRotation: 0, autoSkipPadding: 12 } },
            y: { type: 'linear', position: 'left', suggestedMin: 20, suggestedMax: 40, grid: { color: '#eef2f7' }, ticks: { color: '#64748b' }, title: { display: true, text: 'Suhu (°C)', color: '#c62828', font: { weight: 600 } } },
            y1: { type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 100, grid: { drawOnChartArea: false }, ticks: { color: '#64748b' }, title: { display: true, text: 'Kelembapan (%)', color: '#0277bd', font: { weight: 600 } } }
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

function buildZoneSelect() {
    const sel = document.getElementById('zoneSelect');
    sel.innerHTML = '';
    ZONES.forEach((z, i) => {
        const opt = document.createElement('option');
        opt.value = i;
        opt.textContent = labels[z.id] || z.name;
        sel.appendChild(opt);
    });
    sel.value = currentZone;
    // Device id pindah ke title: informasinya tetap ada tanpa memakan lebar header.
    sel.title = 'Perangkat: ' + zoneId() + ' — pilih greenhouse';
    sel.onchange = () => {
        currentZone = parseInt(sel.value, 10) || 0;
        lastData = null;
        optimistic = null;
        fetchDataLogs();
    };
    document.getElementById('zoneCount').textContent =
        ZONES.length > 1 ? ZONES.length + ' greenhouse' : '';
}

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

function renameZone() {
    if (DEMO_MODE) {
        const n = prompt('Nama greenhouse', zoneName());
        if (n) { labels[zoneId()] = n.trim(); buildZoneSelect(); }
        return;
    }
    const n = prompt('Nama greenhouse untuk ' + zoneId() + ':', zoneName());
    if (!n) return;
    const val = n.trim();
    const status = document.getElementById('cmdStatus');
    sendLabel(val).then(ok => {
        if (!ok) { status.textContent = "Gagal menyimpan nama: akses ditolak"; }
        else { labels[zoneId()] = val; buildZoneSelect(); }
        setTimeout(() => status.textContent = "", 4000);
    });
}

async function sendLabel(value) {
    if (DEMO_MODE) return true;
    try {
        const token = await auth.currentUser.getIdToken();
        // null menghapus label -> dashboard kembali ke nama default.
        const res = await fetch(`${DB_URL}/labels/${zoneId()}.json?auth=${token}`, {
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

    const tableBody = document.getElementById('logTableBody');
    tableBody.innerHTML = '';   // sekaligus menghapus baris "Gagal memuat" dari fetch sebelumnya
    [...data].reverse().forEach(log => {
        const s = log.status_sistem || "";
        let pill = '<span class="pill idle">' + s + '</span>';
        if (s.includes("MISTING")) pill = '<span class="pill mist">' + s + '</span>';
        if (s.includes("BLOWER")) pill = '<span class="pill blower">' + s + '</span>';
        if (s.includes("IRIGASI")) pill = '<span class="pill irigasi">' + s + '</span>';

        tableBody.innerHTML += `
            <tr>
                <td>${fmtLogTime(log.ts)}</td>
                <td class="temp">${f(log.temperature)}</td>
                <td class="hum">${f(log.humidity)}</td>
                <td class="soil">${f(saneSoil(log.soil), 0)}</td>
                <td class="vpd">${f(log.vpd)}</td>
                <td class="et0">${f(log.et0)}</td>
                <td class="tds">${f(log.tds, 0)}</td>
                <td>${pill}</td>
            </tr>`;
    });
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
        const res = await fetch(`${DB_URL}/zones/${zoneId()}/sensor_logs.json?orderBy="$key"&limitToLast=20&auth=${token}`);
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
    document.getElementById('state-mist').textContent = s.includes("MISTING") ? "AKTIF" : "mati";
    document.getElementById('state-blower').textContent = s.includes("BLOWER") ? "AKTIF" : "mati";
    document.getElementById('state-irigasi').textContent = s.includes("IRIGASI") ? "AKTIF" : "mati";

    const manual = lastData.manual_mode === true;
    document.getElementById('state-auto').textContent = manual ? "off (manual)" : "on (otomatis)";
    document.getElementById('sw-auto').checked = !manual;
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
    if (DEMO_MODE) {
        applyLocalCommand(action, value);
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
        // Auto/switch dimatikan seketika di UI; device mengeksekusi <=5 s lalu
        // mengonfirmasi lewat push berikutnya (<=20 s).
        applyLocalCommand(action, value);
        status.textContent = "Perintah " + action + " terkirim ke " + zoneId() + " (dieksekusi hingga 5 detik)";
    } catch (e) {
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
