#include <Arduino.h>
#include "ESP32OTAPull.h"

// --- INDUSTRIAL INCLUDES ---
#include "nvs_flash.h"     // Required for NVS Auto-Healing
#include "esp_ota_ops.h"   // Required for App Rollback management
#include "esp_task_wdt.h"  // Required for Task Watchdog Timer (Anti-Freeze)

// ==========================================
// THE MASTER FLAG: Set to 'W' for Wi-Fi, 'B' for BLE
#define TRIGGER_MODE 'W' 
// ==========================================

// --- COMMON CONFIGURATION ---
#define VERSION      "1.0.0"
#define JSON_URL "https://raw.githubusercontent.com/segestic/repo/main/manifest.json"

// Safe execution flag. NEVER run heavy code inside network/radio callbacks.
bool triggerOtaCheck = false; 

// ==========================================
// WI-FI SPECIFIC CODE (Native Captive Portal)
// ==========================================
#if TRIGGER_MODE == 'W'
#include "captive.h" // Includes our modular captive portal class

// Instantiate the manager from our header file
CaptiveManager captivePortal;

void setupTriggerMode() {
    // 1 line of code replaces the entire hardcoded Wi-Fi setup!
    captivePortal.begin(); 
}

// ==========================================
// BLE SPECIFIC CODE
// ==========================================
#elif TRIGGER_MODE == 'B'
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h> 

// If running in BLE mode, we still need credentials to do the actual OTA download
#define SSID "YOUR_WIFI_SSID"
#define PASS "YOUR_WIFI_PASS"

// Generate your own unique UUIDs for production
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define VERSION_CHAR_UUID   "d2583850-84eb-4c57-b088-b4bd69cb7124"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class BleTriggerCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0 && rxValue[0] == 'U') {
            Serial.println("[BLE] Update command 'U' received!");
            triggerOtaCheck = true; // Safe Flag Pattern
        }
    }
};

void setupTriggerMode() {
    BLEDevice::init("ESP32_Commercial_Device");
    BLEServer *pServer = BLEDevice::createServer();
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
                                       
    // In your BLE setup function:
    BLECharacteristic *pVersionChar = pService->createCharacteristic(
                                     VERSION_CHAR_UUID,
                                     BLECharacteristic::PROPERTY_READ
                                   );
    pVersionChar->setValue(VERSION); // Sets the value to "1.0.0"

    pCharacteristic->setCallbacks(new BleTriggerCallback());
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    BLEDevice::startAdvertising();
    Serial.println("[SYSTEM] BLE Advertising Started. Send 'U' to trigger OTA.");
}
#endif

// ==========================================
// COMMON OTA EXECUTION
// ==========================================
void executeOtaPull() {
    Serial.println("[OTA] Starting OTA Pull sequence...");

    // 1. INDUSTRIAL RULE: Pause the Watchdog. 
    // The OTA download takes a long time and will block the main loop. 
    // We must unsubscribe from the WDT so it doesn't falsely trigger a reboot mid-download.
    esp_task_wdt_delete(NULL); 

    // 2. INDUSTRIAL RULE: Kill BLE entirely to free RAM and prevent Radio/Flash interrupts
    #if TRIGGER_MODE == 'B'
    BLEDevice::deinit(true); 
    Serial.println("[SYSTEM] BLE radio safely de-initialized for Flash operations.");
    
    // Turn on Wi-Fi for the download
    Serial.print("[OTA] Turning on Wi-Fi for download");
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(250); Serial.print("."); }
    Serial.println("\n[OTA] Wi-Fi connected.");
    #endif

    // 3. Execute OTA
    ESP32OTAPull ota;
    int ret = ota
        .SetCallback([](int offset, int totallength) {
            int percent = 100 * offset / totallength;
            static int lastPercent = 0;
            if (percent >= lastPercent + 10) {
                Serial.printf("[OTA] Downloading... %02d%%\n", percent);
                lastPercent = percent;
            }
        })
        .CheckForOTAUpdate(JSON_URL, VERSION, ESP32OTAPull::UPDATE_AND_BOOT);
        
    Serial.printf("[OTA] Process finished. Result code: %d\n", ret);

    // 4. If we fail, restart to restore normal operation (BLE mode and clean RAM)
    if (ret != ESP32OTAPull::UPDATE_OK) {
        Serial.println("[OTA] Update failed or no update available. Rebooting to restore system state...");
        delay(1000);
        ESP.restart();
    }

    // 5. If it was just a check and no update was needed, re-enable the Watchdog.
    // (Note: If UPDATE_AND_BOOT succeeded, the device will have already rebooted).
    esp_task_wdt_add(NULL); 
}

// ==========================================
// MAIN ARDUINO HOOKS
// ==========================================
void setup() {
    Serial.begin(115200);
    
    // INDUSTRIAL RULE: Allow power supply caps to charge before Flash access
    // This prevents Brownout Coprocessor crashes on cold boots.
    delay(1000); 

    // ---------------------------------------------------------
    // INDUSTRIAL RULE 1: NVS Auto-Healing
    // Do not blindly initialize NVS. If it's corrupted due to a power loss, 
    // erase and rebuild it automatically so the device doesn't brick.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println("[SYSTEM] NVS Corrupted! Reformatting to self-heal...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    // ---------------------------------------------------------

    // ---------------------------------------------------------
    // INDUSTRIAL RULE 2: App Rollback Verification
    // If this is the first boot after an OTA, we must tell the bootloader the code is stable.
    // If we don't do this, it will assume the OTA failed and roll back on the next boot!
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[SYSTEM] App Rollback cleared. Firmware marked as stable.");
    // ---------------------------------------------------------

    // Start communications (Wi-Fi or BLE based on TRIGGER_MODE)
    setupTriggerMode(); 

    // ---------------------------------------------------------
    // INDUSTRIAL RULE 3: Task Watchdog Timer (Anti-Freeze)
    // Initialize Watchdog for a 10-second timeout. If the loop() hangs, 
    // the system will hardware-reset itself. (Updated for ESP-IDF v5 / Core v3)
    
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 10000,             // 10 seconds (in milliseconds)
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Monitor all available cores
        .trigger_panic = true            // Hardware reset on timeout
    };
    esp_task_wdt_init(&twdt_config); 
    esp_task_wdt_add(NULL); // Subscribe the main setup/loop task to the WDT
    // ---------------------------------------------------------
}

void loop() {
    // "Pet the dog" - Tells the Watchdog everything is running smoothly.
    esp_task_wdt_reset(); 

    // ONLY process the Captive Portal DNS traffic if we are in Wi-Fi mode
    #if TRIGGER_MODE == 'W'
    captivePortal.handle();
    #endif

    // Process flags safely in the main thread (avoids BLE Stack overflows)
    if (triggerOtaCheck) {
        triggerOtaCheck = false;
        executeOtaPull();
    }
    
    // Prevent FreeRTOS Idle Task starvation
    delay(10); 
}
