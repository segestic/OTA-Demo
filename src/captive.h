#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

extern bool triggerOtaCheck; 

class CaptiveManager {
private:
    AsyncWebServer server;
    DNSServer dnsServer;
    Preferences preferences;
    bool portalActive;
    const byte DNS_PORT = 53;

    // Flags for safe Flash operations
    bool pendingSave = false;
    unsigned long saveTriggerTime = 0;
    String pendingSSID = "";
    String pendingPASS = "";

    // --- NEW: Flag for resetting Wi-Fi ---
    bool pendingReset = false;
    unsigned long resetTriggerTime = 0;

    void startPortal() {
        portalActive = true;
        
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32_Commercial_Setup");
        Serial.println("[SYSTEM] Started Access Point: ESP32_Commercial_Setup");

        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

        // The SETUP Page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
            String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                          "<style>body{font-family:sans-serif;text-align:center;background:#f4f4f9;padding-top:50px;} "
                          "h2{color:#333;} "
                          "input{margin:10px 0;padding:12px;width:90%;max-width:300px;border:1px solid #ccc;border-radius:5px;} "
                          "input[type='submit']{background:#007BFF;color:white;font-weight:bold;border:none;cursor:pointer;}</style></head>"
                          "<body><h2>Device Setup</h2>"
                          "<form action='/save' method='POST'>"
                          "<input type='text' name='ssid' placeholder='WiFi Name (SSID)' required><br>"
                          "<input type='password' name='pass' placeholder='WiFi Password' required><br>"
                          "<input type='submit' value='Connect'>"
                          "</form></body></html>";
            request->send(200, "text/html", html);
        });

        server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
            this->pendingSSID = request->arg("ssid");
            this->pendingPASS = request->arg("pass");
            request->send(200, "text/plain", "Credentials Received! Device will now reboot and connect.");
            this->pendingSave = true;
            this->saveTriggerTime = millis();
        });

        server.onNotFound([](AsyncWebServerRequest *request){
            request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
        });

        server.begin();
    }

public:
    CaptiveManager() : server(80), portalActive(false) {}

    void begin(String currentVersion) {
        preferences.begin("wifi_creds", true); 
        String savedSSID = preferences.getString("ssid", "");
        String savedPASS = preferences.getString("pass", "");
        preferences.end();

        if (savedSSID == "") {
            Serial.println("[SYSTEM] No Wi-Fi credentials found. Entering Setup Mode.");
            startPortal();
        } else {
            Serial.println("[SYSTEM] Connecting to Wi-Fi: " + savedSSID);
            WiFi.mode(WIFI_STA);
            WiFi.begin(savedSSID.c_str(), savedPASS.c_str());

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 30) {
                delay(500);
                Serial.print(".");
                attempts++;
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("\n[SYSTEM] Wi-Fi Connected. IP: " + WiFi.localIP().toString());
                
                // ---------------------------------------------------------
                // UPDATED: The Commercial Web Dashboard
                // ---------------------------------------------------------
                server.on("/", HTTP_GET, [currentVersion](AsyncWebServerRequest *request){
                    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
                                  "<style>body{font-family:sans-serif;text-align:center;background:#2c3e50;color:white;padding-top:40px;} "
                                  "h1{color:#ecf0f1;} .version-box{background:#34495e;padding:20px;border-radius:10px;display:inline-block;margin:20px 0;}"
                                  ".ver-text{font-size:36px;color:#2ecc71;font-weight:bold;margin:0;} "
                                  "button{background:#3498db;color:white;padding:15px 30px;font-size:18px;border:none;border-radius:5px;cursor:pointer;transition:0.3s; margin: 10px;}"
                                  "button:hover{background:#2980b9;} "
                                  ".btn-danger{background:#e74c3c;} .btn-danger:hover{background:#c0392b;}</style></head>"
                                  "<body><h1>Device Dashboard</h1>"
                                  "<div class='version-box'><p style='margin:0 0 10px 0;'>Current Firmware</p>"
                                  "<p class='ver-text'>v" + currentVersion + "</p></div><br>"
                                  
                                  // Update Button
                                  "<button onclick=\"fetch('/update').then(()=>alert('Update triggered! Check device LEDs/Serial.'));\">Check for Updates</button><br>"
                                  
                                  // NEW: Reset Wi-Fi Button with JavaScript Confirmation
                                  "<button class='btn-danger' onclick=\"if(confirm('Are you sure? This will disconnect the device from Wi-Fi and reboot into Setup Mode.')) { fetch('/reset-wifi').then(()=>alert('Rebooting... Look for the ESP32_Commercial_Setup network.')); }\">Reset Wi-Fi</button>"
                                  
                                  "</body></html>";
                    request->send(200, "text/html", html);
                });

                // OTA Endpoint
                server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
                    triggerOtaCheck = true; 
                    request->send(200, "text/plain", "OK");
                });

                // NEW: Reset Wi-Fi Endpoint
                server.on("/reset-wifi", HTTP_GET, [this](AsyncWebServerRequest *request){
                    request->send(200, "text/plain", "Resetting...");
                    this->pendingReset = true;
                    this->resetTriggerTime = millis();
                });

                server.begin();
                Serial.println("[SYSTEM] Commercial Dashboard Started.");
            } else {
                Serial.println("\n[SYSTEM] Wi-Fi connection failed! Falling back to setup.");
                WiFi.disconnect();
                startPortal();
            }
        }
    }

    void handle() {
        if (portalActive) {
            dnsServer.processNextRequest();
        }

        // Handle Saving New Credentials
        if (pendingSave && (millis() - saveTriggerTime > 1000)) {
            pendingSave = false; 
            dnsServer.stop();
            
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.putString("ssid", pendingSSID);
            prefs.putString("pass", pendingPASS);
            prefs.end();

            Serial.println("[SYSTEM] Saved. Rebooting...");
            delay(500);
            ESP.restart(); 
        }

        // --- NEW: Handle Resetting Credentials ---
        if (pendingReset && (millis() - resetTriggerTime > 1000)) {
            pendingReset = false;
            
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.clear(); // Wipes all keys in the "wifi_creds" namespace
            prefs.end();

            Serial.println("[SYSTEM] Wi-Fi Credentials wiped! Rebooting into Captive Portal...");
            delay(500);
            ESP.restart();
        }
    }
};
