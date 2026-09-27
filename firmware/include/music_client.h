#pragma once
#include "config.h"
#include "display_ui.h"
#include <esp_websocket_client.h>
#include <freertos/semphr.h>

class MusicClient {
public:
  MusicClient();
  ~MusicClient();

  void setServer(const String& host, uint16_t port);
  bool connectWiFi(DisplayUI& ui, const String& ssid, const String& password);

  // WebSocket lifecycle & state
  void startWebSocket();
  void stopWebSocket();
  bool isWsConnected() const;

  // Checks and pops newly pushed metadata from WebSocket
  bool popTrackUpdate(TrackInfo& info);

  // Fallback HTTP operations (and artwork binary fetch)
  bool fetchMetadata(TrackInfo& info);
  bool fetchArtwork(uint8_t* buffer, size_t bufferSize);

  // Instant command delivery (WebSocket prioritized, HTTP fallback)
  bool sendCommand(const String& action);

private:
  static void websocketEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
  void handleWsEvent(int32_t event_id, void* event_data);

  String serverBaseUrl;
  String wsUrl;
  WiFiClient wifiClient;
  HTTPClient httpClient;

  esp_websocket_client_handle_t wsClient;
  bool wsConnected;
  bool hasNewMetadata;
  TrackInfo latestMetadata;
  portMUX_TYPE stateMux;
};

