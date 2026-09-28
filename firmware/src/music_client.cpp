#if __has_include("music_client.h")
#include "music_client.h"
#else
#include "../include/music_client.h"
#endif

MusicClient::MusicClient()
  : serverBaseUrl(""),
    wsUrl(""),
    wsClient(nullptr),
    wsConnected(false),
    hasNewMetadata(false) {
  wsMutex = xSemaphoreCreateMutex();
}

MusicClient::~MusicClient() {
  stopWebSocket();
  if (wsMutex) {
    vSemaphoreDelete(wsMutex);
    wsMutex = nullptr;
  }
}

void MusicClient::setServer(const String& host, uint16_t port) {
  String cleanHost = host;
  cleanHost.trim();
  if (cleanHost.startsWith("http://")) {
    serverBaseUrl = cleanHost;
    wsUrl = "ws://" + cleanHost.substring(7);
  } else if (cleanHost.startsWith("https://")) {
    serverBaseUrl = cleanHost;
    wsUrl = "wss://" + cleanHost.substring(8);
  } else {
    serverBaseUrl = "http://" + cleanHost;
    wsUrl = "ws://" + cleanHost;
  }

  int colonIdx = serverBaseUrl.lastIndexOf(':');
  if (colonIdx <= 5 && port > 0) {
    serverBaseUrl += ":" + String(port);
    wsUrl += ":" + String(port);
  }
  wsUrl += "/ws";

  Serial.printf("[MusicClient] HTTP Endpoint: %s\n", serverBaseUrl.c_str());
  Serial.printf("[MusicClient] WS Endpoint:   %s\n", wsUrl.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    startWebSocket();
  }
}

void MusicClient::websocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  MusicClient* self = static_cast<MusicClient*>(handler_args);
  if (self) {
    self->handleWsEvent(event_id, event_data);
  }
}

void MusicClient::handleWsEvent(int32_t event_id, void* event_data) {
  esp_websocket_event_data_t* data = (esp_websocket_event_data_t*)event_data;
  switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      Serial.println("[WS] Connected to Companion WebSocket server!");
      if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        wsConnected = true;
        xSemaphoreGive(wsMutex);
      }
      break;

    case WEBSOCKET_EVENT_DISCONNECTED:
      Serial.println("[WS] Disconnected from Companion WebSocket server.");
      if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        wsConnected = false;
        xSemaphoreGive(wsMutex);
      }
      break;

    case WEBSOCKET_EVENT_DATA:
      if (data && data->data_ptr && data->data_len > 0) {
        // Parse incoming pushed metadata JSON (opcode 0x1 is text frame)
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data->data_ptr, data->data_len);
        if (!err) {
          TrackInfo info;
          info.isRunning = doc["running"] | false;
          info.state     = doc["state"] | "stopped";
          info.title     = doc["title"] | "";
          info.artist    = doc["artist"] | "";
          info.album     = doc["album"] | "";
          info.duration  = doc["duration"] | 0;
          info.elapsed   = doc["elapsed"] | 0;
          info.artworkId = doc["artwork_id"] | "";
          info.clock     = doc["clock"] | "";

          if (doc["epoch"].is<long>()) {
            long epoch = doc["epoch"].as<long>();
            long tzOffset = doc["tz_offset"] | 0;
            timeval tv = { epoch, 0 };
            settimeofday(&tv, nullptr);
            configTime(tzOffset, 0, "pool.ntp.org", "time.google.com");
          }

          if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            latestMetadata = info;
            hasNewMetadata = true;
            xSemaphoreGive(wsMutex);
          }
        }
      }
      break;

    case WEBSOCKET_EVENT_ERROR:
      Serial.println("[WS] WebSocket error encountered.");
      break;

    default:
      break;
  }
}

void MusicClient::startWebSocket() {
  if (wsUrl.length() == 0 || WiFi.status() != WL_CONNECTED) {
    return;
  }

  stopWebSocket();

  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = wsUrl.c_str();
  ws_cfg.disable_auto_reconnect = false;
  ws_cfg.buffer_size = 2048;
  ws_cfg.ping_interval_sec = 10;
  ws_cfg.pingpong_timeout_sec = 15;

  wsClient = esp_websocket_client_init(&ws_cfg);
  if (!wsClient) {
    Serial.println("[WS] Failed to initialize WebSocket client!");
    return;
  }

  esp_websocket_register_events(wsClient, WEBSOCKET_EVENT_ANY, MusicClient::websocketEventHandler, this);
  esp_err_t err = esp_websocket_client_start(wsClient);
  if (err == ESP_OK) {
    Serial.printf("[WS] Client started for %s\n", wsUrl.c_str());
  } else {
    Serial.printf("[WS] Client start failed (0x%x)\n", err);
  }
}

void MusicClient::stopWebSocket() {
  if (wsClient) {
    esp_websocket_client_stop(wsClient);
    esp_websocket_client_destroy(wsClient);
    wsClient = nullptr;
  }
  if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    wsConnected = false;
    hasNewMetadata = false;
    xSemaphoreGive(wsMutex);
  }
}

bool MusicClient::isWsConnected() const {
  return wsConnected;
}

bool MusicClient::popTrackUpdate(TrackInfo& info) {
  bool updated = false;
  if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (hasNewMetadata) {
      info = latestMetadata;
      hasNewMetadata = false;
      updated = true;
    }
    xSemaphoreGive(wsMutex);
  }
  return updated;
}

bool MusicClient::connectWiFi(DisplayUI& ui, const String& ssid, const String& password) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wsConnected && wsClient == nullptr) {
      startWebSocket();
    }
    return true;
  }

  Serial.printf("[WiFi] Connecting to %s...\n", ssid.c_str());
  ui.renderStatus("Connecting to WiFi:", ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 12000)) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    ui.renderStatus("WiFi Connected!", "IP: " + WiFi.localIP().toString());

    // Initialize SNTP background synchronization
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    // Connect WebSocket
    startWebSocket();

    delay(800);
    return true;
  } else {
    Serial.println("[WiFi] Connection failed!");
    ui.renderStatus("WiFi Error", "Connection failed");
    return false;
  }
}

bool MusicClient::fetchMetadata(TrackInfo& info) {
  if (WiFi.status() != WL_CONNECTED || serverBaseUrl.length() == 0) {
    return false;
  }

  String url = serverBaseUrl + METADATA_PATH;
  httpClient.begin(wifiClient, url);
  httpClient.setTimeout(2500);

  int httpCode = httpClient.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP] Metadata GET failed, code: %d\n", httpCode);
    httpClient.end();
    return false;
  }

  String payload = httpClient.getString();
  httpClient.end();

  // Parse JSON response
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[JSON] Deserialization error: %s\n", err.c_str());
    return false;
  }

  info.isRunning = doc["running"] | false;
  info.state     = doc["state"] | "stopped";
  info.title     = doc["title"] | "";
  info.artist    = doc["artist"] | "";
  info.album     = doc["album"] | "";
  info.duration  = doc["duration"] | 0;
  info.elapsed   = doc["elapsed"] | 0;
  info.artworkId = doc["artwork_id"] | "";
  info.clock     = doc["clock"] | "";

  // Synchronize internal hardware RTC from companion bridge timestamp
  if (doc["epoch"].is<long>()) {
    long epoch = doc["epoch"].as<long>();
    long tzOffset = doc["tz_offset"] | 0;
    timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    configTime(tzOffset, 0, "pool.ntp.org", "time.google.com");
  }

  return true;
}

bool MusicClient::fetchArtwork(uint8_t* buffer, size_t bufferSize) {
  if (WiFi.status() != WL_CONNECTED || serverBaseUrl.length() == 0) {
    return false;
  }

  String url = serverBaseUrl + ARTWORK_PATH;
  httpClient.begin(wifiClient, url);
  httpClient.setTimeout(3000);

  int httpCode = httpClient.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP] Artwork GET failed, code: %d\n", httpCode);
    httpClient.end();
    return false;
  }

  int contentLength = httpClient.getSize();
  if (contentLength > (int)bufferSize) {
    Serial.printf("[HTTP] Artwork exceeds buffer: %d > %u\n", contentLength, bufferSize);
    httpClient.end();
    return false;
  }

  WiFiClient* stream = httpClient.getStreamPtr();
  size_t bytesRead = 0;
  unsigned long timeout = millis() + 3000;

  while (httpClient.connected() && bytesRead < bufferSize && millis() < timeout) {
    size_t available = stream->available();
    if (available > 0) {
      size_t toRead = (available < (bufferSize - bytesRead)) ? available : (bufferSize - bytesRead);
      int r = stream->readBytes(reinterpret_cast<char*>(buffer + bytesRead), toRead);
      if (r > 0) {
        bytesRead += r;
      }
    } else {
      delay(2);
    }
  }

  httpClient.end();
  Serial.printf("[HTTP] Fetched %u bytes of artwork.\n", bytesRead);
  return (bytesRead >= bufferSize);
}

bool MusicClient::sendCommand(const String& action) {
  bool wsOk = false;
  if (wsMutex && xSemaphoreTake(wsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    wsOk = (wsClient != nullptr && wsConnected);
    xSemaphoreGive(wsMutex);
  }

  if (wsOk) {
    String payload = "{\"action\":\"" + action + "\"}";
    int sent = esp_websocket_client_send_text(wsClient, payload.c_str(), payload.length(), pdMS_TO_TICKS(100));
    if (sent > 0) {
      Serial.printf("[WS] Dispatched command '%s' instantly via WebSocket.\n", action.c_str());
      return true;
    }
  }

  // Fallback to HTTP POST
  if (WiFi.status() != WL_CONNECTED || serverBaseUrl.length() == 0) {
    return false;
  }

  String url = serverBaseUrl + "/api/" + action;
  httpClient.begin(wifiClient, url);
  httpClient.setTimeout(2000);

  int httpCode = httpClient.POST("");
  bool success = (httpCode == HTTP_CODE_OK);
  httpClient.end();

  Serial.printf("[HTTP] Command '%s' fallback result: %d\n", action.c_str(), httpCode);
  return success;
}
