#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

// Connects to Wi-Fi using credentials from NVS.
// If no credentials or connection fails, starts provisioning.
void connectWiFi();

#endif
