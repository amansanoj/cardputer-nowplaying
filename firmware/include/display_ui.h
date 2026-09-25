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
  String clock = "";
};

struct LineScroller {
  String lastText = "";
  unsigned long pauseStartTime = 0;
  unsigned long scrollStartTime = 0;
  bool isScrolling = false;

  void reset(unsigned long now) {
    pauseStartTime = now;
    scrollStartTime = 0;
    isScrolling = false;
  }

  int16_t getOffset(const String &text, int16_t maxW, unsigned long now) {
    if (text != lastText) {
      lastText = text;
      reset(now);
      return 0;
    }

    int16_t textW = text.length() * 6;
    if (textW <= maxW) {
      return 0;
    }

    const int16_t gapSpaces = 5;
    int16_t loopW = (text.length() + gapSpaces) * 6;
    const unsigned long PAUSE_DURATION = 10000; // 10 seconds stationary pause
    const unsigned long SCROLL_SPEED = 40;      // 40ms per pixel

    unsigned long scrollDuration = (unsigned long)loopW * SCROLL_SPEED;

    if (!isScrolling) {
      if (pauseStartTime == 0) pauseStartTime = now;
      if (now - pauseStartTime >= PAUSE_DURATION) {
        isScrolling = true;
        scrollStartTime = now;
        return 0;
      }
      return 0;
    } else {
      unsigned long scrollElapsed = now - scrollStartTime;
      if (scrollElapsed >= scrollDuration) {
        isScrolling = false;
        pauseStartTime = now; // Reset timer to 10 seconds upon finishing scroll
        return 0;
      }

      int16_t offset = (int16_t)(scrollElapsed / SCROLL_SPEED);
      if (offset >= loopW) {
        isScrolling = false;
        pauseStartTime = now; // Reset timer to 10 seconds upon finishing scroll
        return 0;
      }
      return offset;
    }
  }
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

  LineScroller titleScroller;
  LineScroller artistScroller;
  LineScroller albumScroller;

  void drawPlaceholderArt(int16_t x, int16_t y, int16_t size);
  void drawStatusBar(int16_t x, int16_t y, int16_t rightX, const String& clockTime);
  void getBatteryInfo(uint8_t &pct, bool &isCharging);
  void drawScrollingText(int16_t x, int16_t y, const String& text, int16_t maxW, uint16_t color, LineScroller &scroller, unsigned long now);
  String formatTime(uint32_t totalSeconds);
};
