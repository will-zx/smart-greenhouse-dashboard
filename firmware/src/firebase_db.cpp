#include "firebase_db.h"
#include "config.h"
#include "control.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static WiFiClientSecure client;
static HTTPClient http;

// ponytail: setInsecure(true) melepas verifikasi CA. Ceiling: development/test.
// Untuk produksi: client.setCACert(<root cert google>) — tambah ~2KB flash.
static void httpBegin(const String& path) {
    client.setInsecure();
    String url = String(FIREBASE_DB_URL) + path + ".json";
    if (String(FIREBASE_DB_SECRET).length() > 0) {
        url += "?auth=" + String(FIREBASE_DB_SECRET);
    }
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
}

void pushSensorLog(float temperature, float humidity, float soil, float vpd, float etc,
                   const char* statusSistem) {
    if (WiFi.status() != WL_CONNECTED) return;

    // ts = epoch (NTP). Skip saat NTP belum sinkron supaya tidak tercatat epoch 1970.
    time_t ts = time(nullptr);
    if (ts < 100000) return;

    JsonDocument doc;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["soil"] = soil;
    doc["vpd"] = vpd;
    doc["et0"] = etc;
    doc["status_sistem"] = statusSistem;
    doc["ts"] = ts;
    String body;
    serializeJson(doc, body);

    httpBegin("/sensor_logs");
    int code = http.POST(body);
    if (code < 200 || code >= 300) {
        Serial.printf("[fb] push log gagal: HTTP %d\n", code);
    }
    http.end();
}

void pollCommands() {
    if (WiFi.status() != WL_CONNECTED) return;

    httpBegin(String("/commands/") + DEVICE_ID);
    int code = http.GET();
    if (code != 200) {
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();
    if (payload == "null" || payload == "{}") return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    JsonObject cmds = doc.as<JsonObject>();
    if (err || cmds.size() == 0) return;

    // ponytail: mengurutkan dengan string compare push-ID Firebase (terurut waktu).
    // Ceiling: semua perintah ter-fetch tiap 5s; jika volume perintah besar, pakai
    // orderBy="$value/createdAt"&limitToFirst=1 di query (butuh field sort ekstra).
    // Firebase push-ID terurut; kunci terendah = perintah terlama.
    const char* oldest = nullptr;
    JsonObject target;
    for (JsonPair kv : cmds) {
        if (oldest == nullptr || strcmp(kv.key().c_str(), oldest) < 0) {
            oldest = kv.key().c_str();
            target = kv.value().as<JsonObject>();
        }
    }

    const char* action = target["action"] | "";
    bool on = target["value"] | false;

    if (strcmp(action, "auto") == 0) {
        controlRelease();
    } else if (strcmp(action, "mist") == 0) {
        controlSetMist(on);
    } else if (strcmp(action, "irigasi") == 0) {
        controlSetIrig(on);
    } else if (strcmp(action, "blower") == 0) {
        controlSetBlower(on);
    } else if (strcmp(action, "restart") == 0) {
        ESP.restart();
    }
    Serial.printf("[fb] perintah diterima: %s %d\n", action, on);

    // tandai selesai dengan menghapus entri
    httpBegin(String("/commands/") + DEVICE_ID + "/" + oldest);
    http.sendRequest("DELETE", "");
    http.end();
}