#include "telegram_bot.h"
#include "control.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static bool botEnabled = false;
static long lastUpdateId = 0;

static String buildStatus(float temp, float humidity, float soilMoisture, float vpd,
                          const char* status, bool mist, bool irigasi, bool blower) {
    String msg = "=== Smart Watering System ===\n\n";
    msg += "Suhu: " + String(temp, 1) + " C\n";
    msg += "Kelembaban: " + String(humidity, 1) + " %\n";
    msg += "Soil Moisture: " + String(soilMoisture, 1) + " %\n";
    msg += "VPD: " + String(vpd, 2) + " kPa\n";
    msg += "Status: " + String(status) + "\n\n";
    msg += "Misting: " + String(mist ? "ON" : "OFF") + "\n";
    msg += "Irigasi: " + String(irigasi ? "ON" : "OFF") + "\n";
    msg += "Blower: " + String(blower ? "ON" : "OFF") + "\n\n";
    msg += "Data diperbarui: " + String(millis() / 1000) + " detik";
    return msg;
}

static void sendTelegramMessage(const String& text) {
    if (!botEnabled || WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["chat_id"] = CHAT_ID;
    doc["text"] = text;
    doc["parse_mode"] = "HTML";
    String body;
    serializeJson(doc, body);
    http.POST(body);
    http.end();
}

static bool parseOnOff(const String& s) {
    return s.indexOf("on") >= 0;
}

void initTelegram() {
    if (String(CHAT_ID) == "YOUR_CHAT_ID" || String(BOT_TOKEN) == "YOUR_BOT_TOKEN") {
        Serial.println("Telegram bot disabled (no CHAT_ID set)");
        botEnabled = false;
        return;
    }
    botEnabled = true;
    Serial.println("Telegram bot enabled");
    sendTelegramMessage("Smart Watering System Online!");
}

void pollTelegram(float temp, float humidity, float soilMoisture, float vpd,
                  const char* status, bool mist, bool irigasi, bool blower) {
    if (!botEnabled || WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/getUpdates?offset=" + String(lastUpdateId + 1);
    http.begin(url);
    http.setTimeout(3000);
    int code = http.GET();
    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            JsonArray results = doc["result"];
            for (JsonObject r : results) {
                long id = r["update_id"];
                if (id > lastUpdateId) lastUpdateId = id;
                String cmd = r["message"]["text"];
                if (cmd == "/status") {
                    sendTelegramMessage(buildStatus(temp, humidity, soilMoisture, vpd,
                                                    status, mist, irigasi, blower));
                } else if (cmd == "/restart") {
                    sendTelegramMessage("Restarting system...");
                    delay(1000);
                    ESP.restart();
                } else if (cmd == "/auto") {
                    controlRelease();
                    sendTelegramMessage("Mode otomatis aktif");
                } else if (cmd.startsWith("/mist ")) {
                    bool on = parseOnOff(cmd);
                    controlSetMist(on);
                    sendTelegramMessage("Pompa Misting: " + String(on ? "ON" : "OFF"));
                } else if (cmd.startsWith("/irigasi ")) {
                    bool on = parseOnOff(cmd);
                    controlSetIrig(on);
                    sendTelegramMessage("Pompa Irigasi: " + String(on ? "ON" : "OFF"));
                } else if (cmd.startsWith("/blower ")) {
                    bool on = parseOnOff(cmd);
                    controlSetBlower(on);
                    sendTelegramMessage("Blower: " + String(on ? "ON" : "OFF"));
                }
            }
        }
    }
    http.end();
}

void sendTelegramAlert(const char* message) {
    String alert = "<b>ALERT</b>\n";
    alert += String(message);
    sendTelegramMessage(alert);
}