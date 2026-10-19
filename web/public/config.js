const DB_URL = "https://smart-greenhouse-e214c-default-rtdb.asia-southeast1.firebasedatabase.app";

// Greenhouse kecil yang dikelola sistem. Satu entri per unit ESP32;
// `id` HARUS sama dengan DEVICE_ID di firmware/src/secrets.h unit tersebut.
// Nama di sini hanya default — user bisa menamainya sendiri lewat dashboard
// (tersimpan di RTDB labels/{id}, menang atas `name`).
const ZONES = [
    { id: "esp32-gh-01", name: "Greenhouse 1" }
];

// Email yang boleh honed control aktuator & set label di dashboard.
// Aturan sama ditegakkan di RTDB rules — ini hanya menyembunyikan UI.
const ADMIN_EMAIL = "mr.pman@mail.unnes.ac.id";

const firebaseConfig = {
    apiKey: "AIzaSyC1ox01jTx12i9-jBFZ3JaYVgnBfcYOjA0",
    projectId: "smart-greenhouse-e214c",
    authDomain: "smart-greenhouse-e214c.firebaseapp.com"
};
