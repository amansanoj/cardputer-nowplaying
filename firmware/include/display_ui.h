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

enum RadioMode {
  RADIO_NONE = 0,
  RADIO_WIFI,
  RADIO_BLE
};

class DisplayUI {
public:
  DisplayUI();
  void init();
  void setArtworkData(const uint8_t* rawData, size_t length);
  void render(const TrackInfo& info, uint32_t currentElapsed, char activeKey = 0);
  void renderStatus(const String& line1, const String& line2 = "");
  void renderSetupScreen(const String& apName, const String& apIP);

  // Radio & Connection Status
  void setRadioStatus(RadioMode mode, bool connected) {
    currentRadioMode = mode;
    isRadioConnected = connected;
  }

  // Standard Header & Footer Components (modular & template-ready)
  void drawHeader(const String& clockTime, const String& screenTitle = "Now Playing");
  void drawFooter(const FooterControl* controls, size_t count);
  void drawPlaybackFooter(const String& state, char activeKey = 0);

  // Screen rotation controls (180-degree flip)
  void setRotation(uint8_t rot);
  void toggleRotation();
  uint8_t getRotation() const { return currentRotation; }

  // Force redraw on next render cycle
  void markDirty() { forceRedraw = true; }

  // Modular text helper for fixed 2-line title layout
  static void splitTitle(const String& title, int16_t maxW, String& line1, String& line2);

private:
  Adafruit_ST7789 tft;
  uint8_t currentRotation;
  GFXcanvas16 canvas; // 240x135 16-bit off-screen double-buffer
  uint16_t artworkBuffer[ARTWORK_SIZE * ARTWORK_SIZE];
  bool hasArtwork;

  // Dirty frame repaint optimizations
  bool forceRedraw;
  uint32_t lastRenderedElapsed;
  char lastRenderedKey;
  String lastRenderedState;
  String lastRenderedArtworkId;
  String lastRenderedClock;
  int16_t lastRenderedOffsetsSum;

  // Shared Synchronized Marquee Timing
  String lastTitle;
  String lastArtist;
  String lastAlbum;
  unsigned long sharedPauseStartTime;
  unsigned long sharedScrollStartTime;
  bool isSharedScrolling;
  String lastKnownClock;

  // Radio status
  RadioMode currentRadioMode;
  bool isRadioConnected;

  void drawArtwork(int16_t x, int16_t y, int16_t size);
  void drawPlaceholderArt(int16_t x, int16_t y, int16_t size);
  void getBatteryInfo(uint8_t &pct, bool &isCharging);
  void drawControlIcon(int16_t x, int16_t y, ControlIcon icon, uint16_t color);
  void drawScrollingText(int16_t x, int16_t y, const String& text, int16_t maxW, uint16_t color, int16_t offset);
  int16_t getLoopWidth(const String& text, int16_t maxW);
  static void formatTime(uint32_t totalSeconds, char* outBuf, size_t bufSize);
};


