// true = mode demo (data dummy, tanpa Firebase/auth, cocok untuk presentasi).
// false = mode produksi (Firebase Auth + RTDB live).
const DEMO_MODE = false;

document.addEventListener('DOMContentLoaded', () => {
    if (DEMO_MODE) {
        document.getElementById('app').classList.remove('hidden');
        document.getElementById('login').classList.add('hidden');
        document.getElementById('onlineBadge').classList.remove('on', 'off');
        document.getElementById('onlineBadge').classList.add('on');
        document.getElementById('onlineText').textContent = 'Demo Mode';
        loadDemoData();
        return;
    }

    // Firebase Auth: tanda tangan Email/Password sebelum data dimuat.
    // config.js menyediakan firebaseConfig, DB_URL, DEVICE_ID (jangan di-commit).
    firebase.initializeApp(firebaseConfig);
    const auth = firebase.auth();

    auth.onAuthStateChanged(user => {
        const app = document.getElementById('app');
        const login = document.getElementById('login');
        if (user) {
            app.classList.remove('hidden');
            login.classList.add('hidden');
            document.getElementById('userEmail').textContent = user.email;
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

function signOut() {
    if (DEMO_MODE) return;
    auth.signOut();
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
        scales: {
            x: { grid: { color: '#eef2f7' }, ticks: { color: '#64748b' } },
            y: { type: 'linear', position: 'left', grid: { color: '#eef2f7' }, ticks: { color: '#64748b' }, title: { display: true, text: 'Suhu (°C)', color: '#c62828', font: { weight: 600 } } },
            y1: { type: 'linear', position: 'right', grid: { drawOnChartArea: false }, ticks: { color: '#64748b' }, title: { display: true, text: 'Kelembapan (%)', color: '#0277bd', font: { weight: 600 } } }
        }
    }
});

let lastData = null;

function startDashboard() {
    fetchDataLogs();
    setInterval(fetchDataLogs, 10000);
}

function loadDemoData() {
    const now = Math.floor(Date.now() / 1000);
    const logs = [];
    const base = [
        [29.1, 62, 38, 1.42, 5.2, "Normal"],
        [29.8, 58, 33, 1.58, 5.5, "Normal MISTING"],
        [30.4, 55, 30, 1.72, 5.8, "Normal MISTING"],
        [30.1, 59, 28, 1.66, 5.6, "Normal BLOWER"],
        [28.7, 66, 25, 1.35, 5.1, "Normal IRIGASI"],
        [27.9, 70, 41, 1.18, 4.9, "Normal"],
        [27.4, 74, 44, 1.04, 4.6, "Normal"],
        [26.8, 78, 47, 0.88, 4.3, "Normal BLOWER"]
    ];
    base.forEach((b, i) => {
        logs.push({
            ts: now - (base.length - 1 - i) * 20,
            temperature: b[0],
            humidity: b[1],
            soil: b[2],
            vpd: b[3],
            et0: b[4],
            status_sistem: b[5],
            manual_mode: false
        });
    });

    lastData = logs[logs.length - 1];
    renderLogs(logs);
    updateActuatorStates();
    const badge = document.getElementById('onlineText');
    badge.textContent = 'Demo Mode';
    document.getElementById('onlineBadge').classList.remove('off');
    document.getElementById('onlineBadge').classList.add('on');
}

function renderLogs(data) {
    document.getElementById('mv-temp').textContent = lastData.temperature ?? '-';
    document.getElementById('mv-hum').textContent = lastData.humidity ?? '-';
    document.getElementById('mv-vpd').textContent = lastData.vpd ?? '-';
    document.getElementById('mv-et0').textContent = lastData.et0 ?? '-';
    document.getElementById('mv-soil').textContent = lastData.soil ?? '-';

    sensorChart.data.labels = data.map(fmtLogTime);
    sensorChart.data.datasets[0].data = data.map(l => l.temperature);
    sensorChart.data.datasets[1].data = data.map(l => l.humidity);
    sensorChart.update();

    const tableBody = document.getElementById('logTableBody');
    tableBody.innerHTML = '';
    [...data].reverse().forEach(log => {
        const s = log.status_sistem || "";
        let pill = '<span class="pill idle">' + s + '</span>';
        if (s.includes("MISTING")) pill = '<span class="pill mist">' + s + '</span>';
        if (s.includes("BLOWER")) pill = '<span class="pill blower">' + s + '</span>';
        if (s.includes("IRIGASI")) pill = '<span class="pill irigasi">' + s + '</span>';

        tableBody.innerHTML += `
            <tr>
                <td>${fmtLogTime(log.ts)}</td>
                <td class="temp">${log.temperature ?? '-'}</td>
                <td class="hum">${log.humidity ?? '-'}</td>
                <td class="soil">${log.soil ?? '-'}</td>
                <td class="vpd">${log.vpd ?? '-'}</td>
                <td class="et0">${log.et0 ?? '-'}</td>
                <td>${pill}</td>
            </tr>`;
    });
}

function fmtLogTime(ts) {
    if (typeof ts !== 'number' || ts < 100000) return '-';
    return new Date(ts * 1000).toLocaleString('id-ID');
}

async function fetchDataLogs() {
    try {
        const token = await auth.currentUser.getIdToken();
        const res = await fetch(`${DB_URL}/sensor_logs.json?orderBy="$key"&limitToLast=20&auth=${token}`);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const obj = await res.json();
        if (!obj) throw new Error('kosong');

        const data = Object.entries(obj)
            .sort((a, b) => a[0].localeCompare(b[0]))
            .map(([, v]) => v);

        lastData = data[data.length - 1];
        if (!lastData) throw new Error('kosong');

        renderLogs(data);
        setOnline(true);
    } catch (err) {
        console.error("Gagal memuat data:", err.message);
        setOnline(false);
    }
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

function setOnline(ok) {
    const badge = document.getElementById('onlineBadge');
    const text = document.getElementById('onlineText');
    badge.classList.remove('on', 'off');
    badge.classList.add(ok ? 'on' : 'off');
    text.textContent = ok
        ? (lastData ? 'Online - ' + new Date().toLocaleTimeString('id-ID') : 'Online')
        : 'Offline - cek ESP32/DB';
}

async function sendCommand(action, value) {
    const status = document.getElementById('cmdStatus');
    status.textContent = "Mengirim perintah...";
    if (DEMO_MODE) {
        let msg;
        if (action === "auto") {
            msg = "Demo: mode otomatis (semua kembalikan ke fuzzy)";
            lastData.manual_mode = false;
        } else {
            msg = "Demo: " + action + " " + (value ? "ON" : "OFF");
            lastData.manual_mode = true;
            const kw = action === "irigasi" ? "IRIGASI" : (action === "blower" ? "BLOWER" : "MISTING");
            if (value) {
                if (!lastData.status_sistem.includes(kw)) lastData.status_sistem += " " + kw;
            } else {
                lastData.status_sistem = lastData.status_sistem.split(" " + kw).join("");
            }
        }
        status.textContent = msg;
        setTimeout(() => status.textContent = "", 4000);
        updateActuatorStates();
        return;
    }
    try {
        const token = await auth.currentUser.getIdToken();
        const res = await fetch(`${DB_URL}/commands/${DEVICE_ID}.json?auth=${token}`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ action: action, value: value })
        });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        status.textContent = "Perintah " + action + " terkirim (dieksekusi hingga 5 detik)";
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
