#if __has_include("music_client.h")
#include "music_client.h"
#else
#include "../include/music_client.h"
#endif

MusicClient::MusicClient() {}

bool MusicClient::connectWiFi(DisplayUI& ui) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("[WiFi] Connecting to %s...\n", WIFI_SSID);
  ui.renderStatus("Connecting to WiFi:", String(WIFI_SSID));

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start < 15000)) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    ui.renderStatus("WiFi Connected!", "IP: " + WiFi.localIP().toString());
    delay(1000);
    return true;
  } else {
    Serial.println("[WiFi] Connection failed!");
    ui.renderStatus("WiFi Error", "Retrying...");
    return false;
  }
}

bool MusicClient::fetchMetadata(TrackInfo& info) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = String(SERVER_HOST) + METADATA_PATH;
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

  return true;
}

bool MusicClient::fetchArtwork(uint8_t* buffer, size_t bufferSize) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  String url = String(SERVER_HOST) + ARTWORK_PATH;
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
