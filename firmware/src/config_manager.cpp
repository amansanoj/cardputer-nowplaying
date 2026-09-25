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
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Now Playing Setup</title>
<style>
  @import url('https://cdn.jsdelivr.net/npm/@amansanoj/brand/globals.css');

  :root {
    --background: #000000;
    --text: #ffffff;
    --card: #111111;
    --border: #282828;
    --radius: 8px;
    --primary: #ffffff;
    --primary-foreground: #000000;
    --muted-foreground: #888888;
    --accent: #00ff88;
    --font-body: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    --font-mono: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace;
    --font-display: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { height: 100%; }

  body {
    font-family: var(--font-body);
    background: var(--background);
    color: var(--text);
    min-height: 100%;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 32px 20px;
    padding-top: calc(32px + env(safe-area-inset-top, 0px));
    padding-bottom: calc(32px + env(safe-area-inset-bottom, 0px));
    -webkit-font-smoothing: antialiased;
  }

  .card {
    width: 100%;
    max-width: 380px;
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    gap: 26px;
  }

  .identity {
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
  }

  .name {
    font-family: var(--font-display);
    font-size: 1.7rem;
    font-weight: 600;
    letter-spacing: -0.02em;
    line-height: 1.15;
  }

  form {
    width: 100%;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .field {
    text-align: left;
    border: 1px solid var(--border);
    border-radius: var(--radius);
    background: var(--card);
    padding: 10px 16px 12px;
    transition: border-color 0.15s ease;
  }

  .field:focus-within {
    border-color: var(--accent);
  }

  .field label {
    display: block;
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--muted-foreground);
    margin-bottom: 3px;
  }

  .field input,
  .field select {
    width: 100%;
    border: none;
    background: transparent;
    color: var(--text);
    font-family: var(--font-body);
    font-size: 0.95rem;
    font-weight: 500;
    outline: none;
    appearance: none;
  }

  .field select {
    cursor: pointer;
  }

  .pass-row {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .pass-row input { flex: 1; }

  .toggle-pass {
    display: flex;
    align-items: center;
    gap: 6px;
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--muted-foreground);
    cursor: pointer;
    user-select: none;
    white-space: nowrap;
  }

  .toggle-pass input {
    width: auto;
    accent-color: var(--accent);
  }

  button {
    width: 100%;
    padding: 15px 20px;
    background: var(--primary);
    color: var(--primary-foreground);
    border: none;
    border-radius: var(--radius);
    font-family: var(--font-body);
    font-weight: 600;
    font-size: 1rem;
    cursor: pointer;
    margin-top: 6px;
    transition: opacity 0.15s ease;
  }

  button:hover { opacity: 0.9; }

  button:focus-visible,
  .field:has(input:focus-visible),
  .field:has(select:focus-visible) {
    outline: 2px solid var(--accent);
    outline-offset: 2px;
  }

  .status {
    font-family: var(--font-mono);
    font-size: 0.7rem;
    color: var(--muted-foreground);
    min-height: 1em;
  }

  .status.ok { color: var(--accent); }

  @media (prefers-reduced-motion: reduce) {
    .field, button { transition: none; }
  }
</style>
<script>
  (function () {
    var isDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
    document.documentElement.classList.toggle('dark', isDark);
  })();
</script>
</head>
<body>
  <div class="card">
    <div class="identity">
      <div class="name">Cardputer Now Playing Setup</div>
    </div>

    <form onsubmit="handleSubmit(event)">
      <div class="field">
        <label for="ssid_select">Wi-Fi network</label>
        <select id="ssid_select" onchange="checkManualSSID()">
)rawliteral";

    html += networkOptions;

    html += R"rawliteral(
          <option value="__custom__">Manual entry&hellip;</option>
        </select>
      </div>

      <div class="field" id="manual_ssid_div" style="display:none;">
        <label for="manual_ssid">Network name</label>
        <input type="text" id="manual_ssid" name="manual_ssid" placeholder="Network name">
      </div>

      <div class="field">
        <label for="password">Wi-Fi password</label>
        <div class="pass-row">
          <input type="password" id="password" name="password" placeholder="Password">
          <label class="toggle-pass" for="show_pass">
            <input type="checkbox" id="show_pass" onclick="togglePass()">
            Show
          </label>
        </div>
      </div>

      <div class="field">
        <label for="host">Mac LAN IP address</label>
        <input type="text" id="host" name="host" placeholder="192.168.1.150" required>
      </div>

      <div class="field">
        <label for="port">Bridge port</label>
        <input type="number" id="port" name="port" value="58329" min="1" max="65535" required>
      </div>

      <button type="submit">Save &amp; connect</button>
      <div class="status" id="status"></div>
    </form>
  </div>

  <script>
    function checkManualSSID() {
      var sel = document.getElementById("ssid_select");
      var manualDiv = document.getElementById("manual_ssid_div");
      manualDiv.style.display = sel.value === "__custom__" ? "block" : "none";
    }

    function togglePass() {
      var passInput = document.getElementById("password");
      var cb = document.getElementById("show_pass");
      passInput.type = cb.checked ? "text" : "password";
    }

    function handleSubmit(e) {
      e.preventDefault();
      var sel = document.getElementById("ssid_select");
      var manual = document.getElementById("manual_ssid");
      var ssid = (sel.value === "__custom__" || !sel.value) ? manual.value : sel.value;
      var pass = document.getElementById("password").value;
      var host = document.getElementById("host").value;
      var port = document.getElementById("port").value;

      var status = document.getElementById("status");
      status.textContent = "Saving...";
      status.className = "status";

      var body = "ssid=" + encodeURIComponent(ssid) +
                 "&password=" + encodeURIComponent(pass) +
                 "&host=" + encodeURIComponent(host) +
                 "&port=" + encodeURIComponent(port);

      fetch("/save", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: body
      }).then(function(res) {
        status.textContent = "Saved. Cardputer will reboot and reconnect.";
        status.classList.add("ok");
      }).catch(function(err) {
        status.textContent = "Saved. Cardputer will reboot and reconnect.";
        status.classList.add("ok");
      });
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

    server.send(200, "text/plain", "OK");
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
