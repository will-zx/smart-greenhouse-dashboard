let auth = null;
let currentZone = 0;      // index ke ZONES
let labels = {};          // { deviceId: "Nama tanaman" } dari RTDB
let isAdmin = false;
let zoneTimer = null;
let logData = [];         // log terakhir yang sudah difilter (untuk render tabel)
let logFilter = '';       // teks pencarian log
let logLimit = 20;        // berapa baris yang diambil dari RTDB (dropdown "Show N entries")

// Angka sensor dibaca ADC 12-bit lalu jadi float: 1.421642 kPa / 5.4894 mm tidak
// pernah berarti apa-apa bagi pengguna. 2 desimal cukup, TANAH/TDS dibulatkan
// karena hanya dipakai sebagai persentase/ppm bulat.
const fmtNum = (v, d = 2) => (typeof v === 'number' && isFinite(v)) ? v.toFixed(d) : '-';

// Dashboard terbuka untuk semua orang: data sensor dibaca tanpa login, dan
// hanya kontrol aktuator / ganti nama greenhouse yang butuh akun admin.
// Tamu -> klik avatar -> popup akun -> item "Login".
document.addEventListener('DOMContentLoaded', () => {
    const login = document.getElementById('login');
    login.hidden = true;
    document.getElementById('app').classList.remove('hidden');

    // config.js menyediakan firebaseConfig, DB_URL, ZONES, ADMIN_EMAIL.
    // Persistence default = local: sesi admin tersimpan di browser.
    firebase.initializeApp(firebaseConfig);
    auth = firebase.auth();

    setIdentity(null);            // mulai sebagai tamu
    startDashboard();             // polling jalan untuk tamu (tanpa token)

    // onAuthStateChanged juga menangani login/keluar saat aplikasi sudah terbuka.
    auth.onAuthStateChanged(user => setIdentity(user));

    document.getElementById('loginForm').addEventListener('submit', async (e) => {
        e.preventDefault();
        const err = document.getElementById('loginError');
        err.textContent = '';
        const email = document.getElementById('loginEmail').value.trim();
        const pass = document.getElementById('loginPass').value;
        try {
            await auth.signInWithEmailAndPassword(email, pass);
            login.hidden = true;
            err.textContent = '';
        } catch (e2) {
            err.textContent = e2.message;
        }
    });

    // Menutup modal login: tombol, klik overlay, atau Escape.
    document.getElementById('loginClose').addEventListener('click', () => {
        login.hidden = true;
        document.getElementById('loginError').textContent = '';
    });
    login.addEventListener('click', e => { if (e.target === login) login.hidden = true; });

    // Popup akun: Tamu melihat item "Login", admin melihat "Keluar".
    const chip = document.getElementById('userChip');
    const menu = document.getElementById('userMenu');
    chip.addEventListener('click', e => {
        const open = menu.hidden;
        menu.hidden = !open;
        chip.setAttribute('aria-expanded', String(open));
        e.stopPropagation();
    });
    document.getElementById('loginItem').addEventListener('click', () => {
        menu.hidden = true;
        chip.setAttribute('aria-expanded', 'false');
        document.getElementById('login').hidden = false;
        document.getElementById('loginEmail').focus();
    });
    document.addEventListener('click', () => {
        menu.hidden = true;
        chip.setAttribute('aria-expanded', 'false');
        closeZoneMenu();
    });
    document.addEventListener('keydown', e => {
        if (e.key === 'Escape') { login.hidden = true; menu.hidden = true; closeZoneMenu(); }
    });

    // Navigasi: 2 view (Beranda / Log Sensor) tanpa router. Satu tombol di
    // topbar toggle antara keduanya; labelnya ikut berubah.
    document.getElementById('logBtn').addEventListener('click', () => {
        switchView(currentView === 'log' ? 'beranda' : 'log');
    });

    // Greenhouse aktif + daftar unit.
    document.getElementById('zoneBtn').addEventListener('click', e => {
        e.stopPropagation();
        const m = document.getElementById('zoneMenu');
        if (m.hidden) openZoneMenu(); else closeZoneMenu();
    });
    // Klik di luar popup menutupnya (listener global "click" di atas).
    document.getElementById('zoneMenu').addEventListener('click', e => e.stopPropagation());

    // Ikon Refresh berputar selama pemuatan, di kedua halaman (Beranda & Log).
    // Dipanggil lewat listener (bukan onclick inline) supaya kita tahu kapan
    // fetch selesai.
    document.querySelectorAll('.refresh').forEach(btn => {
        btn.addEventListener('click', async () => {
            btn.classList.add('spinning');
            const t0 = Date.now();
            try {
                await fetchDataLogs();
            } finally {
                // Putar minimal ~0.5 s supaya perputarannya terlihat, walau
                // fetch selesai secepatnya.
                setTimeout(() => btn.classList.remove('spinning'), Math.max(0, 500 - (Date.now() - t0)));
            }
        });
    });

    // Search + "Show N entries" hanya relevan di halaman Log.
    document.getElementById('logSearch').addEventListener('input', e => {
        logFilter = e.target.value.trim().toLowerCase();
        renderLogTable();
    });
    document.getElementById('showEntries').addEventListener('change', e => {
        logLimit = parseInt(e.target.value, 10) || 20;
        fetchDataLogs();   // jumlah baris berubah -> ambil ulang dari RTDB
    });
});

// Ikon incognito untuk tamu (topi + kacamata), Login/keluar tidak bergantian.
const INCOGNITO_ICON = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M2.6 13.2c4.6-1.5 14.2-1.5 18.8 0"/><path d="M7.8 13 9.4 7.6h5.2L16.2 13"/><path d="M5.2 16.6 5 15.2"/><path d="M18.8 16.6 19 15.2"/><circle cx="7.4" cy="18.2" r="2.7"/><circle cx="16.6" cy="18.2" r="2.7"/><path d="M10.1 18.2h3.8"/></svg>';

// Menentukan tampilan identitas di topbar & popup akun.
// user = null  -> Tamu (ikon incognito, tanpa kontrol)
// user != null -> Admin (kalau emailnya cocok) atau User (hanya lihat)
function setIdentity(user) {
    const email = user && user.email ? user.email : null;
    const admin = !!email && email === ADMIN_EMAIL;
    isAdmin = admin;

    const avatar = document.getElementById('userAvatar');
    const nameEl = document.getElementById('userName');
    const roleEl = document.getElementById('userRole');
    const menuMail = document.getElementById('userEmail');
    const badge = document.getElementById('roleBadge');

    if (email) {
        avatar.textContent = email.charAt(0).toUpperCase();
        nameEl.textContent = email.split('@')[0];
        roleEl.textContent = admin ? 'Admin' : 'User';
        menuMail.textContent = email;
        badge.textContent = admin ? 'Admin' : 'User';
        badge.classList.toggle('admin', admin);
    } else {
        avatar.innerHTML = INCOGNITO_ICON;
        nameEl.textContent = 'Tamu';
        roleEl.textContent = 'Hanya lihat';
        menuMail.textContent = 'Tamu';
        badge.textContent = 'Hanya lihat';
        badge.classList.remove('admin');
    }

    document.getElementById('loginItem').hidden = !!email;
    document.getElementById('logoutItem').hidden = !email;
    applyRoleView();
    buildZoneSelect();   // ikon pensil ikut menyesuaikan peran
}

// Kartu Kontrol tetap terlihat untuk semua orang (=status aktuator berguna
// untuk dipantau), tetapi sakelarnya disembunyikan bila belum login admin.
// Ikon pensil di popup greenhouse tetap khusus admin.
function applyRoleView() {
    document.body.classList.toggle('read-only', !isAdmin);
    document.getElementById('ctrlNote').textContent =
        isAdmin ? 'Kontrol remot via Firebase RTDB' : 'Hanya lihat — login untuk kontrol manual';
}

// Navigasi: cukup tampilkan satu section. Tombol Log Sensor jadi tombol
// "kembali" ketika Log sedang terbuka.
const VIEW_TITLES = { beranda: 'Beranda', log: 'Log Sensor' };
let currentView = 'beranda';

function switchView(name) {
    currentView = name;
    Object.keys(VIEW_TITLES).forEach(v => {
        document.getElementById('view-' + v).classList.toggle('hidden', v !== name);
    });
    const btn = document.getElementById('logBtn');
    btn.setAttribute('aria-expanded', String(name === 'log'));
    document.getElementById('logBtnLabel').textContent =
        name === 'log' ? 'Beranda' : 'Log Sensor';
}

function signOut() {
    auth.signOut().catch(e => console.error("Gagal logout:", e));
}

function zoneId() {
    return ZONES[currentZone].id;
}

function zoneName() {
    return labels[zoneId()] || ZONES[currentZone].name;
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
            { label: 'Suhu (°C)', data: [], borderColor: cssVar('--danger'), pointBackgroundColor: cssVar('--danger'), pointRadius: 3, pointHoverRadius: 5, borderWidth: 2, tension: 0, yAxisID: 'y' },
            { label: 'Kelembapan (%)', data: [], borderColor: cssVar('--accent'), pointBackgroundColor: cssVar('--accent'), pointRadius: 3, pointHoverRadius: 5, borderWidth: 2, tension: 0, yAxisID: 'y1' }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'index', intersect: false },
        // Legenda diletakkan di bawah-tengah seperti grafik referensi: lebih
        // rapi dan tidak memakan ruang di dekat judul kartu.
        plugins: {
            legend: {
                position: 'bottom',
                labels: { usePointStyle: true, pointStyle: 'circle', boxWidth: 7, boxHeight: 7, padding: 18, font: { weight: 600 } }
            },
            tooltip: {
                padding: 10, boxWidth: 8, boxHeight: 8, usePointStyle: true,
                callbacks: {
                    // Sumbu X sudah menunjuk waktunya, jadi cukup tampilkan
                    // "label + nilai + satuan" - tidak perlu mengulang label.
                    label: c => ' ' + c.dataset.label.replace(/\s*\(.*\)/, '') + ': ' +
                        c.parsed.y.toFixed(2) + ' ' + (c.dataset.yAxisID === 'y' ? '°C' : '%')
                }
            }
        },
        scales: {
            // suggestedMin/Max, bukan min/max: rentang tetap supaya kemiringan
            // garis jujur. Tanpa ini Chart.js auto-zoom dan 0.5°C terlihat
            // seperti lonjakan besar.
            x: {
                grid: { color: cssVar('--card-2') },
                ticks: { color: cssVar('--muted'), maxTicksLimit: 8, maxRotation: 0, autoSkipPadding: 12 }
            },
            y: {
                type: 'linear', position: 'left', suggestedMin: 20, suggestedMax: 40,
                grid: { color: cssVar('--card-2') },
                border: { display: false },
                ticks: { color: cssVar('--danger'), maxTicksLimit: 6 },
                title: { display: true, text: 'Suhu (°C)', color: cssVar('--danger'), font: { weight: 600 } }
            },
            y1: {
                type: 'linear', position: 'right', suggestedMin: 0, suggestedMax: 100,
                grid: { drawOnChartArea: false },
                border: { display: false },
                ticks: { color: cssVar('--accent'), maxTicksLimit: 6 },
                title: { display: true, text: 'Kelembapan (%)', color: cssVar('--accent'), font: { weight: 600 } }
            }
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
            ${isAdmin ? `<input class="z-input" type="text" hidden aria-label="Nama greenhouse ${z.id}">
                <button type="button" class="z-edit" title="Ganti nama greenhouse" aria-label="Ganti nama greenhouse"><svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 20h9"/><path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z"/></svg></button>` : ''}`;
        row.querySelector('.z-name').textContent = labels[z.id] || z.name;
        row.querySelector('.z-id').textContent = z.id;
        row.querySelector('.z-pick').addEventListener('click', () => { switchZone(i); closeZoneMenu(); });
        const edit = row.querySelector('.z-edit');
        if (edit) edit.addEventListener('click', () => startRename(i, row));
        list.appendChild(row);
    });
    // Nama unit yang sedang dipantau, supaya jelas data di halaman ini milik
    // greenhouse yang mana (bukan jumlah unit). Namesaknya menyambung di judul
    // card: "Grafik Tren Sensor Lombok", bukan chip terpisah.
    document.querySelectorAll('.head-zone').forEach(el => el.textContent = zoneName());
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

// Pembacaan data_sensor boleh tanpa login (RTDB rules .read: true), jadi
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

async function loadLabels() {
    try {
        const res = await fetch(await apiRead('labels.json'));
        if (!res.ok) return;
        labels = (await res.json()) || {};
    } catch (e) {
        console.warn("Label tidak terbaca:", e.message);
    }
}

// Ganti nama greenhouse dari dalam daftar unit.
// Memakai input inline, BUKAN prompt(): prompt() diblokir di beberapa konteks
// (iframe, embedded browser) sehingga tombolnya jadi tampak tidak berrespons.
// Enter = simpan, Esc atau klik ke luar = batal.
function startRename(i, row) {
    const input = row.querySelector('.z-input');
    row.querySelector('.z-text').hidden = true;
    input.hidden = false;
    input.value = labels[ZONES[i].id] || ZONES[i].name;
    input.focus();
    input.select();

    let done = false;
    const finish = save => {
        if (done) return;
        done = true;
        const val = input.value.trim();
        row.querySelector('.z-text').hidden = false;
        input.hidden = true;
        if (save && val) saveRename(i, val);
    };
    input.onkeydown = e => {
        if (e.key === 'Enter') { e.preventDefault(); finish(true); }
        else if (e.key === 'Escape') { e.preventDefault(); finish(false); }
    };
    input.onblur = () => finish(true);
}

function saveRename(i, val) {
    const id = ZONES[i].id;
    sendLabel(val, id).then(ok => {
        if (!ok) showZoneMsg("Gagal menyimpan nama: akses ditolak");
        else {
            labels[id] = val;
            showZoneMsg("Nama greenhouse disimpan", true);
            if (i === currentZone) setOnline(
                !document.getElementById('zoneBtn').classList.contains('is-off'),
                Math.floor(Date.now() / 1000) - (lastData ? lastData.ts : 0));
        }
        buildZoneSelect();
    });
}

function showZoneMsg(text, ok = false) {
    const el = document.getElementById('zoneMsg');
    if (!el) return;
    el.textContent = text;
    el.classList.toggle('ok', ok);
    el.hidden = false;
    clearTimeout(showZoneMsg.timer);
    showZoneMsg.timer = setTimeout(() => el.hidden = true, 3000);
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

function renderLogs(data) {
    document.getElementById('mv-temp').textContent = fmtNum(lastData.temperature);
    document.getElementById('mv-hum').textContent = fmtNum(lastData.humidity);
    document.getElementById('mv-vpd').textContent = fmtNum(lastData.vpd);
    document.getElementById('mv-et0').textContent = fmtNum(lastData.et0);
    document.getElementById('mv-tds').textContent = fmtNum(lastData.tds, 0);
    document.getElementById('mv-soil').textContent = fmtNum(saneSoil(lastData.soil), 0);

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
                <td class="num">${fmtNum(log.temperature)}</td>
                <td class="num">${fmtNum(log.humidity)}</td>
                <td class="num">${fmtNum(saneSoil(log.soil), 0)}</td>
                <td class="num">${fmtNum(log.vpd)}</td>
                <td class="num">${fmtNum(log.et0)}</td>
                <td class="num">${fmtNum(log.tds, 0)}</td>
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
    // Format id-ID memakai titik ("18.26.15"); di sumbu grafik titik dua lebih
    // mudah dibaca sebagai pemisah waktu.
    return new Date(ts * 1000).toLocaleTimeString('id-ID', { hour: '2-digit', minute: '2-digit', second: '2-digit' }).replace(/\./g, ':');
}

function fmtLogTime(ts) {
    if (typeof ts !== 'number' || ts < 100000) return '-';
    // Format id-ID menulis waktu dengan titik ("11/10/2026 18.26.15"); bagian
    // jamnya diubah ke titik dua supaya sama dengan label sumbu grafik.
    return new Date(ts * 1000).toLocaleString('id-ID')
        .replace(/(\d{2})\.(\d{2})\.(\d{2})\s*$/, '$1:$2:$3');
}

async function fetchDataLogs() {
    try {
        const path = `zones/${zoneId()}/sensor_logs.json?orderBy="$key"&limitToLast=${logLimit}`;
        const res = await fetch(await apiRead(path));
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const obj = await res.json();
        if (!obj) throw new Error('kosong');

        // Urut berdasarkan ts, bukan kunci push-ID. Kunci RTDB urut saat apa yang
        // ditulis, tapi record lama yang diputar ulang dari backlog MicroSD tetap
        // dapat kunci baru - jadi kunci != waktu. Ternsorted ts juga membetulkan
        // kolom waktu di tabel (terbaru di atas) dan sumbu X grafik.
        const data = Object.values(obj)
            .sort((a, b) => (a.ts || 0) - (b.ts || 0));

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

// Umur data dalam bahasa sehari-hari: detik → menit → jam → hari, dengan sisa
// unit yang lebih kecil ("2 hari 15 jam"). Satu fungsi supaya label "diam ..."
// di topbar dan "Data terakhir ..." di grafik tidak pernah beda format.
// Floor, bukan round: 1439 menit harus tetap "23 jam 59 menit", bukan "24 jam".
function humanAge(sec) {
    if (sec < 60) return sec + " detik";
    const m = Math.floor(sec / 60);
    if (m < 60) return m + " menit";
    const sisaM = m % 60;
    const h = Math.floor(m / 60);
    if (h < 24) return h + (sisaM ? " jam " + sisaM + " menit" : " jam");
    const sisaH = h % 24;
    return Math.floor(h / 24) + " hari" + (sisaH ? " " + sisaH + " jam" : "");
}

// Umur data terakhir di bawah grafik: pengguna bisa membedakan "sensor diam
// 40 detik" dari "dashboardnya yang macet".
function updateFreshness(age) {
    const el = document.getElementById('freshness');
    if (typeof age !== 'number') { el.textContent = ''; return; }
    el.textContent = "Data terakhir " + humanAge(age) + " lalu";
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

// Isi pill greenhouse hanya kapan data terakhir masuk ("2 jam 5 menit"),
// tanpa nama unit. Status ditandai warna: hijau = segar, kuning = basi kurang
// dari sehari, merah = basi lebih dari sehari atau gagal membaca data. Teks
// lengkapnya juga ditulis ke title + aria-label supaya hover dan pembaca layar
// bisa membacanya. Durasi yang sama tertera di bawah grafik.
function setOnline(ok, age = 0) {
    const btn = document.getElementById('zoneBtn');
    const text = document.getElementById('zoneStatus');
    btn.classList.remove('is-ok', 'is-stale', 'is-off');
    let ket;
    if (!ok) {
        btn.classList.add('is-off');
        text.textContent = 'gagal baca';
        ket = 'Gagal membaca data greenhouse';
    } else if (age > 86400) {
        btn.classList.add('is-off');
        text.textContent = humanAge(age);
        ket = 'Data terakhir masuk ' + humanAge(age) + ' lalu';
    } else if (age > 60) {
        btn.classList.add('is-stale');
        text.textContent = humanAge(age);
        ket = 'Data terakhir masuk ' + humanAge(age) + ' lalu';
    } else {
        btn.classList.add('is-ok');
        text.textContent = humanAge(age);
        ket = 'Data terbaru ' + humanAge(age) + ' lalu';
    }
    btn.title = ket;
    btn.setAttribute('aria-label', ket);
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
    // Tamu tidak punya UI kontrol, tapi tetap dijaga agar tidak bisa menulis.
    if (!auth || !auth.currentUser) {
        status.textContent = "Kamu sedang masuk sebagai tamu — login untuk kontrol";
        setTimeout(() => status.textContent = "", 4000);
        return;
    }
    status.textContent = "Mengirim perintah...";
    // Terapkan ke tampilan SEBELUM menunggu jaringan. Browser sudah mencentang
    // checkbox seketika saat diklik; kalau teks menunggu balasan RTDB (ratusan
    // ms), switch dan tulisan ON/OFF terlihat bertentangan.
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
