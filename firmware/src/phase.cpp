#include "phase.h"
#include "config.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <string.h>
#include <time.h>

// Setelan fase disimpan di NVS (namespace NVS_NAMESPACE) supaya bertahan
// setelah reboot. Penghitung durasi memakai epoch (bertahan setelah power loss)
// bila jam sudah sinkron; fallback ke esp_timer (uptime) bila belum.
static Preferences prefs;
static SemaphoreHandle_t mux = nullptr;
static bool     s_active = false;
static float    s_vpdTarget = PHASE_DEFAULT_VPD;
static char     s_name[PHASE_NAME_LEN] = PHASE_DEFAULT_NAME;
static uint32_t s_durationSec = PHASE_DEFAULT_DURATION;
static uint32_t s_startEpoch = 0;      // epoch saat fase di-set/reset (0 = belum sync)
static int64_t  s_startUs = 0;

static void lock()   { if (mux) xSemaphoreTake(mux, portMAX_DELAY); }
static void unlock() { if (mux) xSemaphoreGive(mux); }

static uint32_t nowEpoch() {
    time_t now = time(nullptr);
    return (now >= 100000) ? (uint32_t)now : 0;
}

static uint32_t elapsedSec() {
    if (s_startEpoch >= 100000) {
        uint32_t now = nowEpoch();
        if (now >= 100000) {
            // clamp ke UINT32_MAX (now selalu >= epoch secara wajar)
            return (now >= s_startEpoch) ? (now - s_startEpoch) : 0;
        }
    }
    if (s_startUs == 0) return 0;
    int64_t d = esp_timer_get_time() - s_startUs;
    if (d < 0) return 0;
    uint64_t sec = (uint64_t)d / 1000000ULL;
    return (sec > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)sec;
}

// Kc aktif mengikuti NAMA fase yang sedang berjalan (Tabel 2.1 proposal).
// Nama fase diisi admin lewat dashboard ("set_fase"), jadi pemetaannya
// berbasis nama, bukan indeks. Nama yang tidak dikenal -> Kc default
// (Vegetatif) supaya perilaku lama tetap berlaku.
static float phaseKcUnlocked() {
    const char* n = s_name;
    if (strcasestr(n, "awal"))     return KC_AWAL;
    if (strcasestr(n, "vegetatif"))return KC_VEGETATIF;
    if (strcasestr(n, "pembungaan") || strcasestr(n, "bunga")) return KC_PEMBUNGAAN;
    if (strcasestr(n, "pemuliaan") || strcasestr(n, "buah"))  return KC_PEMULIAAN_BUAH;
    if (strcasestr(n, "panen"))    return KC_PANEN;
    return KC_DEFAULT;
}

void phaseInit() {
    if (!mux) mux = xSemaphoreCreateMutex();
    prefs.begin(NVS_NAMESPACE, false);
    s_active      = prefs.getBool("ph_active", false);
    s_vpdTarget   = prefs.getFloat("ph_vpd", PHASE_DEFAULT_VPD);
    s_durationSec = prefs.getULong("ph_dur", PHASE_DEFAULT_DURATION);
    String n      = prefs.getString("ph_name", PHASE_DEFAULT_NAME);
    strncpy(s_name, n.c_str(), PHASE_NAME_LEN - 1);
    s_name[PHASE_NAME_LEN - 1] = '\0';
    // ph_epoch tidak ada di device lama -> 0, elapsed fallback ke uptime.
    s_startEpoch = prefs.getULong("ph_epoch", 0);
    s_startUs = esp_timer_get_time();
}

bool phaseSetVpd(float vpd) {
    // Hanya rentang VPD yang wajar untuk hortikultura yang diterima; target
    // 10 kPa membuat sistem tidak pernah misting sama sekali.
    if (isnan(vpd) || vpd < VPD_TARGET_MIN || vpd > VPD_TARGET_MAX) return false;
    lock();
    s_vpdTarget = vpd;
    s_active = true;
    unlock();
    prefs.putFloat("ph_vpd", vpd);
    prefs.putBool("ph_active", true);
    return true;
}

void phaseDeactivate() {
    // Mode setpoint/fase dimatikan -> keputusan sepenuhnya kembali ke fuzzy.
    lock();
    s_active = false;
    unlock();
    prefs.putBool("ph_active", false);
}

void phaseSetName(const char* name) {
    if (!name) return;
    char stored[PHASE_NAME_LEN];
    lock();
    strncpy(s_name, name, PHASE_NAME_LEN - 1);
    s_name[PHASE_NAME_LEN - 1] = '\0';
    strncpy(stored, s_name, PHASE_NAME_LEN - 1);
    stored[PHASE_NAME_LEN - 1] = '\0';
    unlock();
    prefs.putString("ph_name", stored);
    // Log Kc ikut berubah supaya bisa dibuktikan dari log port.
    Serial.printf("[fase] fase: %s (Kc %.2f)\n", stored, phaseKc());
}

void phaseSetDuration(uint32_t seconds) {
    if (seconds > PHASE_MAX_DURATION_SEC) return;   // diamankan di sini juga
    uint32_t startEpoch = nowEpoch();
    lock();
    s_durationSec = seconds;
    s_startUs = esp_timer_get_time();   // mulai hitung dari sekarang
    s_startEpoch = startEpoch;
    unlock();
    prefs.putULong("ph_dur", seconds);
    prefs.putULong("ph_epoch", startEpoch);
}

void phaseReset() {
    uint32_t startEpoch = nowEpoch();
    lock();
    s_startUs = esp_timer_get_time();
    s_startEpoch = startEpoch;
    unlock();
    prefs.putULong("ph_epoch", startEpoch);
}

bool phaseActive() {
    lock();
    bool active = s_active;
    unlock();
    return active;
}

float phaseVpdTarget() {
    lock();
    float target = s_vpdTarget;
    unlock();
    return target;
}

float phaseKc() {
    lock();
    float kc = phaseKcUnlocked();
    unlock();
    return kc;
}

PhaseState phaseGet() {
    PhaseState st;
    lock();
    st.active = s_active;
    st.vpdTarget = s_vpdTarget;
    strncpy(st.name, s_name, PHASE_NAME_LEN - 1);
    st.name[PHASE_NAME_LEN - 1] = '\0';
    st.kc = phaseKcUnlocked();
    st.durationSec = s_durationSec;
    uint32_t el = elapsedSec();
    if (s_durationSec > 0 && el > s_durationSec) el = s_durationSec;
    st.elapsedSec = el;
    st.remainingSec = (s_durationSec > el) ? (s_durationSec - el) : 0;
    unlock();
    return st;
}
