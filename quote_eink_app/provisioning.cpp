// provisioning.cpp

#include "secrets.h"
#include "app_prefs.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// Implemented in quote_eink_app.ino
extern void displayStatus(const String &msg);

WebServer server(80);
DNSServer dnsServer;

// Simple HTML provisioning page
const char* HTML_FORM = R"====(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Quote Display Setup</title>
  <style>
    body { font-family: -apple-system, system-ui, sans-serif; margin: 0; padding: 20px; text-align: center; background:#f5f5f5; }
    .card { max-width: 420px; margin: 20px auto; background:#ffffff; padding: 20px; border-radius: 16px; box-shadow: 0 4px 10px rgba(0,0,0,0.08); }
    h2 { margin-top:0; }
    label { display:block; text-align:left; margin-top:12px; margin-bottom:4px; font-size:14px; font-weight:600; }
    input { width:100%; padding:10px; border-radius:10px; border:1px solid #ccc; font-size:14px; box-sizing:border-box; }
    button { margin-top:16px; padding:12px; width:100%; border-radius:999px; border:none; background:#111827; color:white; font-size:15px; font-weight:600; cursor:pointer; }
    button:hover { opacity:0.9; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Quote Display Setup</h2>
    <p>Enter Wi-Fi and Firebase credentials.</p>
    <form action="/save" method="POST">
      <label>Wi-Fi SSID</label>
      <input type="text" name="wifi_ssid" required>

      <label>Wi-Fi Password</label>
      <input type="password" name="wifi_password">

      <label>Firebase Email</label>
      <input type="email" name="fb_email" required>

      <label>Firebase Password</label>
      <input type="password" name="fb_password" required>

      <button type="submit">Save & Reboot</button>
    </form>
  </div>
</body>
</html>
)====";

void handleRoot() {
  server.send(200, "text/html", HTML_FORM);
}

void handleSave() {
  if (server.hasArg("wifi_ssid")) {
    writeNVS("wifi_ssid",     server.arg("wifi_ssid"));
    writeNVS("wifi_password", server.arg("wifi_password"));
    writeNVS("fb_email",      server.arg("fb_email"));
    writeNVS("fb_password",   server.arg("fb_password"));

    String response = F(
      "<html><body><h2>Saved!</h2>"
      "<p>Device will reboot in a few seconds.</p></body></html>"
    );
    server.send(200, "text/html", response);

    displayStatus("Setup saved.\nRebooting...");

    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void startProvisioning() {
  Serial.println("[PROVISIONING] Starting AP mode...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress apIP = WiFi.softAPIP();
  Serial.print("[PROVISIONING] AP IP address: ");
  Serial.println(apIP);

  // Show AP info on the display
  String msg = "Setup mode\n\nSSID:\n";
  msg += AP_SSID;
  msg += "\n\nOpen:\nhttp://";
  msg += apIP.toString();
  displayStatus(msg);

  dnsServer.start(53, "*", apIP);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  server.begin();

  Serial.printf("[PROVISIONING] Connect to Wi-Fi '%s' and open any page.\n", AP_SSID);

  // Block here until reboot
  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    delay(1);
  }
}
