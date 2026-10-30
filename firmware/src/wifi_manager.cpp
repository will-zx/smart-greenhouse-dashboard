#include <esp_wifi.h>
#include "wifi_manager.h"
#include "config.h"



WiFiManager wifiManager;

static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\'') out += "&#39;";
        else out += c;
    }
    return out;
}

void WiFiManager::begin() {
    // AP sudah aktif: biarkan handle() melayani form. Jangan reset AP/timer
    // setiap percobaan reconnect.
    if (apActive) return;
    // Initialize NVS namespace "wifi"
    prefs.begin("wifi", false);
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.setSleep(false);
    // Load stored credentials
    loadCreds();
    if (credCount > 0 && connectStored()) {
        // Connected, nothing else
        return;
    }
    // Fallback to setup AP mode
    startSetupAP();
}

void WiFiManager::loadCreds() {
    credCount = prefs.getUChar("cnt", 0);
    if (credCount > WIFI_MAX_NETWORKS) credCount = WIFI_MAX_NETWORKS;
    for (uint8_t i = 0; i < credCount; ++i) {
        String keyBase = String("ssid") + i;
        creds[i].ssid = prefs.getString(keyBase.c_str(), "");
        keyBase = String("pwd") + i;
        creds[i].password = prefs.getString(keyBase.c_str(), "");
        keyBase = String("usr") + i;
        creds[i].username = prefs.getString(keyBase.c_str(), "");
        keyBase = String("idn") + i;
        creds[i].identity = prefs.getString(keyBase.c_str(), "");
    }
}

bool WiFiManager::tryConnect(const WiFiCred &c) {
    Serial.printf("[wifi] mencoba SSID %s\n", c.ssid.c_str());
    // Ensure clean STA mode before each attempt
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    if (c.username.length() == 0) {
        // Personal Wi‑Fi – disable enterprise handling
        // Ensure enterprise mode disabled for personal Wi‑Fi (no action needed)
        WiFi.begin(c.ssid.c_str(), c.password.c_str());
    } else {
        // Enterprise (PEAP‑MSCHAPv2) – no CA validation
        const char *identity = c.identity.length() ? c.identity.c_str() : c.username.c_str();
        WiFi.begin(c.ssid.c_str(), WPA2_AUTH_PEAP, identity, c.username.c_str(), c.password.c_str());
    }
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::connectStored() {
    for (uint8_t i = 0; i < credCount; ++i) {
        if (tryConnect(creds[i])) {
            Serial.println("[wifi] terhubung!");
            Serial.print("SSID: "); Serial.println(WiFi.SSID());
            Serial.print("IP: "); Serial.println(WiFi.localIP());
            return true;
        }
        Serial.println("[wifi] gagal, mencoba berikutnya");
    }
    return false;
}

bool WiFiManager::saveCred(const WiFiCred &cred) {
    // Tambah ke daftar, jika penuh tolak tambahan
    if (credCount >= WIFI_MAX_NETWORKS) {
        Serial.println("[wifi] kredensial penuh, tidak dapat menambah");
        return false;
    }
    uint8_t idx = credCount++;
    creds[idx] = cred;
    // Simpan semua kembali ke NVS
    prefs.putUChar("cnt", credCount);
    for (uint8_t i = 0; i < credCount; ++i) {
        prefs.putString((String("ssid") + i).c_str(), creds[i].ssid);
        prefs.putString((String("pwd") + i).c_str(), creds[i].password);
        prefs.putString((String("usr") + i).c_str(), creds[i].username);
        prefs.putString((String("idn") + i).c_str(), creds[i].identity);
    }
    prefs.end();
    configSaved = true;
    return true;
}

void WiFiManager::handle() {
    if (!apActive) return;
    server.handleClient();
    // AP timeout – disable AP after AP_TIMEOUT_MS
    if (millis() - apStart > AP_TIMEOUT_MS) {
        Serial.println("[wifi] AP timeout, shutting down");
        WiFi.softAPdisconnect(true);
        apActive = false;
    }
}

// clearCreds implementation
void WiFiManager::clearCreds() {
    prefs.clear();
    credCount = 0;
    configSaved = false;
}

void WiFiManager::startSetupAP() {
    Serial.println("[wifi] masuk ke mode konfigurasi (AP)");
    WiFi.mode(WIFI_AP_STA);
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    char apPass[16];
    snprintf(apPass, sizeof(apPass), "gh-%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
    WiFi.softAP("GH-Setup", apPass);
    Serial.printf("[wifi] AP password: %s\n", apPass);
    const char *form = R"EOF(
<!DOCTYPE html><html><body>
<h2>Konfigurasi WiFi</h2>
<form method="POST" action="/save">
SSID: <input name="ssid"><br>
Password: <input type="password" name="pwd"><br>
Username (PEAP-MSCHAPv2, opsional): <input name="usr"><br>
Identity (opsional): <input name="idn"><br>
<input type="submit" value="Simpan">
</form>
</body></html>
)EOF";
    server.on("/", [this, form](){ server.send(200, "text/html", form); });
    server.on("/save", HTTP_POST, [this](){
        WiFiCred newCred;
        newCred.ssid = server.arg("ssid");
        newCred.password = server.arg("pwd");
        newCred.username = server.arg("usr");
        newCred.identity = server.arg("idn");
        if (newCred.ssid.length() == 0) {
            server.send(400, "text/plain", "SSID diperlukan");
            return;
        }
        // Check for duplicate SSID and update all fields if exists
        for (uint8_t i = 0; i < credCount; ++i) {
            if (creds[i].ssid == newCred.ssid) {
                creds[i] = newCred;
                prefs.putString((String("ssid") + i).c_str(), newCred.ssid);
                prefs.putString((String("pwd") + i).c_str(), newCred.password);
                prefs.putString((String("usr") + i).c_str(), newCred.username);
                prefs.putString((String("idn") + i).c_str(), newCred.identity);
                prefs.end();
                configSaved = true;
                server.send(200, "text/plain", "Updated – device will restart");
                delay(1000);
                ESP.restart();
                return;
            }
        }
        saveCred(newCred);
        server.send(200, "text/plain", "Saved – device will restart");
        delay(1000);
        ESP.restart();
    });
    // List saved networks
    server.on("/list", HTTP_GET, [this](){
        String page = "<h2>Saved WiFi Networks</h2><ul>";
        for (uint8_t i = 0; i < credCount; ++i) {
            page += "<li>" + htmlEscape(creds[i].ssid) +
                    " <form method=\"POST\" action=\"/del\" style=\"display:inline\">"
                    "<input type=\"hidden\" name=\"i\" value=\"" + String(i) + "\">"
                    "<button type=\"submit\">Delete</button></form></li>";
        }
        page += "</ul><form method=\"POST\" action=\"/clear\">"
                "<button type=\"submit\">Clear All</button></form>";
        server.send(200, "text/html", page);
    });
    // Delete a network
    server.on("/del", HTTP_POST, [this](){
        int idx = server.arg("i").toInt();
        if (idx >= 0 && idx < credCount) {
            for (uint8_t j = idx; j < credCount - 1; ++j) {
                creds[j] = creds[j + 1];
            }
            credCount--;
            // rewrite NVS
            prefs.putUChar("cnt", credCount);
            for (uint8_t i = 0; i < credCount; ++i) {
                prefs.putString((String("ssid") + i).c_str(), creds[i].ssid);
                prefs.putString((String("pwd") + i).c_str(), creds[i].password);
                prefs.putString((String("usr") + i).c_str(), creds[i].username);
                prefs.putString((String("idn") + i).c_str(), creds[i].identity);
            }
            prefs.end();
        }
        server.send(200, "text/plain", "Deleted");
    });
    // Clear all credentials
    server.on("/clear", HTTP_POST, [this](){
        clearCreds();
        server.send(200, "text/plain", "All credentials cleared");
    });
    server.begin();
    // No blocking loop – AP stays alive, netTask calls handle().
    apActive = true;
    apStart = millis();

}
