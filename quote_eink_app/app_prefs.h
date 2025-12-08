// app_prefs.h
#ifndef APP_PREFS_H
#define APP_PREFS_H

#include <Arduino.h>

// Read a string from NVS by key
String readNVS(const char* key);

// Write a string to NVS
void writeNVS(const char* key, const String& value);

// Check if the device is provisioned (Wi-Fi + Firebase creds present)
bool isProvisioned();

#endif
