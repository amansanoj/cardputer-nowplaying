#pragma once
#include "config.h"
#include "display_ui.h"

class MusicClient {
public:
  MusicClient();
  bool connectWiFi(DisplayUI& ui);
  bool fetchMetadata(TrackInfo& info);
  bool fetchArtwork(uint8_t* buffer, size_t bufferSize);

private:
  WiFiClient wifiClient;
  HTTPClient httpClient;
};
