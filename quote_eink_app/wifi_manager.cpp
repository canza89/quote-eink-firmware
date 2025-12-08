#include "wifi_manager.h"

#include <WiFi.h>

#include "app_prefs.h"
#include "provisioning.h"
#include "display_manager.h"
#include "secrets.h"

void connectWiFi() {
  String ssid = readNVS("wifi_ssid");
  String pass = readNVS("wifi_password");

  if (ssid.length() == 0) {
    Serial.println("[WIFI] No SSID stored. Starting provisioning...");
    displayStatus("No Wi-Fi saved.\nStarting setup...");
    startProvisioning(); // never returns
  }

  Serial.println("[WIFI] Connecting to " + ssid);
  displayStatus("Connecting to Wi-Fi:\n" + ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    Serial.print("[WIFI] Connected. IP: ");
    Serial.println(ip);
    displayStatus("Wi-Fi connected:\n" + ssid + "\nIP: " + ip.toString());
  } else {
    Serial.println("[WIFI] Failed. Starting provisioning...");
    displayStatus("Wi-Fi failed.\nStarting setup...");
    startProvisioning();
  }
}
