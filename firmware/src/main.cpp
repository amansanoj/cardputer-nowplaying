#if __has_include("config.h")
#include "config.h"
#include "config_manager.h"
#include "display_ui.h"
#include "keyboard_driver.h"
#include "music_client.h"
#else
#include "../include/config.h"
#include "../include/config_manager.h"
#include "../include/display_ui.h"
#include "../include/keyboard_driver.h"
#include "../include/music_client.h"
#endif

static DisplayUI ui;
static MusicClient musicClient;
static ConfigManager configManager;
static KeyboardDriver keyboard;
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
  Serial.println(" Cardputer Now Playing Display");
  Serial.println("==================================================");

  // Initialize keyboard (TCA8418 on Cardputer-Adv or Serial fallback)
  keyboard.begin();

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

static char activeControlKey = 0;
static unsigned long activeControlKeyTime = 0;

void loop() {
  unsigned long now = millis();

  // 1. Check physical G0 / Boot button
  if (digitalRead(BTN_SETUP_PIN) == LOW) {
    delay(50); // debounce
    if (digitalRead(BTN_SETUP_PIN) == LOW) {
      Serial.println("[Setup] G0 button pressed! Entering Setup Mode...");
      configManager.runSetupPortal(ui, appConfig);
      musicClient.setServer(appConfig.macHost, appConfig.port);
      return;
    }
  }

  // 2. Check Keyboard & Serial Keypresses:
  //    Space=play/pause, p=prev, ,/< = seek back, ./> = seek fwd, n=next, s=setup, r=reset
  char key = keyboard.getKey();
  if (key != 0) {
    if (key == ' ') {
      Serial.println("[Control] Play/Pause toggled!");
      activeControlKey = key;
      activeControlKeyTime = now;
      musicClient.sendCommand("toggle");
      lastPollTime = 0; // trigger immediate refresh
    } else if (key == 'p' || key == 'P') {
      Serial.println("[Control] Previous track!");
      activeControlKey = key;
      activeControlKeyTime = now;
      musicClient.sendCommand("previous");
      lastPollTime = 0;
    } else if (key == ',' || key == '<' || key == '[') {
      Serial.println("[Control] Seek backward (-10s)!");
      activeControlKey = key;
      activeControlKeyTime = now;
      musicClient.sendCommand("backward");
      lastPollTime = 0;
    } else if (key == '.' || key == '>' || key == ']') {
      Serial.println("[Control] Seek forward (+10s)!");
      activeControlKey = key;
      activeControlKeyTime = now;
      musicClient.sendCommand("forward");
      lastPollTime = 0;
    } else if (key == 'n' || key == 'N') {
      Serial.println("[Control] Next track!");
      activeControlKey = key;
      activeControlKeyTime = now;
      musicClient.sendCommand("next");
      lastPollTime = 0;
    } else if (key == 's' || key == 'S') {
      Serial.println("[Setup] Setup requested via keyboard.");
      configManager.runSetupPortal(ui, appConfig);
      musicClient.setServer(appConfig.macHost, appConfig.port);
      return;
    } else if (key == 'r' || key == 'R') {
      Serial.println("[Setup] Reset requested. Clearing NVS...");
      configManager.clear();
      ESP.restart();
    }
  }

  // 3. Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    if (!musicClient.connectWiFi(ui, appConfig.wifiSsid, appConfig.wifiPassword)) {
      delay(2000);
      return;
    }
  }

  // Compute active key highlight for visual tactile feedback in footer
  char highlightKey = (now - activeControlKeyTime < 350) ? activeControlKey : 0;

  // 4. Poll server for metadata updates
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
      ui.render(currentTrack, interpolatedElapsed, highlightKey);
      lastRenderTime = now;
    } else {
      Serial.println("[Poll] Waiting for bridge service...");
      lastPollTime = now - (POLL_INTERVAL_MS - 2000); // Retry sooner on failure
    }
  }

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

    // Render frame to off-screen buffer and push to ST7789
    ui.render(currentTrack, interpolatedElapsed, highlightKey);
  }

  // Yield to RTOS and WiFi stack
  delay(10);
}

