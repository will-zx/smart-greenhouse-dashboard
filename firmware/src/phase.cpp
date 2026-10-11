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

void phaseSetVpd(float vpd) {
    if (vpd < 0.0f || vpd > 10.0f) return;   // sanity: VPD wajar 0-10 kPa
    s_vpdTarget = vpd;
    s_active = true;
    prefs.putFloat("ph_vpd", vpd);
    prefs.putBool("ph_active", true);
}

void phaseSetName(const char* name) {
    if (!name) return;
    strncpy(s_name, name, PHASE_NAME_LEN - 1);
    s_name[PHASE_NAME_LEN - 1] = '\0';
    prefs.putString("ph_name", s_name);
}

void phaseSetDuration(uint32_t seconds) {
    s_durationSec = seconds;
    s_startUs = esp_timer_get_time();   // mulai hitung dari sekarang
    prefs.putULong("ph_dur", seconds);
}

void phaseReset() {
    s_startUs = esp_timer_get_time();
}

bool  phaseActive()    { return s_active; }
float phaseVpdTarget() { return s_vpdTarget; }

PhaseState phaseGet() {
    PhaseState st;
    st.active = s_active;
    st.vpdTarget = s_vpdTarget;
    strncpy(st.name, s_name, PHASE_NAME_LEN - 1);
    st.name[PHASE_NAME_LEN - 1] = '\0';
    st.durationSec = s_durationSec;
    uint32_t el = elapsedSec();
    if (s_durationSec > 0 && el > s_durationSec) el = s_durationSec;
    st.elapsedSec = el;
    st.remainingSec = (s_durationSec > el) ? (s_durationSec - el) : 0;
    return st;
}

bool phaseElapsed() {
    return s_durationSec > 0 && elapsedSec() >= s_durationSec;
}
