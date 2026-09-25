#pragma once
#include "config.h"
#include "display_ui.h"

class MusicClient {
public:
  MusicClient();
  void setServer(const String& host, uint16_t port);
  bool connectWiFi(DisplayUI& ui, const String& ssid, const String& password);
  bool fetchMetadata(TrackInfo& info);
  bool fetchArtwork(uint8_t* buffer, size_t bufferSize);
  bool sendCommand(const String& action);

private:
  String serverBaseUrl;
  WiFiClient wifiClient;
  HTTPClient httpClient;
};

