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

// Hitung jumlah baris (dipisah '\n') dalam string backlog.
static int countLines(const String& s) {
    int lines = 0;
    for (unsigned int i = 0; i < s.length(); i++) if (s[i] == '\n') lines++;
    if (s.length() > 0 && !s.endsWith("\n")) lines++;   // baris terakhir tanpa newline
    return lines;
}

// Buang n baris paling depan (data paling lama) dari string backlog.
static void dropOldestLines(String& s, int n) {
    for (int i = 0; i < n; i++) {
        int idx = s.indexOf('\n');
        if (idx < 0) { s = ""; return; }
        s.remove(0, idx + 1);
    }
}

void sdQueueBacklog(const char* body) {
    if (!sdReady) return;
    lock();
    // Hitung baris antrean (baca saja, tidak menulis kartu).
    String all;
    File f = SD.open(BACKLOG_FILE, FILE_READ);
    if (f) {
        all = f.readString();
        f.close();
    }
    int lines = countLines(all);

    if (lines < SD_BACKLOG_MAX) {
        // Normal: append (murah, tidak menulis ulang file).
        File w = SD.open(BACKLOG_FILE, FILE_APPEND);
        if (w) {
            w.println(body);
            w.close();
            Serial.printf("[sd] push gagal, antrekan backlog (%d)\n", lines + 1);
        }
        unlock();
        return;
    }

    // Penuh: yang dibuang adalah baris paling LAMA (data terbaru lebih
    // penting). Dulu yang dibuang justru record terbaru sehingga data paling
    // baru hilang saat antrean penuh.
    int drop = lines - (SD_BACKLOG_MAX - 1);   // sisakan MAX-1 supaya baris baru muat
    dropOldestLines(all, drop);
    Serial.printf("[sd] backlog penuh, %d baris terlama dibuang\n", drop);

    // Tulis ulang file: sisa antrean + record baru.
    // ponytail: tulis ulang seluruh file = wear kartu SD, tapi cuma jalan saat
    // antrean penuh (rare); jalur normal tetap append.
    File w = SD.open(BACKLOG_FILE, FILE_WRITE);   // truncate
    if (w) {
        w.print(all);
        if (all.length() > 0 && !all.endsWith("\n")) w.println();
        w.println(body);
        w.close();
        Serial.printf("[sd] push gagal, antrekan backlog (%d)\n", SD_BACKLOG_MAX);
    }
    unlock();
}

void sdFlushBacklog() {
    if (!sdReady) return;
    // Tulis ulang seluruh file setiap flush — jangan terlalu sering.
    if (millis() - lastBacklogFlush < SD_FLUSH_MS) return;
    lastBacklogFlush = millis();

    // Ambil isi backlog TANPA memotong file dulu. Kalau listrik mati di tengah
    // pengiriman, data masih utuh di kartu (dulu file di-truncate lebih dulu
    // -> seluruh antrean hilang begitu device mati). Setelah sending, file
    // ditulis ulang dengan sisa antrean saja (baris yang sukses terkirim dibuang).
    String all;
    lock();
    File f = SD.open(BACKLOG_FILE, FILE_READ);
    if (f) {
        all = f.readString();
        f.close();
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
    // Baris terakhir tanpa newline (jarang, tapi jangan hilang diam-diam).
    if (start < (int)all.length()) {
        String line = all.substring(start);
        line.trim();
        if (line.length() > 0) {
            if (pushed < SD_BACKLOG_FLUSH_PER_CALL && httpPostSensorLogs(line)) {
                pushed++;
            } else {
                rest += line + "\n";
            }
        }
    }

    // Baru sekarang file dipotong: isinya hanya antrean yang belum terkirim.
    // Kalau tidak ada yang terkirim, file tidak perlu ditulis ulang.
    // (Kalau device mati saat menulis, sisa antrean bisa hilang sebagian —
    // ponytail: rename-atomik butuh SD.rename + file sementara; cukup kalau
    // power padam saat flush jarang terjadi.)
    if (pushed > 0) {
        lock();
        File w = SD.open(BACKLOG_FILE, FILE_WRITE);   // truncate
        if (w) {
            w.print(rest);
            w.close();
        }
        unlock();
    }
    if (pushed > 0) Serial.printf("[sd] backlog terkirim: %d\n", pushed);
}
