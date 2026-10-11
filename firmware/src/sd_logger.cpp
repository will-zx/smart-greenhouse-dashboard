#include "sd_logger.h"
#include "firebase_db.h"
#include <SD.h>
#include <freertos/semphr.h>

static const char* LOG_FILE = "/log.csv";
static const char* BACKLOG_FILE = "/backlog.txt";
static bool sdReady = false;

// File CSV dibuka sekali lalu ditahan — buka/tutup per baris wear kartu SD
// dan lambat. flush berkala supaya data tetap ada kalau listrik mati mendadak.
static File logFile;
static int linesSinceFlush = 0;
static unsigned long lastBacklogFlush = 0;

// SD diakses dua task (kontrol: log CSV; jaringan: backlog) — satu mutex.
static SemaphoreHandle_t mux = nullptr;

static void lock()   { if (mux) xSemaphoreTake(mux, portMAX_DELAY); }
static void unlock() { if (mux) xSemaphoreGive(mux); }

void initSdLogger() {
    mux = xSemaphoreCreateMutex();
    if (SD.begin(SD_CS)) {
        sdReady = true;
        logFile = SD.open(LOG_FILE, FILE_APPEND);
        Serial.println("[sd] MicroSD siap (backup lokal aktif)");
    } else {
        Serial.println("[sd] MicroSD tidak terdeteksi - lanjut tanpa backup");
    }
}

void sdLogRecord(const char* line) {
    if (!sdReady || !logFile) return;
    lock();
    logFile.println(line);
    if (++linesSinceFlush >= SD_LOG_FLUSH_EVERY) {
        logFile.flush();
        linesSinceFlush = 0;
    }
    unlock();
}

void sdQueueBacklog(const char* body) {
    if (!sdReady) return;
    lock();
    File f = SD.open(BACKLOG_FILE, FILE_READ);
    int lines = 0;
    if (f) {
        while (f.available()) { f.readStringUntil('\n'); lines++; }
        f.close();
    }
    if (lines >= SD_BACKLOG_MAX) {
        unlock();
        Serial.println("[sd] backlog penuh, data terbaru tidak diantrekan");
        return;
    }
    File w = SD.open(BACKLOG_FILE, FILE_APPEND);
    if (w) {
        w.println(body);
        w.close();
        Serial.printf("[sd] push gagal, antrekan backlog (%d)\n", lines + 1);
    }
    unlock();
}

void sdFlushBacklog() {
    if (!sdReady) return;
    // Tulis ulang seluruh file setiap flush — jangan terlalu sering.
    if (millis() - lastBacklogFlush < SD_FLUSH_MS) return;
    lastBacklogFlush = millis();

    // Ambil isi backlog + kosongkan di bawah kunci, lalu LEPAS kunci saat
    // mengirim (HTTP/TLS bisa detik-an; jangan menahan controlTask yang
    // menulis CSV). ponytail: crash di antara kosongkan & tulis sisa =
    // backlog hilang; pindah ke rename dua file kalau itu jadi masalah.
    String all;
    lock();
    File f = SD.open(BACKLOG_FILE, FILE_READ);
    if (f) {
        all = f.readString();
        f.close();
    }
    if (all.length() > 0) {
        File t = SD.open(BACKLOG_FILE, FILE_WRITE);   // truncate
        if (t) t.close();
    }
    unlock();

    if (all.length() == 0) return;

    // Kirim line per line (limit per panggilan); yang gagal/tak sempat tetap disimpan.
    String rest;
    int start = 0, idx, pushed = 0;
    while ((idx = all.indexOf('\n', start)) >= 0) {
        String line = all.substring(start, idx);
        start = idx + 1;
        line.trim();
        if (line.length() == 0) continue;
        if (pushed < SD_BACKLOG_FLUSH_PER_CALL && httpPostSensorLogs(line)) {
            pushed++;
        } else {
            rest += line + "\n";
        }
    }

    if (rest.length() > 0) {
        lock();
        File w = SD.open(BACKLOG_FILE, FILE_APPEND);
        if (w) {
            w.print(rest);
            w.close();
        }
        unlock();
    }
    if (pushed > 0) Serial.printf("[sd] backlog terkirim: %d\n", pushed);
}
