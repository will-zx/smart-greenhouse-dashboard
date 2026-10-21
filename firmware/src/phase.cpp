#include "phase.h"
#include "config.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <string.h>

// Setelan fase disimpan di NVS (namespace NVS_NAMESPACE) supaya bertahan
// setelah reboot. Penghitung durasi memakai esp_timer (64-bit, tanpa wrap).
static Preferences prefs;
static bool     s_active = false;
static float    s_vpdTarget = PHASE_DEFAULT_VPD;
static char     s_name[PHASE_NAME_LEN] = PHASE_DEFAULT_NAME;
static uint32_t s_durationSec = PHASE_DEFAULT_DURATION;
static int64_t  s_startUs = 0;

static uint32_t elapsedSec() {
    if (s_startUs == 0) return 0;
    int64_t d = esp_timer_get_time() - s_startUs;
    if (d < 0) return 0;
    uint64_t sec = (uint64_t)d / 1000000ULL;
    return (sec > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)sec;
}

void phaseInit() {
    prefs.begin(NVS_NAMESPACE, false);
    s_active      = prefs.getBool("ph_active", false);
    s_vpdTarget   = prefs.getFloat("ph_vpd", PHASE_DEFAULT_VPD);
    s_durationSec = prefs.getULong("ph_dur", PHASE_DEFAULT_DURATION);
    String n      = prefs.getString("ph_name", PHASE_DEFAULT_NAME);
    strncpy(s_name, n.c_str(), PHASE_NAME_LEN - 1);
    s_name[PHASE_NAME_LEN - 1] = '\0';
    s_startUs = esp_timer_get_time();
}

bool phaseSetVpd(float vpd) {
    // Hanya rentang VPD yang wajar untuk hortikultura yang diterima; target
    // 10 kPa membuat sistem tidak pernah misting sama sekali.
    if (isnan(vpd) || vpd < VPD_TARGET_MIN || vpd > VPD_TARGET_MAX) return false;
    s_vpdTarget = vpd;
    s_active = true;
    prefs.putFloat("ph_vpd", vpd);
    prefs.putBool("ph_active", true);
    return true;
}

void phaseDeactivate() {
    // Mode setpoint/fase dimatikan -> keputusan sepenuhnya kembali ke fuzzy.
    s_active = false;
    prefs.putBool("ph_active", false);
}

void phaseSetName(const char* name) {
    if (!name) return;
    strncpy(s_name, name, PHASE_NAME_LEN - 1);
    s_name[PHASE_NAME_LEN - 1] = '\0';
    prefs.putString("ph_name", s_name);
    // Log Kc ikut berubah supaya bisa dibuktikan dari log port.
    Serial.printf("[fase] fase: %s (Kc %.2f)\n", s_name, phaseKc());
}

void phaseSetDuration(uint32_t seconds) {
    if (seconds > PHASE_MAX_DURATION_SEC) return;   // diamankan di sini juga
    s_durationSec = seconds;
    s_startUs = esp_timer_get_time();   // mulai hitung dari sekarang
    prefs.putULong("ph_dur", seconds);
}

void phaseReset() {
    s_startUs = esp_timer_get_time();
}

bool  phaseActive()    { return s_active; }
float phaseVpdTarget() { return s_vpdTarget; }

// Kc aktif mengikuti NAMA fase yang sedang berjalan (Tabel 2.1 proposal).
// Nama fase diisi admin lewat dashboard ("set_fase"), jadi pemetaannya
// berbasis nama, bukan indeks. Nama yang tidak dikenal -> Kc default
// (Vegetatif) supaya perilaku lama tetap berlaku.
float phaseKc() {
    const char* n = s_name;
    if (strcasestr(n, "awal"))     return KC_AWAL;
    if (strcasestr(n, "vegetatif"))return KC_VEGETATIF;
    if (strcasestr(n, "pembungaan") || strcasestr(n, "bunga")) return KC_PEMBUNGAAN;
    if (strcasestr(n, "pemuliaan") || strcasestr(n, "buah"))  return KC_PEMULIAAN_BUAH;
    if (strcasestr(n, "panen"))    return KC_PANEN;
    return KC_DEFAULT;
}

PhaseState phaseGet() {
    PhaseState st;
    st.active = s_active;
    st.vpdTarget = s_vpdTarget;
    strncpy(st.name, s_name, PHASE_NAME_LEN - 1);
    st.name[PHASE_NAME_LEN - 1] = '\0';
    st.kc = phaseKc();
    st.durationSec = s_durationSec;
    uint32_t el = elapsedSec();
    if (s_durationSec > 0 && el > s_durationSec) el = s_durationSec;
    st.elapsedSec = el;
    st.remainingSec = (s_durationSec > el) ? (s_durationSec - el) : 0;
    return st;
}
