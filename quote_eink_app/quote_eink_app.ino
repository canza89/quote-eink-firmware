// quote_eink_app_v.1.0.2.ino

// ----------------------------------------
// CORE LIBRARIES
// ----------------------------------------
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Update.h>               // For OTA

// ----------------------------------------
// DISPLAY LIBRARY (Heltec Vision Master E290)
// ----------------------------------------
#include <heltec-eink-modules.h>

// ----------------------------------------
// PROJECT HEADERS
// ----------------------------------------
#include "secrets.h"              // FW_VERSION, GITHUB_OTA_META_URL, FIREBASE_API_KEY, FIREBASE_PROJECT_ID, etc.
#include "app_prefs.h"            // readNVS, writeNVS, isProvisioned
#include "provisioning.h"         // startProvisioning, WIFI_CONNECT_TIMEOUT_MS, QUOTE_INTERVAL_MS

// ----------------------------------------
// GLOBALS
// ----------------------------------------

// E-ink display object for Heltec Vision Master E290
EInkDisplay_VisionMasterE290 display;

// Timing
unsigned long lastQuoteUpdate = 0;
unsigned long lastUpdateCheck = 0;

// Firebase Auth state (REST-based)
String firebaseIdToken;          // ID token (Authorization: Bearer ...)
String firebaseRefreshToken;     // Not used yet
unsigned long firebaseTokenExpiryMs = 0;  // millis() when token expires
String firebaseUserUid;          // localId returned by signInWithPassword

// ----------------------------------------
// DISPLAY HELPERS
// ----------------------------------------

// Draw small version text (e.g. "v1.0.1") bottom-right
void drawVersionBadge() {
  String versionStr = "v";
  versionStr += FW_VERSION;     // e.g. FW_VERSION "1.0.1" -> "v1.0.1"

  display.setTextSize(1);

  int screenWidth  = display.width();
  int screenHeight = display.height();

  int charWidth    = 6;         // approx built-in font width at textSize=1
  int lineWidthPx  = versionStr.length() * charWidth;

  int marginX = 4;
  int marginY = 8;

  int x = screenWidth  - lineWidthPx - marginX;
  int y = screenHeight - marginY;  // bottom area

  display.setCursor(x, y);
  display.println(versionStr);
}

void displayStatus(const String &msg) {
  Serial.println("[DISPLAY] " + msg);
  display.clearMemory();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println(msg);

  // Draw version in bottom-right corner
  drawVersionBadge();

  display.update();
}

void clearDisplay() {
  display.clearMemory();
  display.update();
}

void displayError(const String& msg) {
  displayStatus("Error:\n" + msg);
}

// Wrap + center multiline text.
// Uses display.width() so it respects rotation.
void printWrappedCentered(const String &text,
                          int maxCharsPerLine,
                          int &cursorY,
                          int lineHeight,
                          uint8_t textSize) {
  display.setTextSize(textSize);

  int len = text.length();
  int start = 0;
  int screenWidthPx = display.width();

  while (start < len) {
    int end = start + maxCharsPerLine;
    if (end > len) end = len;

    int breakPos = end;

    if (end < len) {
      breakPos = text.lastIndexOf(' ', end);
      if (breakPos <= start) {
        breakPos = end;
      }
    }

    String line = text.substring(start, breakPos);
    line.trim();

    if (line.length() > 0) {
      int lineLen    = line.length();
      int charWidth  = 6 * textSize;  // approx built-in font width
      int lineWidthPx = lineLen * charWidth;

      int startX = 0;
      if (lineWidthPx < screenWidthPx) {
        startX = (screenWidthPx - lineWidthPx) / 2;
      }

      display.setCursor(startX, cursorY);
      display.println(line);
      cursorY += lineHeight;
    }

    start = breakPos;
    while (start < len && text[start] == ' ') start++;
  }
}

// Main formatted quote renderer
void displayQuote(const String& text,
                  const String& author,
                  const String& tagsLine) {
  Serial.println("[DISPLAY] Rendering formatted quote...");
  display.clearMemory();

  int screenWidth  = display.width();
  int screenHeight = display.height();

  int y = 6;  // top padding

  // 1) Quote text (big, centered, wrapped)
  const int maxQuoteChars   = 24;   // for textSize=2
  const int quoteLineHeight = 20;
  printWrappedCentered(text, maxQuoteChars, y, quoteLineHeight, 2);

  // Extra spacing before author
  y += 4;

  // 2) Author (small, centered)
  if (author.length() > 0) {
    String authorLine = "- " + author;

    const int authorTextSize   = 1;
    const int authorLineHeight = 12;
    const int maxAuthorChars   = 36;

    if (authorLine.length() > maxAuthorChars) {
      authorLine = authorLine.substring(0, maxAuthorChars);
    }

    display.setTextSize(authorTextSize);

    int lineLen     = authorLine.length();
    int charWidth   = 6 * authorTextSize;
    int lineWidthPx = lineLen * charWidth;
    int startX      = 0;
    if (lineWidthPx < screenWidth) {
      startX = (screenWidth - lineWidthPx) / 2;
    }

    display.setCursor(startX, y);
    display.println(authorLine);
    y += authorLineHeight;
  }

  // 3) Tags line (bottom, small, centered)
  if (tagsLine.length() > 0) {
    const int tagsTextSize = 1;
    display.setTextSize(tagsTextSize);

    String t = tagsLine;
    const int maxTagChars = 45;
    if (t.length() > maxTagChars) {
      t = t.substring(0, maxTagChars - 3) + "...";
    }

    int lineLen     = t.length();
    int charWidth   = 6 * tagsTextSize;
    int lineWidthPx = lineLen * charWidth;
    int yTags       = screenHeight - 14;

    int startX = 0;
    if (lineWidthPx < screenWidth) {
      startX = (screenWidth - lineWidthPx) / 2;
    }

    display.setCursor(startX, yTags);
    display.println(t);
  }

  // Draw version in bottom-right corner
  drawVersionBadge();

  display.update();
}

// ----------------------------------------
// WIFI
// ----------------------------------------

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

// ----------------------------------------
// FIREBASE AUTH (REST)
// ----------------------------------------

void clearFirebaseCredentials() {
  Serial.println("[FIREBASE] Clearing saved credentials from NVS");
  writeNVS("fb_email", "");
  writeNVS("fb_password", "");
  firebaseIdToken = "";
  firebaseUserUid = "";
  firebaseTokenExpiryMs = 0;
}

bool firebaseSignIn() {
  String fbEmail    = readNVS("fb_email");
  String fbPassword = readNVS("fb_password");

  if (fbEmail.isEmpty() || fbPassword.isEmpty()) {
    Serial.println("[FIREBASE] No email/password stored. Starting provisioning...");
    displayStatus("No Firebase login.\nStarting setup...");
    startProvisioning();  // never returns
  }

  Serial.print("[FIREBASE] Signing in as: ");
  Serial.println(fbEmail);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FIREBASE] Wi-Fi not connected.");
    displayError("Wi-Fi not connected.");
    return false;
  }

  String url =
    String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
    FIREBASE_API_KEY;

  WiFiClientSecure client;
  client.setInsecure();   // TODO: proper root cert

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("[FIREBASE] HTTP begin failed");
    displayError("Firebase HTTP init failed");
    return false;
  }

  DynamicJsonDocument payloadDoc(512);
  payloadDoc["email"] = fbEmail;
  payloadDoc["password"] = fbPassword;
  payloadDoc["returnSecureToken"] = true;

  String body;
  serializeJson(payloadDoc, body);

  http.addHeader("Content-Type", "application/json");
  Serial.println("[FIREBASE] POST signInWithPassword...");
  int httpCode = http.POST(body);

  if (httpCode != HTTP_CODE_OK) {
    String err = "[FIREBASE] HTTP error: " + String(httpCode);
    Serial.println(err);
    String resp = http.getString();
    Serial.println("[FIREBASE] Response:");
    Serial.println(resp);
    http.end();

    displayError("Firebase login failed.\nRe-enter credentials.");
    clearFirebaseCredentials();
    delay(2000);
    startProvisioning();  // never returns
  }

  String resp = http.getString();
  http.end();

  Serial.println("[FIREBASE] Response payload:");
  Serial.println(resp);

  DynamicJsonDocument respDoc(2048);
  DeserializationError jsonErr = deserializeJson(respDoc, resp);
  if (jsonErr) {
    String err = "JSON error: ";
    err += jsonErr.c_str();
    Serial.println("[FIREBASE] " + err);
    displayError("Firebase JSON error.");
    return false;
  }

  firebaseIdToken      = respDoc["idToken"].as<String>();
  firebaseRefreshToken = respDoc["refreshToken"].as<String>();
  firebaseUserUid      = respDoc["localId"].as<String>();
  String expiresInStr  = respDoc["expiresIn"].as<String>();

  if (firebaseIdToken.isEmpty() || firebaseUserUid.isEmpty()) {
    Serial.println("[FIREBASE] Missing idToken or localId.");
    displayError("Firebase login invalid.");
    return false;
  }

  unsigned long expiresSec = expiresInStr.toInt();
  if (expiresSec == 0) {
    expiresSec = 3600;
  }

  firebaseTokenExpiryMs = millis() + (expiresSec - 60) * 1000UL;

  Serial.println("[FIREBASE] Sign-in OK.");
  Serial.print("[FIREBASE] UID: ");
  Serial.println(firebaseUserUid);
  Serial.print("[FIREBASE] Token expires in ~");
  Serial.print(expiresSec);
  Serial.println(" seconds.");

  return true;
}

bool ensureFirebaseAuth() {
  if (firebaseIdToken.isEmpty() || firebaseUserUid.isEmpty() ||
      millis() > firebaseTokenExpiryMs) {
    Serial.println("[FIREBASE] Token missing/expired. Signing in...");
    displayStatus("Logging in to Firebase...");
    return firebaseSignIn();
  }
  return true;
}

// ----------------------------------------
// FIRESTORE (REST)
// ----------------------------------------

String buildFirestoreListUrl() {
  String url =
    "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/users/";
  url += firebaseUserUid;
  url += "/quotes?pageSize=50";
  return url;
}

void fetchAndDisplayQuote() {
  if (WiFi.status() != WL_CONNECTED) {
    displayError("Wi-Fi not connected.");
    Serial.println("[QUOTE] Wi-Fi not connected.");
    return;
  }

  if (!ensureFirebaseAuth()) {
    Serial.println("[QUOTE] Auth failed, cannot fetch quotes.");
    return;
  }

  if (firebaseUserUid.isEmpty()) {
    displayError("No Firebase UID.");
    Serial.println("[QUOTE] firebaseUserUid empty.");
    return;
  }

  String url = buildFirestoreListUrl();
  Serial.print("[QUOTE] Requesting: ");
  Serial.println(url);
  displayStatus("Fetching quote\nfrom Firestore...");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    displayError("HTTP begin failed");
    Serial.println("[QUOTE] HTTP begin failed");
    return;
  }

  String authHeader = "Bearer " + firebaseIdToken;
  http.addHeader("Authorization", authHeader);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    String err = "[QUOTE] HTTP error: " + String(httpCode);
    displayError(err);
    Serial.println(err);
    String payload = http.getString();
    Serial.println("[QUOTE] Response body:");
    Serial.println(payload);
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  Serial.println("[QUOTE] Response payload:");
  Serial.println(payload);

  DynamicJsonDocument doc(16384);
  DeserializationError jsonErr = deserializeJson(doc, payload);
  if (jsonErr) {
    String err = "JSON error: ";
    err += jsonErr.c_str();
    displayError(err);
    Serial.println("[QUOTE] " + err);
    return;
  }

  JsonArray docs = doc["documents"].as<JsonArray>();
  if (!docs || docs.size() == 0) {
    displayError("No quotes found.");
    Serial.println("[QUOTE] No documents found.");
    return;
  }

  int count = docs.size();
  int randomIndex = random(count);
  JsonObject randomDoc = docs[randomIndex];

  JsonObject fields = randomDoc["fields"];
  String text   = fields["text"]["stringValue"]   | "";
  String author = fields["author"]["stringValue"] | "";

  String tagsLine = "";
  if (fields.containsKey("tagNames")) {
    JsonVariant tagsVariant = fields["tagNames"]["arrayValue"]["values"];
    if (!tagsVariant.isNull() && tagsVariant.is<JsonArray>()) {
      JsonArray tagArray = tagsVariant.as<JsonArray>();
      for (JsonVariant v : tagArray) {
        String tag = v["stringValue"].as<String>();
        if (tag.length() > 0) {
          if (tagsLine.length() > 0) tagsLine += "   ";
          tagsLine += "#";
          tagsLine += tag;
        }
      }
    }
  }

  Serial.println("[QUOTE] Selected quote:");
  Serial.println("TEXT: " + text);
  Serial.println("AUTHOR: " + author);
  Serial.println("TAGS: " + tagsLine);

  if (text.length() == 0) {
    displayError("Quote missing text");
    return;
  }

  displayQuote(text, author, tagsLine);
}

// ----------------------------------------
// OTA (GITHUB)
// ----------------------------------------

bool isNewerVersion(const String &remote, const String &current) {
  int rMaj = 0, rMin = 0, rPatch = 0;
  int cMaj = 0, cMin = 0, cPatch = 0;

  sscanf(remote.c_str(), "%d.%d.%d", &rMaj, &rMin, &rPatch);
  sscanf(current.c_str(), "%d.%d.%d", &cMaj, &cMin, &cPatch);

  if (rMaj != cMaj) return rMaj > cMaj;
  if (rMin != cMin) return rMin > cMin;
  return rPatch > cPatch;
}

bool performOTA(const String &binUrl) {
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

  if (!Update.begin(contentLength)) {
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

  Serial.println("[OTA] Update successful, restarting...");
  http.end();
  displayStatus("Firmware updated.\nRebooting...");
  delay(2000);
  ESP.restart();
  return true; // not really reached
}

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

// ----------------------------------------
// SETUP & LOOP
// ----------------------------------------

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.print("==== Quote E-Ink App FW ");
  Serial.print(FW_VERSION);
  Serial.println(" ====");

  display.begin();
  display.setRotation(1); // landscape
  display.clearMemory();
  display.update();

  randomSeed(esp_random());

  if (!isProvisioned()) {
    Serial.println("[BOOT] Device not provisioned. Starting provisioning...");
    displayStatus("First boot.\nStarting setup...");
    startProvisioning();
  }

  connectWiFi();

  // Check for update immediately at boot
  checkForUpdate();

  // First auth + initial quote
  if (ensureFirebaseAuth()) {
    fetchAndDisplayQuote();
  }

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
