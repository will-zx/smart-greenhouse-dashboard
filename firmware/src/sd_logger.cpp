#include "sd_logger.h"
#include "firebase_db.h"
#include <SD.h>

static const char* LOG_FILE = "/log.csv";
static const char* BACKLOG_FILE = "/backlog.txt";
static bool sdReady = false;

// File CSV dibuka sekali lalu ditahan — buka/tutup per baris wear kartu SD
// dan lambat. flush berkala supaya data tetap ada kalau listrik mati mendadak.
static File logFile;
static int linesSinceFlush = 0;
static unsigned long lastBacklogFlush = 0;

void initSdLogger() {
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
    logFile.println(line);
    if (++linesSinceFlush >= SD_LOG_FLUSH_EVERY) {
        logFile.flush();
        linesSinceFlush = 0;
    }
}

void sdQueueBacklog(const char* body) {
    if (!sdReady) return;
    File f = SD.open(BACKLOG_FILE, FILE_READ);
    int lines = 0;
    if (f) {
        while (f.available()) { f.readStringUntil('\n'); lines++; }
        f.close();
    }
    if (lines >= SD_BACKLOG_MAX) {
        Serial.println("[sd] backlog penuh, data terbaru tidak diantrekan");
        return;
    }
    File w = SD.open(BACKLOG_FILE, FILE_APPEND);
    if (w) {
        w.println(body);
        w.close();
        Serial.printf("[sd] push gagal, antrekan backlog (%d)\n", lines + 1);
    }
}

void sdFlushBacklog() {
    if (!sdReady) return;
    // Tulis ulang seluruh file setiap flush — jangan terlalu sering.
    if (millis() - lastBacklogFlush < SD_FLUSH_MS) return;
    lastBacklogFlush = millis();

    File f = SD.open(BACKLOG_FILE, FILE_READ);
    if (!f) return;
    String all = f.readString();
    f.close();
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
    File w = SD.open(BACKLOG_FILE, FILE_WRITE);
    if (w) {
        if (rest.length() > 0) w.print(rest);
        w.close();
    }
    if (pushed > 0) Serial.printf("[sd] backlog terkirim: %d\n", pushed);
}
