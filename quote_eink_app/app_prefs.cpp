// app_prefs.cpp

#include "secrets.h"
#include "app_prefs.h"
#include <Preferences.h>

Preferences prefs;

// Read a stored string value from NVS
String readNVS(const char* key) {
  prefs.begin(PREFS_NAMESPACE, true);   // read-only
  String value = prefs.getString(key, "");
  prefs.end();
  return value;
}

// Write a string value to NVS
void writeNVS(const char* key, const String& value) {
  prefs.begin(PREFS_NAMESPACE, false);  // read-write
  prefs.putString(key, value);
  prefs.end();
}

// Device is considered provisioned if both Wi-Fi and Firebase email are set
bool isProvisioned() {
  return readNVS("wifi_ssid").length() > 0 &&
         readNVS("fb_email").length() > 0;
}
