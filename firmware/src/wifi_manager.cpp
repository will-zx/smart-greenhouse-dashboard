#include "wifi_manager.h"
#include "config.h"

WiFiManager wifiManager;

void WiFiManager::begin() {
    // Initialize NVS namespace "wifi"
    prefs.begin("wifi", false);
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.setSleep(false);
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
    if (c.username.length() == 0) {
        WiFi.begin(c.ssid.c_str(), c.password.c_str());
    } else {
        // Enterprise not supported in this core version – fallback to regular WPA2 using password field.
        WiFi.begin(c.ssid.c_str(), c.password.c_str());
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

void WiFiManager::saveCred(const WiFiCred &cred) {
    // Tambah ke daftar, jika penuh ganti entry pertama
    uint8_t idx = (credCount < WIFI_MAX_NETWORKS) ? credCount++ : 0;
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
}

void WiFiManager::clearCreds() {
    prefs.clear();
    credCount = 0;
    configSaved = false;
}

void WiFiManager::startSetupAP() {
    Serial.println("[wifi] masuk ke mode konfigurasi (AP)");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("GH-Setup", "setup1234");
    // Simple HTML form
    const char *form = R"EOF(
<!DOCTYPE html><html><body>
<h2>Konfigurasi WiFi</h2>
<form method="POST" action="/save">
SSID: <input name="ssid"><br>
Password: <input type="password" name="pwd"><br>
Username (Enterprise, optional): <input name="usr"><br>
Identity (optional): <input name="idn"><br>
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
        saveCred(newCred);
        server.send(200, "text/plain", "Saved – device will restart");
        delay(1000);
        ESP.restart();
    });
    server.begin();
    while (!configSaved) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
