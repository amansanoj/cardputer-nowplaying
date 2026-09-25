#pragma once
#include "arduino_compat.h"
#include "config.h"

struct TrackInfo {
  bool isRunning = false;
  String state = "stopped"; // "playing", "paused", "stopped"
  String title = "";
  String artist = "";
  String album = "";
  uint32_t duration = 0;
  uint32_t elapsed = 0;
  String artworkId = "";
};

class DisplayUI {
public:
  DisplayUI();
  void init();
  void setArtworkData(const uint8_t* rawData, size_t length);
  void render(const TrackInfo& info, uint32_t currentElapsed);
  void renderStatus(const String& line1, const String& line2 = "");

private:
  Adafruit_ST7789 tft;
  GFXcanvas16 canvas; // 240x135 16-bit off-screen double-buffer
  uint16_t artworkBuffer[ARTWORK_SIZE * ARTWORK_SIZE];
  bool hasArtwork;

  void drawPlaceholderArt(int16_t x, int16_t y, int16_t size);
  void drawTruncatedText(int16_t x, int16_t y, const String& text, int maxChars, uint8_t size = 1);
  String formatTime(uint32_t totalSeconds);
};
