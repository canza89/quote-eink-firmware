// logout_manager.cpp

#include <Arduino.h>
#include "logout_manager.h"
#include "app_prefs.h"
#include "display_manager.h"

void deviceLogout() {
  Serial.println("[LOGOUT] Clearing all credentials and restarting...");
  displayStatus("Logging out...\nResetting...");

  // This clears Wi-Fi + Firebase credentials + "provisioned" flag
  clearAllCredentials();   // from app_prefs.cpp

  delay(500);
  ESP.restart();           // never returns
}
