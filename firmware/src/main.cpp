#if __has_include("config.h")
#include "config.h"
#include "display_ui.h"
#include "music_client.h"
#else
#include "../include/config.h"
#include "../include/display_ui.h"
#include "../include/music_client.h"
#endif

static DisplayUI ui;
static MusicClient musicClient;
static TrackInfo currentTrack;

static String cachedArtworkId = "";
static uint8_t rawArtBuffer[ARTWORK_SIZE * ARTWORK_SIZE * 2];

static unsigned long lastPollTime = 0;
static unsigned long lastRenderTime = 0;
static uint32_t serverElapsed = 0;
static uint32_t interpolatedElapsed = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("==================================================");
  Serial.println(" ESP32-S3 Apple Music Now Playing Display");
  Serial.println("==================================================");

  // Initialize double-buffered display
  ui.init();
  ui.renderStatus("Apple Music", "Connecting WiFi...");

  // Connect to Wi-Fi (Wokwi-GUEST or configured network)
  musicClient.connectWiFi(ui);

  // Initial immediate poll
  lastPollTime = 0;
  lastRenderTime = 0;
}

void loop() {
  unsigned long now = millis();

  // 1. Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    musicClient.connectWiFi(ui);
    return;
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
