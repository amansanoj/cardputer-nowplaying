#if __has_include("config.h")
#include "config.h"
#include "display_ui.h"
#include "keyboard_driver.h"
#include "artwork_cache.h"
#include "config_manager.h"
#include "music_client.h"
#if !defined(TARGET_WOKWI_SIMULATOR)
#include "ble_manager.h"
#endif
#else
#include "../include/config.h"
#include "../include/display_ui.h"
#include "../include/keyboard_driver.h"
#include "../include/artwork_cache.h"
#include "../include/config_manager.h"
#include "../include/music_client.h"
#if !defined(TARGET_WOKWI_SIMULATOR)
#include "../include/ble_manager.h"
#endif
#endif

static DisplayUI ui;
static KeyboardDriver keyboard;
static ArtworkCache artworkCache;
static ConfigManager configManager;
static AppConfig appConfig;
static TrackInfo currentTrack;

static String cachedArtworkId = "";
static uint8_t rawArtBuffer[ARTWORK_SIZE * ARTWORK_SIZE * 2];

static MusicClient musicClient;
#if !defined(TARGET_WOKWI_SIMULATOR)
static BleManager bleManager;
#endif

static bool isWiFiMode = false;
static unsigned long lastPollTime = 0;
static unsigned long lastRenderTime = 0;
static uint32_t serverElapsed = 0;
static uint32_t interpolatedElapsed = 0;

static char activeControlKey = 0;
static unsigned long activeControlKeyTime = 0;

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("==================================================");
  Serial.println(" Cardputer Now Playing Display (v4.0.0)");
  Serial.println("==================================================");

  // Initialize double-buffered ST7789 display
  ui.init();

  // Initialize on-device artwork cache (MicroSD / LittleFS)
  artworkCache.begin();

  // Initialize hardware keyboard (TCA8418 on Cardputer-Adv or Serial fallback)
  keyboard.begin();

  // Load persistent configuration
  configManager.load(appConfig);
  if (appConfig.rotation == 1 || appConfig.rotation == 3) {
    ui.setRotation(appConfig.rotation);
  }

  // Check G0 button held on boot to enter Setup Portal
  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) == LOW) {
    delay(50);
    if (digitalRead(0) == LOW) {
      Serial.println("[Boot] G0 button held! Launching Setup Portal...");
      if (configManager.runSetupPortal(ui, appConfig, &keyboard)) {
        Serial.println("[Setup] Saved! Rebooting to apply new configuration...");
        ui.renderStatus("Rebooting...", "Applying settings");
        delay(600);
        ESP.restart();
      }
    }
  }

#if defined(TARGET_WOKWI_SIMULATOR)
  // Wokwi Simulator: Direct fast boot into Wi-Fi mode
  isWiFiMode = true;
  Serial.println("[Wokwi] Connecting directly to Wokwi-GUEST...");
  musicClient.setServer(WOKWI_DEFAULT_HOST, DEFAULT_PORT);
  musicClient.connectWiFi(ui, WOKWI_DEFAULT_SSID, WOKWI_DEFAULT_PASS);
  ui.render(currentTrack, 0, 0);
#else
  if (appConfig.transportMode == TRANSPORT_WIFI && appConfig.wifiSsid.length() > 0 && appConfig.macHost.length() > 0) {
    isWiFiMode = true;
    Serial.println("[Cardputer-Adv] Starting Wi-Fi / WebSocket mode...");
    musicClient.setServer(appConfig.macHost, appConfig.port);
    musicClient.connectWiFi(ui, appConfig.wifiSsid, appConfig.wifiPassword);
    ui.render(currentTrack, 0, 0);
  } else {
    isWiFiMode = false;
    Serial.println("[Cardputer-Adv] Starting Bluetooth Low Energy (BLE) mode...");
    ui.renderStatus("Bluetooth", "Ready to pair...");
    bleManager.begin();
  }
#endif

  lastPollTime = 0;
  lastRenderTime = 0;
}

void loop() {
  unsigned long now = millis();

  // 1. Keyboard & Serial Input Handling
  char key = keyboard.getKey();
  if (key != 0) {
    activeControlKey = key;
    activeControlKeyTime = now;

    if (key == 's' || key == 'S') {
      if (configManager.runSetupPortal(ui, appConfig, &keyboard)) {
        Serial.println("[Setup] Saved! Rebooting to apply new configuration...");
        ui.renderStatus("Rebooting...", "Applying settings");
        delay(600);
        ESP.restart();
      }
    } else if (key == 'f' || key == 'F') {
      ui.toggleRotation();
      appConfig.rotation = ui.getRotation();
      configManager.save(appConfig);
      Serial.printf("[Display] Screen flipped to rotation %d and saved.\n", appConfig.rotation);
    } else if (isWiFiMode) {
      if (key == ' ') {
        musicClient.sendCommand("toggle");
        if (!musicClient.isWsConnected()) lastPollTime = 0;
      } else if (key == '.' || key == '>' || key == 'p' || key == 'P') {
        musicClient.sendCommand("previous");
        if (!musicClient.isWsConnected()) lastPollTime = 0;
      } else if (key == ';' || key == ':' || key == 'n' || key == 'N') {
        musicClient.sendCommand("next");
        if (!musicClient.isWsConnected()) lastPollTime = 0;
      } else if (key == ',' || key == '<' || key == '[') {
        musicClient.sendCommand("backward");
        if (!musicClient.isWsConnected()) lastPollTime = 0;
      } else if (key == '/' || key == '?' || key == ']') {
        musicClient.sendCommand("forward");
        if (!musicClient.isWsConnected()) lastPollTime = 0;
      }
    }
#if !defined(TARGET_WOKWI_SIMULATOR)
    else {
      if (key == ' ') {
        bleManager.sendCommand("toggle");
      } else if (key == '.' || key == '>' || key == 'p' || key == 'P') {
        bleManager.sendCommand("prev");
      } else if (key == ';' || key == ':' || key == 'n' || key == 'N') {
        bleManager.sendCommand("next");
      } else if (key == ',' || key == '<' || key == '[') {
        bleManager.sendCommand("rw");
      } else if (key == '/' || key == '?' || key == ']') {
        bleManager.sendCommand("ff");
      }
    }
#endif
  }

  // Check G0 button long-press in loop to launch setup portal
  static unsigned long g0HoldStart = 0;
  if (digitalRead(0) == LOW) {
    if (g0HoldStart == 0) g0HoldStart = now;
    else if (now - g0HoldStart > 1200) {
      if (configManager.runSetupPortal(ui, appConfig, &keyboard)) {
        Serial.println("[Setup] Saved! Rebooting to apply new configuration...");
        ui.renderStatus("Rebooting...", "Applying settings");
        delay(600);
        ESP.restart();
      }
      g0HoldStart = 0;
    }
  } else {
    g0HoldStart = 0;
  }

  // Visual highlight for active key hint in footer
  char highlightKey = (now - activeControlKeyTime < 350) ? activeControlKey : 0;

#if !defined(TARGET_WOKWI_SIMULATOR)
  if (!isWiFiMode) {
    ui.setRadioStatus(RADIO_BLE, bleManager.isConnected());
  } else {
    ui.setRadioStatus(RADIO_WIFI, WiFi.status() == WL_CONNECTED);
  }
#else
  ui.setRadioStatus(RADIO_WIFI, WiFi.status() == WL_CONNECTED);
#endif

  // 2. Transport Communication
  if (isWiFiMode) {
    // WebSocket push update
    TrackInfo pushedInfo;
    if (musicClient.popTrackUpdate(pushedInfo)) {
      currentTrack = pushedInfo;
      serverElapsed = currentTrack.elapsed;
      lastPollTime = now;

      if (currentTrack.isRunning && currentTrack.state != "stopped" &&
          currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
        if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
        } else if (musicClient.fetchArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
          artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
        }
      }

      interpolatedElapsed = serverElapsed;
      ui.render(currentTrack, interpolatedElapsed, highlightKey);
      lastRenderTime = now;
    }

    // Periodic HTTP poll fallback
    unsigned long activePollInterval = musicClient.isWsConnected() ? 30000 : POLL_INTERVAL_MS;
    if (now - lastPollTime >= activePollInterval || lastPollTime == 0) {
      TrackInfo newInfo;
      if (musicClient.fetchMetadata(newInfo)) {
        currentTrack = newInfo;
        serverElapsed = currentTrack.elapsed;
        lastPollTime = now;

        if (currentTrack.isRunning && currentTrack.state != "stopped" &&
            currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
          if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
            ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
            cachedArtworkId = currentTrack.artworkId;
          } else if (musicClient.fetchArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
            ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
            cachedArtworkId = currentTrack.artworkId;
            artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
          }
        }

        interpolatedElapsed = serverElapsed;
        ui.render(currentTrack, interpolatedElapsed, highlightKey);
        lastRenderTime = now;
      } else {
        if (!musicClient.isWsConnected()) {
          lastPollTime = now - (POLL_INTERVAL_MS - 2000);
        }
      }
    }
  }
#if !defined(TARGET_WOKWI_SIMULATOR)
  else {
    // BLE metadata update
    TrackInfo bleTrack;
    if (bleManager.popTrackUpdate(bleTrack)) {
      currentTrack = bleTrack;
      serverElapsed = currentTrack.elapsed;
      lastPollTime = now;

      static String requestedArtworkId = "";
      static unsigned long lastArtRequestTime = 0;

      if (currentTrack.isRunning && currentTrack.state != "stopped" &&
          currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
        if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
          Serial.printf("[BLE] Cache hit on %s. Loaded directly.\n", artworkCache.getStorageName());
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
          requestedArtworkId = currentTrack.artworkId;
        } else {
          if (currentTrack.artworkId != requestedArtworkId || (now - lastArtRequestTime > 6000)) {
            Serial.printf("[BLE] Requesting artwork stream for %s...\n", currentTrack.artworkId.c_str());
            requestedArtworkId = currentTrack.artworkId;
            lastArtRequestTime = now;
            bleManager.requestArtwork(currentTrack.artworkId);
          }
        }
      }

      interpolatedElapsed = serverElapsed;
      ui.render(currentTrack, interpolatedElapsed, highlightKey);
      lastRenderTime = now;
    }

    // Full artwork binary stream received over BLE
    if (bleManager.hasNewArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
      ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
      cachedArtworkId = currentTrack.artworkId;
      artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
      ui.render(currentTrack, interpolatedElapsed, highlightKey);
    }
  }
#endif

  // 3. Smooth screen refresh & marquee text scrolling (50ms / 20 FPS)
  const unsigned long RENDER_INTERVAL_MS = 50;
  if (now - lastRenderTime >= RENDER_INTERVAL_MS) {
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

    ui.render(currentTrack, interpolatedElapsed, highlightKey);
  }

  delay(10);
}
