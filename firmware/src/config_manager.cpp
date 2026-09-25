#if __has_include("config_manager.h")
#include "config_manager.h"
#else
#include "../include/config_manager.h"
#endif

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

ConfigManager::ConfigManager() {}

void ConfigManager::sanitizeHost(String& host) {
  host.trim();
  // Strip http:// or https:// prefix if user typed it
  if (host.startsWith("http://")) {
    host = host.substring(7);
  } else if (host.startsWith("https://")) {
    host = host.substring(8);
  }
  // Strip trailing slashes or paths
  int slashIdx = host.indexOf('/');
  if (slashIdx >= 0) {
    host = host.substring(0, slashIdx);
  }
  // Strip port if user typed host:port
  int colonIdx = host.indexOf(':');
  if (colonIdx >= 0) {
    host = host.substring(0, colonIdx);
  }
}

bool ConfigManager::load(AppConfig& config) {
  prefs.begin("nowplaying", true);
  config.isConfigured = prefs.getBool("configured", false);
  config.wifiSsid     = prefs.getString("ssid", "");
  config.wifiPassword = prefs.getString("password", "");
  config.macHost      = prefs.getString("host", "");
  config.port         = prefs.getUShort("port", DEFAULT_PORT);
  prefs.end();

  return config.isConfigured && (config.wifiSsid.length() > 0) && (config.macHost.length() > 0);
}

void ConfigManager::save(const AppConfig& config) {
  prefs.begin("nowplaying", false);
  prefs.putString("ssid", config.wifiSsid);
  prefs.putString("password", config.wifiPassword);
  prefs.putString("host", config.macHost);
  prefs.putUShort("port", config.port);
  prefs.putBool("configured", true);
  prefs.end();
  Serial.println("[Config] Settings saved to NVS flash!");
}

void ConfigManager::clear() {
  prefs.begin("nowplaying", false);
  prefs.clear();
  prefs.end();
  Serial.println("[Config] Settings cleared!");
}

void ConfigManager::runSetupPortal(DisplayUI& ui, AppConfig& config) {
  Serial.println("\n=======================================================");
  Serial.println("         CARDPUTER NOW PLAYING - SETUP PORTAL          ");
  Serial.println("=======================================================");
  Serial.println(" 1. Connect your phone/laptop to Wi-Fi: " AP_SSID);
  Serial.println(" 2. Open browser: http://192.168.4.1");
  Serial.println(" OR enter via USB Serial: SET:SSID,PASSWORD,MAC_LAN_IP");
  Serial.println("=======================================================\n");

  ui.renderSetupScreen(AP_SSID, "192.168.4.1");

  // Initialize Wi-Fi in AP+STA mode to scan networks for the dropdown
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  delay(100);

  Serial.println("[Setup] Scanning Wi-Fi networks...");
  int n = WiFi.scanNetworks();
  String networkOptions = "";
  if (n == 0) {
    networkOptions += "<option value=\"\">No networks found</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() > 0) {
        networkOptions += "<option value=\"" + ssid + "\">" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
      }
    }
  }

  // Start SoftAP
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[Setup] SoftAP '%s' started at IP: %s\n", AP_SSID, apIP.toString().c_str());

  DNSServer dnsServer;
  dnsServer.start(53, "*", apIP);

  WebServer server(80);
  bool configSaved = false;

  // Root Setup Page
  server.on("/", HTTP_GET, [&]() {
    String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Cardputer Setup</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #000000;
      color: #FFFFFF;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      padding: 24px;
      display: flex;
      justify-content: center;
    }
    .card {
      max-width: 400px;
      width: 100%;
      background: #111111;
      border: 1px solid #282828;
      border-radius: 12px;
      padding: 24px;
      box-shadow: 0 8px 30px rgba(0,0,0,0.8);
    }
    h1 { font-size: 19px; font-weight: 700; margin-bottom: 6px; letter-spacing: -0.5px; }
    p.sub { font-size: 13px; color: #888888; margin-bottom: 22px; }
    label { display: block; font-size: 12px; font-weight: 600; text-transform: uppercase; letter-spacing: 0.8px; color: #AAAAAA; margin-bottom: 6px; }
    input, select {
      width: 100%;
      padding: 12px 14px;
      background: #1A1A1A;
      border: 1px solid #333333;
      border-radius: 8px;
      color: #FFFFFF;
      font-size: 14px;
      margin-bottom: 16px;
      outline: none;
      transition: border-color 0.2s;
    }
    input:focus, select:focus { border-color: #FFFFFF; }
    .toggle-pass { display: flex; align-items: center; gap: 8px; font-size: 12px; color: #888888; margin-top: -10px; margin-bottom: 16px; cursor: pointer; }
    button {
      width: 100%;
      padding: 13px;
      background: #FFFFFF;
      color: #000000;
      border: none;
      border-radius: 8px;
      font-size: 15px;
      font-weight: 600;
      cursor: pointer;
      margin-top: 10px;
      transition: opacity 0.2s;
    }
    button:hover { opacity: 0.9; }
    .footer { margin-top: 20px; font-size: 11px; color: #555555; text-align: center; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Now Playing Setup</h1>
    <p class="sub">Configure your Wi-Fi and macOS Companion Bridge</p>
    <form action="/save" method="POST">
      <label>Select Wi-Fi Network</label>
      <select id="ssid_select" onchange="checkManualSSID()">
        <option value="">-- Choose Scanned Network --</option>
)rawliteral";

    html += networkOptions;

    html += R"rawliteral(
        <option value="__custom__">Manual Entry / Other...</option>
      </select>

      <div id="manual_ssid_div" style="display:none;">
        <label>Manual Wi-Fi SSID</label>
        <input type="text" id="manual_ssid" name="manual_ssid" placeholder="Network Name">
      </div>

      <input type="hidden" id="final_ssid" name="ssid">

      <label>Wi-Fi Password</label>
      <input type="password" id="password" name="password" placeholder="Password (leave blank if open)">
      <div class="toggle-pass" onclick="togglePass()">
        <input type="checkbox" id="show_pass" style="width:auto; margin:0;" onclick="event.stopPropagation(); togglePass();">
        <span>Show Password</span>
      </div>

      <label>Mac LAN IP Address</label>
      <input type="text" name="host" placeholder="e.g. 192.168.1.150" required>

      <label>Bridge Port (Obscure Default)</label>
      <input type="number" name="port" value="58329" min="1" max="65535" required>

      <button type="submit" onclick="submitForm()">Save & Connect</button>
    </form>
    <div class="footer">Cardputer Now Playing &bull; ST7789 IPS Display</div>
  </div>

  <script>
    function checkManualSSID() {
      var sel = document.getElementById("ssid_select");
      var manualDiv = document.getElementById("manual_ssid_div");
      if (sel.value === "__custom__") {
        manualDiv.style.display = "block";
      } else {
        manualDiv.style.display = "none";
      }
    }
    function togglePass() {
      var passInput = document.getElementById("password");
      var cb = document.getElementById("show_pass");
      if (passInput.type === "password") {
        passInput.type = "text";
        cb.checked = true;
      } else {
        passInput.type = "password";
        cb.checked = false;
      }
    }
    function submitForm() {
      var sel = document.getElementById("ssid_select");
      var manual = document.getElementById("manual_ssid");
      var finalInput = document.getElementById("final_ssid");
      if (sel.value === "__custom__" || sel.value === "") {
        finalInput.value = manual.value;
      } else {
        finalInput.value = sel.value;
      }
    }
  </script>
</body>
</html>)rawliteral";

    server.send(200, "text/html", html);
  });

  // Save handler
  server.on("/save", HTTP_POST, [&]() {
    String ssid = server.arg("ssid");
    if (ssid.length() == 0) {
      ssid = server.arg("manual_ssid");
    }
    String pass = server.arg("password");
    String host = server.arg("host");
    String portStr = server.arg("port");

    sanitizeHost(host);
    uint16_t port = portStr.toInt();
    if (port == 0) port = DEFAULT_PORT;

    config.wifiSsid = ssid;
    config.wifiPassword = pass;
    config.macHost = host;
    config.port = port;
    config.isConfigured = true;

    save(config);

    String resp = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Saved!</title>
  <style>
    body { background: #000; color: #FFF; font-family: sans-serif; display: flex; justify-content: center; align-items: center; height: 90vh; text-align: center; }
    .card { background: #111; border: 1px solid #333; border-radius: 12px; padding: 30px; max-width: 360px; }
    h2 { font-size: 20px; margin-bottom: 12px; }
    p { font-size: 14px; color: #AAA; line-height: 1.5; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Configuration Saved!</h2>
    <p>Cardputer is connecting to <b>)rawliteral" + ssid + R"rawliteral(</b>...</p>
  </div>
</body>
</html>)rawliteral";

    server.send(200, "text/html", resp);
    configSaved = true;
  });

  // Captive portal redirects
  server.on("/generate_204", HTTP_GET, [&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.onNotFound([&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });

#if defined(TARGET_WOKWI_SIMULATOR)
  unsigned long simSetupStart = millis();
#endif

  // Run captive loop until user submits or provides Serial command
  while (!configSaved) {
    dnsServer.processNextRequest();
    server.handleClient();

#if defined(TARGET_WOKWI_SIMULATOR)
    // In Wokwi simulator, display the setup screen for 4 seconds,
    // then automatically apply simulator defaults so testing is seamless!
    if (millis() - simSetupStart > 4000) {
      Serial.println("[Setup] Wokwi Simulator: Auto-applying default configuration...");
      config.wifiSsid = WOKWI_DEFAULT_SSID;
      config.wifiPassword = WOKWI_DEFAULT_PASS;
      config.macHost = WOKWI_DEFAULT_HOST;
      config.port = DEFAULT_PORT;
      config.isConfigured = true;
      save(config);
      configSaved = true;
      break;
    }
#endif

    // Check for Serial input: SET:SSID,PASSWORD,MAC_LAN_IP or DEFAULT
    if (Serial.available()) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.equalsIgnoreCase("DEFAULT")) {
        config.wifiSsid = WOKWI_DEFAULT_SSID;
        config.wifiPassword = WOKWI_DEFAULT_PASS;
        config.macHost = WOKWI_DEFAULT_HOST;
        config.port = DEFAULT_PORT;
        config.isConfigured = true;
        save(config);
        configSaved = true;
        Serial.printf("[Setup] Applied default configuration: SSID=%s, Host=%s:%u\n",
                      config.wifiSsid.c_str(), config.macHost.c_str(), config.port);
      } else if (line.startsWith("SET:")) {
        String data = line.substring(4);
        int c1 = data.indexOf(',');
        if (c1 >= 0) {
          int c2 = data.indexOf(',', c1 + 1);
          if (c2 >= 0) {
            config.wifiSsid = data.substring(0, c1);
            config.wifiPassword = data.substring(c1 + 1, c2);
            config.macHost = data.substring(c2 + 1);
            config.port = DEFAULT_PORT;
            sanitizeHost(config.macHost);
            config.isConfigured = true;
            save(config);
            configSaved = true;
            Serial.printf("[Setup] Config received via Serial: SSID=%s, Host=%s:%u\n",
                          config.wifiSsid.c_str(), config.macHost.c_str(), config.port);
          }
        }
      } else if (line.equalsIgnoreCase("HELP")) {
        Serial.println("Commands:");
        Serial.println("  SET:SSID,PASSWORD,MAC_LAN_IP");
        Serial.println("  DEFAULT (use Wokwi defaults: Wokwi-GUEST / host.wokwi.internal)");
      }
    }

    delay(5);
  }

  ui.renderStatus("Settings Saved!", "Connecting to Wi-Fi...");
  delay(1200);

  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.scanDelete();
  WiFi.mode(WIFI_STA);
  delay(200);
}
