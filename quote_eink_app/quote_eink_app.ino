// quote_eink_app.ino
//
// Main orchestration for the Quote E-Ink App.
// All logic lives in the modules:
//  - display_manager.*
//  - wifi_manager.*
//  - firebase_client.*
//  - ota_manager.*
//  - app_prefs.*
//  - provisioning.*
//  - secrets.h

#include <Arduino.h>
#include <WiFi.h>

#include "secrets.h"
#include "display_manager.h"
#include "wifi_manager.h"
#include "firebase_client.h"
#include "ota_manager.h"
#include "app_prefs.h"
#include "provisioning.h"

// Timing for quotes + OTA
unsigned long lastQuoteUpdate = 0;
unsigned long lastUpdateCheck = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.print("==== Quote E-Ink App FW ");
  Serial.print(FW_VERSION);
  Serial.println(" ====");

  // Init display (landscape, clear screen, show nothing yet)
  displayInit();

  randomSeed(esp_random());

  // Ensure Wi-Fi + Firebase credentials are provisioned at least once
  if (!isProvisioned()) {
    Serial.println("[BOOT] Device not provisioned. Starting provisioning...");
    displayStatus("First boot.\nStarting setup...");
    startProvisioning();   // will reboot after successful provisioning
  }

  // Connect to Wi-Fi (or go back to provisioning if it fails)
  connectWiFi();

  // Check for OTA update immediately at boot (GitHub)
  checkForUpdate();

  // Auth into Firebase (REST API using email/password from NVS)
  if (ensureFirebaseAuth()) {
    fetchAndDisplayQuote();
  }

  // If this was a fresh OTA image in PENDING_VERIFY state,
  // mark it valid now that we've successfully booted & shown a quote.
  finalizeOtaIfPending();

  lastQuoteUpdate = millis();
  lastUpdateCheck = millis();
}

void loop() {
  // Periodic quote refresh
  if (millis() - lastQuoteUpdate >= QUOTE_INTERVAL_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      fetchAndDisplayQuote();
    } else {
      displayError("Wi-Fi lost.\nReconnecting...");
      WiFi.reconnect();
    }
    lastQuoteUpdate = millis();
  }

  // Daily OTA check (every 24 hours)
  if (millis() - lastUpdateCheck >= 86400000UL) { // 24 * 60 * 60 * 1000
    checkForUpdate();
    lastUpdateCheck = millis();
  }

  delay(100);
}
