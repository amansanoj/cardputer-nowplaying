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
  void renderSetupScreen(const String& apName, const String& apIP);

private:
  Adafruit_ST7789 tft;
  GFXcanvas16 canvas; // 240x135 16-bit off-screen double-buffer
  uint16_t artworkBuffer[ARTWORK_SIZE * ARTWORK_SIZE];
  bool hasArtwork;

  String lastTrackTitle;
  String lastTrackArtist;
  unsigned long trackStartTime;

  void drawPlaceholderArt(int16_t x, int16_t y, int16_t size);
  void drawScrollingText(int16_t x, int16_t y, const String& text, int16_t maxW, uint16_t color, unsigned long now);
  int16_t calculateScrollOffset(const String& text, int16_t maxW, unsigned long now);
  String formatTime(uint32_t totalSeconds);
};
