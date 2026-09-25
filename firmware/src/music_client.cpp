#if __has_include("music_client.h")
#include "music_client.h"
#else
#include "../include/music_client.h"
#endif

MusicClient::MusicClient() : serverBaseUrl("") {}

void MusicClient::setServer(const String& host, uint16_t port) {
  String cleanHost = host;
  cleanHost.trim();
  if (cleanHost.startsWith("http://") || cleanHost.startsWith("https://")) {
    serverBaseUrl = cleanHost;
  } else {
    serverBaseUrl = "http://" + cleanHost;
  }
  int colonIdx = serverBaseUrl.lastIndexOf(':');
  if (colonIdx <= 5 && port > 0) {
    serverBaseUrl += ":" + String(port);
  }
  Serial.printf("[MusicClient] Configured server endpoint: %s\n", serverBaseUrl.c_str());
}

bool MusicClient::connectWiFi(DisplayUI& ui, const String& ssid, const String& password) {
  if (WiFi.status() == WL_CONNECTED) {
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
