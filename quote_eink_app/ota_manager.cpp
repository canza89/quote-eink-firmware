#include "ota_manager.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>

extern "C" {
  #include "esp_ota_ops.h"
}

#include "secrets.h"
#include "display_manager.h"

// Compare "1.2.3" style semantic versions
static bool isNewerVersion(const String &remote, const String &current) {
  int rMaj = 0, rMin = 0, rPatch = 0;
  int cMaj = 0, cMin = 0, cPatch = 0;

  sscanf(remote.c_str(), "%d.%d.%d", &rMaj, &rMin, &rPatch);
  sscanf(current.c_str(), "%d.%d.%d", &cMaj, &cMin, &cPatch);

  if (rMaj != cMaj) return rMaj > cMaj;
  if (rMin != cMin) return rMin > cMin;
  return rPatch > cPatch;
}

// Actual flash to other OTA partition
static bool performOTA(const String &binUrl) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Wi-Fi not connected.");
    return false;
  }

  Serial.print("[OTA] Starting OTA from URL: ");
  Serial.println(binUrl);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, binUrl)) {
    Serial.println("[OTA] HTTP begin failed");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[OTA] HTTP error: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  Serial.print("[OTA] Content-Length: ");
  Serial.println(contentLength);

  if (contentLength <= 0) {
    Serial.println("[OTA] Invalid content length");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength)) {  // writes to next OTA slot
    Serial.print("[OTA] Update.begin failed. Error: ");
    Serial.println(Update.errorString());
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  Serial.print("[OTA] Written: ");
  Serial.print(written);
  Serial.print(" / ");
  Serial.println(contentLength);

  if (written != (size_t)contentLength) {
    Serial.println("[OTA] Written size mismatch");
    http.end();
    Update.end();
    return false;
  }

  if (!Update.end()) {
    Serial.print("[OTA] Update.end failed. Error: ");
    Serial.println(Update.errorString());
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    Serial.println("[OTA] Update not finished");
    http.end();
    return false;
  }

  Serial.println("[OTA] Update successful, restarting into test image...");
  http.end();
  displayStatus("Firmware updated.\nRebooting...");
  delay(2000);
  ESP.restart();
  return true; // not really reached
}

// Check GitHub meta JSON and decide whether to OTA
void checkForUpdate() {
  Serial.println("[OTA] checkForUpdate()...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] Wi-Fi not connected, skipping.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, GITHUB_OTA_META_URL)) {
    Serial.println("[OTA] HTTP begin failed for meta URL");
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print("[OTA] HTTP error: ");
    Serial.println(httpCode);
    String body = http.getString();
    Serial.println("[OTA] Body:");
    Serial.println(body);
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  Serial.println("[OTA] Meta JSON:");
  Serial.println(body);

  DynamicJsonDocument doc(1024);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("[OTA] JSON parse error: ");
    Serial.println(err.c_str());
    return;
  }

  String remoteVersion = doc["version"].as<String>();
  String binUrl        = doc["binUrl"].as<String>();

  Serial.print("[OTA] Current FW: ");
  Serial.println(FW_VERSION);
  Serial.print("[OTA] Remote  FW: ");
  Serial.println(remoteVersion);

  if (!isNewerVersion(remoteVersion, FW_VERSION)) {
    Serial.println("[OTA] Firmware up to date.");
    return;
  }

  Serial.println("[OTA] New firmware available. Updating...");
  displayStatus("Updating firmware to\nv" + remoteVersion + "...");

  if (!performOTA(binUrl)) {
    Serial.println("[OTA] OTA failed.");
    displayError("Update failed.");
  }
}

// After rebooting into a new OTA slot, the bootloader
// may mark the image as PENDING_VERIFY. If we *don't*
// call esp_ota_mark_app_valid_cancel_rollback() and the
// device reboots/crashes, the bootloader can roll back
// to the previous image (when rollback is enabled).
void finalizeOtaIfPending() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    Serial.println("[OTA] No running partition info.");
    return;
  }

  esp_ota_img_states_t state;
  esp_err_t err = esp_ota_get_state_partition(running, &state);
  if (err != ESP_OK) {
    Serial.print("[OTA] esp_ota_get_state_partition failed: ");
    Serial.println((int)err);
    return;
  }

  Serial.print("[OTA] Current OTA image state: ");
  Serial.println((int)state);

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    Serial.println("[OTA] Image is PENDING_VERIFY. Marking as valid...");
    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
      Serial.println("[OTA] App marked valid; rollback cancelled.");
    } else {
      Serial.print("[OTA] Failed to mark app valid: ");
      Serial.println((int)err);
    }
  }
}
