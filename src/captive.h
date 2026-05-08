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

    // --- INDUSTRIAL ADDITIONS FOR SAFE NVS WRITES ---
    bool pendingSave = false;
    unsigned long saveTriggerTime = 0;
    String pendingSSID = "";
    String pendingPASS = "";
    // ------------------------------------------------

    void startPortal() {
        portalActive = true;
        
        WiFi.mode(WIFI_AP);
        WiFi.softAP("ESP32_Commercial_Setup");
        Serial.println("[SYSTEM] Started Access Point: ESP32_Commercial_Setup");

        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

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

        // ---------------------------------------------------------
        // INDUSTRIAL RULE: No Flash Writes in Async Callbacks!
        // ---------------------------------------------------------
        server.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request){
            // 1. Grab the variables into RAM
            this->pendingSSID = request->arg("ssid");
            this->pendingPASS = request->arg("pass");
            
            // 2. Send the HTTP response immediately so the user's phone doesn't hang
            request->send(200, "text/plain", "Credentials Received! Device will now reboot and connect.");
            
            // 3. Set the flag to process the save safely in the main thread
            this->pendingSave = true;
            this->saveTriggerTime = millis();
        });

        server.onNotFound([](AsyncWebServerRequest *request){
            request->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
        });

        server.begin();
        Serial.println("[SYSTEM] Captive Portal Web Server Started.");
    }

public:
    CaptiveManager() : server(80), portalActive(false) {}

    void begin() {
        preferences.begin("wifi_creds", true); 
        String savedSSID = preferences.getString("ssid", "");
        String savedPASS = preferences.getString("pass", "");
        preferences.end();

        if (savedSSID == "") {
            Serial.println("[SYSTEM] No Wi-Fi credentials found.");
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
                
                server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request){
                    triggerOtaCheck = true; 
                    request->send(200, "text/plain", "OTA Check triggered.");
                });
                server.begin();
            } else {
                Serial.println("\n[SYSTEM] Wi-Fi connection failed! Password changed or router offline.");
                WiFi.disconnect();
                startPortal();
            }
        }
    }

    // This runs constantly in the main loop()
    void handle() {
        if (portalActive) {
            dnsServer.processNextRequest();
        }

        // ---------------------------------------------------------
        // INDUSTRIAL RULE: Safe Flash Execution
        // Wait 1000ms after the user clicks "Connect" before writing to NVS.
        // This ensures the HTTP 200 OK packet actually leaves the Wi-Fi antenna!
        // ---------------------------------------------------------
        if (pendingSave && (millis() - saveTriggerTime > 1000)) {
            pendingSave = false; // Prevent it from running twice

            // 1. Turn off DNS to quiet the Wi-Fi radio and prevent interrupts
            dnsServer.stop();
            
            // 2. Safe to write to Flash now on the main CPU core
            Serial.println("[SYSTEM] Safely writing credentials to NVS...");
            Preferences prefs;
            prefs.begin("wifi_creds", false);
            prefs.putString("ssid", pendingSSID);
            prefs.putString("pass", pendingPASS);
            prefs.end();

            Serial.println("[SYSTEM] Saved. Rebooting...");
            delay(500);
            ESP.restart(); // Reboot into normal Wi-Fi mode
        }
    }
};
