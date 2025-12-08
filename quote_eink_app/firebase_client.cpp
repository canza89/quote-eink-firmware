// firebase_client.cpp

#include "firebase_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h"
#include "app_prefs.h"
#include "provisioning.h"    // 🔹 Needed for startProvisioning()
#include "display_manager.h" // displayStatus, displayError, displayQuote

// Firebase Auth state (REST-based)
static String firebaseIdToken;          // ID token (Authorization: Bearer ...)
static String firebaseRefreshToken;     // Not used yet
static unsigned long firebaseTokenExpiryMs = 0;  // millis() when token expires
static String firebaseUserUid;          // localId returned by signInWithPassword

// Clear saved Firebase creds in NVS and in-memory state
static void clearFirebaseCredentials() {
  Serial.println("[FIREBASE] Clearing saved credentials from NVS");
  writeNVS("fb_email", "");
  writeNVS("fb_password", "");
  firebaseIdToken = "";
  firebaseUserUid = "";
  firebaseTokenExpiryMs = 0;
}

// Perform REST sign-in with email/password stored in NVS
static bool firebaseSignIn() {
  String fbEmail    = readNVS("fb_email");
  String fbPassword = readNVS("fb_password");

  if (fbEmail.isEmpty() || fbPassword.isEmpty()) {
    Serial.println("[FIREBASE] No email/password stored. Starting provisioning...");
    displayStatus("No Firebase login.\nStarting setup...");
    startProvisioning();  // never returns (will reboot after provisioning)
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
  client.setInsecure();   // TODO: load proper root CA

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
    startProvisioning();  // never returns (new credentials)
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

// Public API: ensure valid token (refresh if expired / missing)
bool ensureFirebaseAuth() {
  if (firebaseIdToken.isEmpty() || firebaseUserUid.isEmpty() ||
      millis() > firebaseTokenExpiryMs) {
    Serial.println("[FIREBASE] Token missing/expired. Signing in...");
    displayStatus("Logging in to Firebase...");
    return firebaseSignIn();
  }
  return true;
}

// Build Firestore listDocuments URL for users/<UID>/quotes
static String buildFirestoreListUrl() {
  String url =
    "https://firestore.googleapis.com/v1/projects/";
  url += FIREBASE_PROJECT_ID;
  url += "/databases/(default)/documents/users/";
  url += firebaseUserUid;
  url += "/quotes?pageSize=50";
  return url;
}

// Public API: fetch one random quote and display it
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
