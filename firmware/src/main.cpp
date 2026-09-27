#if __has_include("config.h")
#include "config.h"
#include "display_ui.h"
#include "keyboard_driver.h"
#include "artwork_cache.h"
#if defined(TARGET_WOKWI_SIMULATOR)
#include "music_client.h"
#else
#include "ble_manager.h"
#endif
#else
#include "../include/config.h"
#include "../include/display_ui.h"
#include "../include/keyboard_driver.h"
#include "../include/artwork_cache.h"
#if defined(TARGET_WOKWI_SIMULATOR)
#include "../include/music_client.h"
#else
#include "../include/ble_manager.h"
#endif
#endif

static DisplayUI ui;
static KeyboardDriver keyboard;
static ArtworkCache artworkCache;
static TrackInfo currentTrack;

static String cachedArtworkId = "";
static uint8_t rawArtBuffer[ARTWORK_SIZE * ARTWORK_SIZE * 2];

#if defined(TARGET_WOKWI_SIMULATOR)
static MusicClient musicClient;
#else
static BleManager bleManager;
#endif

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

#if defined(TARGET_WOKWI_SIMULATOR)
  // Wokwi Simulator: Direct fast boot into Wi-Fi mode
  Serial.println("[Wokwi] Connecting directly to Wokwi-GUEST...");
  musicClient.setServer(WOKWI_DEFAULT_HOST, DEFAULT_PORT);
  musicClient.connectWiFi(ui, WOKWI_DEFAULT_SSID, WOKWI_DEFAULT_PASS);
#else
  // Physical Cardputer: Start Bluetooth Low Energy (BLE)
  Serial.println("[Cardputer] Starting Bluetooth Low Energy mode...");
  ui.renderStatus("Bluetooth", "Ready to pair...");
  bleManager.begin();
#endif

  lastPollTime = millis();
  lastRenderTime = millis();
}

void loop() {
  unsigned long now = millis();

  // 1. Keyboard & Serial Input Handling
  char key = keyboard.getKey();
  if (key != 0) {
    activeControlKey = key;
    activeControlKeyTime = now;

#if defined(TARGET_WOKWI_SIMULATOR)
    if (key == ' ') {
      musicClient.sendCommand("toggle");
      if (!musicClient.isWsConnected()) lastPollTime = 0;
    } else if (key == 'p' || key == 'P') {
      musicClient.sendCommand("previous");
      if (!musicClient.isWsConnected()) lastPollTime = 0;
    } else if (key == ',' || key == '<' || key == '[') {
      musicClient.sendCommand("backward");
      if (!musicClient.isWsConnected()) lastPollTime = 0;
    } else if (key == '.' || key == '>' || key == ']') {
      musicClient.sendCommand("forward");
      if (!musicClient.isWsConnected()) lastPollTime = 0;
    } else if (key == 'n' || key == 'N') {
      musicClient.sendCommand("next");
      if (!musicClient.isWsConnected()) lastPollTime = 0;
    }
#else
    if (key == ' ') {
      bleManager.sendCommand("toggle");
    } else if (key == 'p' || key == 'P') {
      bleManager.sendCommand("prev");
    } else if (key == ',' || key == '<' || key == '[') {
      bleManager.sendCommand("rw");
    } else if (key == '.' || key == '>' || key == ']') {
      bleManager.sendCommand("ff");
    } else if (key == 'n' || key == 'N') {
      bleManager.sendCommand("next");
    }
#endif
  }

  // Visual highlight for active key hint in footer
  char highlightKey = (now - activeControlKeyTime < 350) ? activeControlKey : 0;

#if defined(TARGET_WOKWI_SIMULATOR)
  // =========================================================================
  // SIMULATOR TRANSPORT: WebSocket Push & HTTP Poll Fallback
  // =========================================================================
  TrackInfo pushedInfo;
  if (musicClient.popTrackUpdate(pushedInfo)) {
    currentTrack = pushedInfo;
    serverElapsed = currentTrack.elapsed;
    lastPollTime = now;

    if (currentTrack.isRunning && currentTrack.state != "stopped" &&
        currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
      if (artworkCache.hasArtwork(currentTrack.artworkId)) {
        if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
        }
      } else {
        if (musicClient.fetchArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
          artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
        }
      }
    }

    interpolatedElapsed = serverElapsed;
    ui.render(currentTrack, interpolatedElapsed, highlightKey);
    lastRenderTime = now;
  }

  unsigned long activePollInterval = musicClient.isWsConnected() ? 30000 : POLL_INTERVAL_MS;
  if (now - lastPollTime >= activePollInterval || lastPollTime == 0) {
    TrackInfo newInfo;
    if (musicClient.fetchMetadata(newInfo)) {
      currentTrack = newInfo;
      serverElapsed = currentTrack.elapsed;
      lastPollTime = now;

      if (currentTrack.isRunning && currentTrack.state != "stopped" &&
          currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
        if (artworkCache.hasArtwork(currentTrack.artworkId)) {
          if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
            ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
            cachedArtworkId = currentTrack.artworkId;
          }
        } else {
          if (musicClient.fetchArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
            ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
            cachedArtworkId = currentTrack.artworkId;
            artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
          }
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

#else
  // =========================================================================
  // PHYSICAL CARDPUTER TRANSPORT: Bluetooth Low Energy (BLE)
  // =========================================================================
  TrackInfo bleTrack;
  if (bleManager.popTrackUpdate(bleTrack)) {
    currentTrack = bleTrack;
    serverElapsed = currentTrack.elapsed;
    lastPollTime = now;

    if (currentTrack.isRunning && currentTrack.state != "stopped" &&
        currentTrack.artworkId.length() > 0 && currentTrack.artworkId != cachedArtworkId) {
      if (artworkCache.hasArtwork(currentTrack.artworkId)) {
        Serial.printf("[BLE] Cache hit on %s. Loading in 4ms...\n", artworkCache.getStorageName());
        if (artworkCache.loadArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer))) {
          ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
          cachedArtworkId = currentTrack.artworkId;
        }
      } else {
        Serial.printf("[BLE] Requesting artwork stream for %s...\n", currentTrack.artworkId.c_str());
        bleManager.requestArtwork(currentTrack.artworkId);
      }
    }

    interpolatedElapsed = serverElapsed;
    ui.render(currentTrack, interpolatedElapsed, highlightKey);
    lastRenderTime = now;
  }

  // Check if a full artwork binary stream completed over BLE
  if (bleManager.hasNewArtwork(rawArtBuffer, sizeof(rawArtBuffer))) {
    ui.setArtworkData(rawArtBuffer, sizeof(rawArtBuffer));
    cachedArtworkId = currentTrack.artworkId;
    artworkCache.saveArtwork(currentTrack.artworkId, rawArtBuffer, sizeof(rawArtBuffer));
    ui.render(currentTrack, interpolatedElapsed, highlightKey);
  }
#endif

  // 5. Smooth screen refresh & marquee text scrolling (50ms / 20 FPS)
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
