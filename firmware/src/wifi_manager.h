#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>

#define WIFI_MAX_NETWORKS 5

struct WiFiCred {
    String ssid;
    String password; // for personal; for Enterprise use username/password fields
    String username; // optional, for PEAP-MSCHAP (empty if not used)
    String identity; // optional
};

class WiFiManager {
public:
    void begin();                // load, try connect, or start AP/setup
    bool saveCred(const WiFiCred &cred);
    void clearCreds();
    void handle(); // call from netTask to process web server when AP active
    bool isAPActive() const { return apActive; }
private:
    Preferences prefs;
    WiFiCred creds[WIFI_MAX_NETWORKS];
    uint8_t credCount = 0;
    bool configSaved = false;
    bool apActive = false;
    unsigned long apStart = 0;
    const unsigned long AP_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 min
    void loadCreds();
    bool tryConnect(const WiFiCred &c);
    bool connectStored();
    void startSetupAP();
    // simple web server for configuration
    WebServer server{80};
};

extern WiFiManager wifiManager;

#endif // WIFI_MANAGER_H