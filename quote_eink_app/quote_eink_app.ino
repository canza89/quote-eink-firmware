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
//  - logout_manager.*
//  - secrets.h

#include <Arduino.h>
#include <WiFi.h>

// --- Project Modules ---
#include "secrets.h"
#include "display_manager.h"
#include "wifi_manager.h"
#include "firebase_client.h"
#include "ota_manager.h"
#include "app_prefs.h"
#include "provisioning.h"
#include "logout_manager.h"

// ----------------------------------------
// LOGOUT BUTTON (GPIO 21)
// ----------------------------------------

#define LOGOUT_BUTTON_PIN 21
const unsigned long LOGOUT_LONG_PRESS_MS = 3000;  // 3 seconds

bool logoutBtnPressed = false;
unsigned long logoutBtnPressStart = 0;

// ----------------------------------------
// TIMERS
// ----------------------------------------

unsigned long lastQuoteUpdate = 0;
unsigned long lastUpdateCheck = 0;

// ----------------------------------------
// SETUP
// ----------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.print("==== Quote E-Ink App FW ");
  Serial.print(FW_VERSION);
  Serial.println(" ====");

  // Init E-Ink screen (landscape)
  displayInit();

  // Seed RNG for random quote selection
  randomSeed(esp_random());

  // Setup logout button
  pinMode(LOGOUT_BUTTON_PIN, INPUT_PULLUP);

  // FIRST BOOT → NO CREDS → start provisioning portal
  if (!isProvisioned()) {
    Serial.println("[BOOT] Device not provisioned. Starting provisioning...");
    displayStatus("First boot.\nStarting setup...");
    startProvisioning();             // Will reboot after successful provisioning
  }

  // Connect to Wi-Fi (or return to provisioning inside wifi_manager if needed)
  connectWiFi();

  // Check GitHub OTA immediately at boot
  checkForUpdate();

  // Authenticate to Firebase (REST, your custom module)
  if (ensureFirebaseAuth()) {
    fetchAndDisplayQuote();
  }

  // OTA rollback: if running new firmware in PENDING_VERIFY → mark as valid
  finalizeOtaIfPending();

  lastQuoteUpdate = millis();
  lastUpdateCheck = millis();
}

// ----------------------------------------
// LOOP
// ----------------------------------------

void loop() {

  // -----------------------
  // 1. LOGOUT BUTTON LOGIC
  // -----------------------
  int rawState = digitalRead(LOGOUT_BUTTON_PIN);
  bool isPressed = (rawState == LOW);  // button pulls pin to GND

  if (isPressed && !logoutBtnPressed) {
    // Button newly pressed
    logoutBtnPressed = true;
    logoutBtnPressStart = millis();
  }
  else if (!isPressed && logoutBtnPressed) {
    // Released early → cancel
    logoutBtnPressed = false;
  }

  if (logoutBtnPressed &&
      (millis() - logoutBtnPressStart >= LOGOUT_LONG_PRESS_MS)) {
    // Long press → LOGOUT
    deviceLogout();  // implemented in logout_manager.cpp (clears creds + restart)
  }

  // -----------------------
  // 2. PERIODIC QUOTE UPDATE
  // -----------------------
  if (millis() - lastQuoteUpdate >= QUOTE_INTERVAL_MS) {
    if (WiFi.status() == WL_CONNECTED) {
      fetchAndDisplayQuote();
    } else {
      displayError("Wi-Fi lost.\nReconnecting...");
      WiFi.reconnect();
    }
    lastQuoteUpdate = millis();
  }

  // -----------------------
  // 3. DAILY OTA CHECK
  // -----------------------
  if (millis() - lastUpdateCheck >= 86400000UL) { // 24 hours
    checkForUpdate();
    lastUpdateCheck = millis();
  }

  delay(100);
}
