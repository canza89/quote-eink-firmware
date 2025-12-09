// app_prefs.cpp

#include <Preferences.h>
#include "secrets.h"    // for PREFS_NAMESPACE
#include "app_prefs.h"

Preferences prefs;

// Read a string from NVS by key
String readNVS(const char* key) {
  prefs.begin(PREFS_NAMESPACE, true);  // read-only
  String value = prefs.getString(key, "");
  prefs.end();
  return value;
}

// Write a string to NVS
void writeNVS(const char* key, const String& value) {
  prefs.begin(PREFS_NAMESPACE, false); // read-write
  prefs.putString(key, value);
  prefs.end();
}

// Check if the device is provisioned (Wi-Fi + Firebase creds present)
bool isProvisioned() {
  prefs.begin(PREFS_NAMESPACE, true);

  bool wifiOK = prefs.isKey("wifi_ssid") &&
                prefs.isKey("wifi_password");

  bool fbOK   = prefs.isKey("fb_email") &&
                prefs.isKey("fb_password");

  prefs.end();
  return wifiOK && fbOK;
}

// Clear all stored credentials and provision flag
void clearAllCredentials() {
  prefs.begin(PREFS_NAMESPACE, false);

  // Wi-Fi credentials
  prefs.remove("wifi_ssid");
  prefs.remove("wifi_password");

  // Firebase credentials
  prefs.remove("fb_email");
  prefs.remove("fb_password");

  // Optional: any explicit provision flag if you ever set one
  prefs.remove("provisioned");

  prefs.end();
}
