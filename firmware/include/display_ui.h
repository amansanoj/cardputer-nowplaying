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

// Control glyph types for standard footer
enum ControlIcon {
  ICON_NONE = 0,
  ICON_PREV,       // |<
  ICON_REWIND,     // << (-10s)
  ICON_PLAY,       // >
  ICON_PAUSE,      // ||
  ICON_PLAYPAUSE,  // >||
  ICON_FORWARD,    // >> (+10s)
  ICON_NEXT,       // >|
  ICON_SETUP       // gear / settings
};

struct FooterControl {
  String label;
  String keyHint;
  ControlIcon icon;
  bool active;
};

class DisplayUI {
public:
  DisplayUI();
  void init();
  void setArtworkData(const uint8_t* rawData, size_t length);
  void render(const TrackInfo& info, uint32_t currentElapsed, char activeKey = 0);
  void renderStatus(const String& line1, const String& line2 = "");
  void renderSetupScreen(const String& apName, const String& apIP);

  // Standard Header & Footer Components (modular & template-ready)
  void drawHeader(const String& clockTime, const String& screenTitle = "Now Playing");
  void drawFooter(const FooterControl* controls, size_t count);
  void drawPlaybackFooter(const String& state, char activeKey = 0);

  // Modular text helper for fixed 2-line title layout
  static void splitTitle(const String& title, int16_t maxW, String& line1, String& line2);

private:
  Adafruit_ST7789 tft;
  GFXcanvas16 canvas; // 240x135 16-bit off-screen double-buffer
  uint16_t artworkBuffer[ARTWORK_SIZE * ARTWORK_SIZE];
  bool hasArtwork;

  // Shared Synchronized Marquee Timing
  String lastTrackKey;
  unsigned long sharedPauseStartTime;
  unsigned long sharedScrollStartTime;
  bool isSharedScrolling;
  String lastKnownClock;

  void drawArtwork(int16_t x, int16_t y, int16_t size);
  void drawPlaceholderArt(int16_t x, int16_t y, int16_t size);
  void getBatteryInfo(uint8_t &pct, bool &isCharging);
  void drawControlIcon(int16_t x, int16_t y, ControlIcon icon, uint16_t color);
  void drawScrollingText(int16_t x, int16_t y, const String& text, int16_t maxW, uint16_t color, int16_t offset);
  int16_t getLoopWidth(const String& text, int16_t maxW);
  String formatTime(uint32_t totalSeconds);
};


