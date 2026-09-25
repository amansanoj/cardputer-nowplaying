#if __has_include("config.h")
#include "config.h"
#include "config_manager.h"
#include "display_ui.h"
#include "music_client.h"
#else
#include "../include/config.h"
#include "../include/config_manager.h"
#include "../include/display_ui.h"
#include "../include/music_client.h"
#endif

static DisplayUI ui;
static MusicClient musicClient;
static ConfigManager configManager;
static AppConfig appConfig;
static TrackInfo currentTrack;

static String cachedArtworkId = "";
static uint8_t rawArtBuffer[ARTWORK_SIZE * ARTWORK_SIZE * 2];

static unsigned long lastPollTime = 0;
static unsigned long lastRenderTime = 0;
static uint32_t serverElapsed = 0;
static uint32_t interpolatedElapsed = 0;
static uint8_t wifiFailCount = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("==================================================");
  Serial.println(" ESP32-S3 Apple Music Now Playing Display");
  Serial.println("==================================================");

  // Configure G0 / Boot button (held at boot to enter Setup Mode)
  pinMode(BTN_SETUP_PIN, INPUT_PULLUP);
  bool forceSetup = (digitalRead(BTN_SETUP_PIN) == LOW);

  // Initialize double-buffered display
  ui.init();
  ui.renderStatus("Apple Music", "Starting up...");

  // Load configuration from NVS flash
  bool isConfigured = configManager.load(appConfig);

  if (forceSetup || !isConfigured) {
    if (forceSetup) {
      Serial.println("[Setup] G0 button pressed at boot. Launching setup mode...");
    } else {
      Serial.println("[Setup] First boot / unconfigured. Launching setup mode...");
    }
    configManager.runSetupPortal(ui, appConfig);
  }

  // Configure server base URL from loaded settings
  musicClient.setServer(appConfig.macHost, appConfig.port);

  // Connect to Wi-Fi
  while (!musicClient.connectWiFi(ui, appConfig.wifiSsid, appConfig.wifiPassword)) {
    wifiFailCount++;
    Serial.printf("[WiFi] Connect attempt %u failed.\n", wifiFailCount);
    if (wifiFailCount >= 3) {
      Serial.println("[WiFi] 3 failed attempts. Entering Setup Portal...");
      configManager.runSetupPortal(ui, appConfig);
      musicClient.setServer(appConfig.macHost, appConfig.port);
      wifiFailCount = 0;
    }
    delay(1000);
  }

  // Initial immediate poll
  lastPollTime = 0;
  lastRenderTime = 0;
}

void loop() {
  unsigned long now = millis();

  // Allow entering setup anytime by pressing G0 button or typing "SETUP" in Serial
  if (digitalRead(BTN_SETUP_PIN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BTN_SETUP_PIN) == LOW) {
      Serial.println("[Setup] G0 button pressed! Entering Setup Mode...");
      configManager.runSetupPortal(ui, appConfig);
      musicClient.setServer(appConfig.macHost, appConfig.port);
      return;
    }
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.equalsIgnoreCase("SETUP") || cmd.equalsIgnoreCase("CONFIG")) {
      Serial.println("[Setup] Setup requested via Serial command.");
      configManager.runSetupPortal(ui, appConfig);
      musicClient.setServer(appConfig.macHost, appConfig.port);
      return;
    } else if (cmd.equalsIgnoreCase("RESET")) {
      configManager.clear();
      Serial.println("[Setup] Settings erased. Restarting...");
      ESP.restart();
    }
  }

  // 1. Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    if (!musicClient.connectWiFi(ui, appConfig.wifiSsid, appConfig.wifiPassword)) {
      delay(2000);
      return;
    }
  }

  // 2. Poll server for metadata updates
  if (now - lastPollTime >= POLL_INTERVAL_MS || lastPollTime == 0) {
    TrackInfo newInfo;
    if (musicClient.fetchMetadata(newInfo)) {
      currentTrack = newInfo;
      serverElapsed = currentTrack.elapsed;
      lastPollTime = now;

      // Check if artwork has changed
      if (currentTrack.isRunning &&
          currentTrack.state != "stopped" &&
          currentTrack.artworkId.length() > 0 &&
          currentTrack.artworkId != cachedArtworkId) {

        Serial.printf("[Artwork] New artwork detected (%s). Downloading...\n", currentTrack.artworkId.c_str());
        if (musicClient.fetchArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
        }
      }

      // Force an immediate frame redraw on fresh poll
      interpolatedElapsed = serverElapsed;
      ui.render(currentTrack, interpolatedElapsed);
      lastRenderTime = now;
    } else {
      Serial.println("[Poll] Waiting for bridge service...");
      lastPollTime = now - (POLL_INTERVAL_MS - 2000); // Retry sooner on failure
    }
  }

  // 3. Smooth local 1-second time interpolation & screen refresh
  if (now - lastRenderTime >= 1000) {
    lastRenderTime = now;

    if (currentTrack.isRunning && currentTrack.state == "playing") {
      uint32_t offsetSec = (now - lastPollTime) / 1000;
      interpolatedElapsed = serverElapsed + offsetSec;
      if (interpolatedElapsed > currentTrack.duration && currentTrack.duration > 0) {
        interpolatedElapsed = currentTrack.duration;
      }
    } else {
      interpolatedElapsed = serverElapsed;
    }

    // Render frame to off-screen buffer and push to ST7789
    ui.render(currentTrack, interpolatedElapsed);
  }

  // Yield to RTOS and WiFi stack
  delay(20);
}
