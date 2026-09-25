#if __has_include("display_ui.h")
#include "display_ui.h"
#else
#include "../include/display_ui.h"
#endif

DisplayUI::DisplayUI()
    : tft(TFT_CS, TFT_DC, TFT_RST), canvas(SCREEN_WIDTH, SCREEN_HEIGHT),
      hasArtwork(false), lastTrackTitle(""), lastTrackArtist(""), trackStartTime(0) {
  memset(artworkBuffer, 0, sizeof(artworkBuffer));
}

void DisplayUI::init() {
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH); // Turn on backlight
  }

  // Initialize hardware SPI bus
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // Initialize ST7789 display controller
  tft.init(SCREEN_HEIGHT, SCREEN_WIDTH); // 135x240 native panel
  // Re-bind SPI pins on ESP32-S3 (tft.init resets pins to defaults)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.setRotation(1); // Landscape: 240x135
  tft.fillScreen(COLOR_BG);

  // Initialize off-screen double-buffer canvas
  canvas.setTextWrap(false);
  canvas.fillScreen(COLOR_BG);
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void DisplayUI::setArtworkData(const uint8_t *rawData, size_t length) {
  size_t expectedSize = ARTWORK_SIZE * ARTWORK_SIZE * 2;
  if (length >= expectedSize) {
    // rawData is 16-bit big-endian RGB565 directly matching ST7789 pixel format
    const uint16_t *pixels = reinterpret_cast<const uint16_t *>(rawData);
    for (size_t i = 0; i < ARTWORK_SIZE * ARTWORK_SIZE; i++) {
      // Byte swap from network/big-endian to native uint16_t
      uint16_t p = pixels[i];
      artworkBuffer[i] = (p >> 8) | (p << 8);
    }
    hasArtwork = true;
  }
}

String DisplayUI::formatTime(uint32_t totalSeconds) {
  uint32_t h = totalSeconds / 3600;
  uint32_t m = (totalSeconds % 3600) / 60;
  uint32_t s = totalSeconds % 60;
  char buf[16];
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%u:%02u:%02u", h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "%02u:%02u", m, s);
  }
  return String(buf);
}

int16_t DisplayUI::calculateScrollOffset(const String &text, int16_t maxW, unsigned long now) {
  int16_t textW = text.length() * 6;
  if (textW <= maxW) {
    return 0; // Text fits inside column, no scrolling needed
  }

  const int16_t gapSpaces = 5;
  int16_t loopW = (text.length() + gapSpaces) * 6;

  // Infinite forward scroll: hold at start for 10 seconds, then scroll forward 1 full loop
  const unsigned long PAUSE_INTERVAL = 10000; // 10 seconds stationary pause
  const unsigned long SCROLL_SPEED = 40;      // 40ms per pixel smooth forward glide

  unsigned long scrollDuration = (unsigned long)loopW * SCROLL_SPEED;
  unsigned long totalCycle = PAUSE_INTERVAL + scrollDuration;

  unsigned long elapsed = now - trackStartTime;
  unsigned long cycleTime = elapsed % totalCycle;

  // Hold stationary at start for 10 seconds
  if (cycleTime < PAUSE_INTERVAL) {
    return 0;
  }

  // Scroll forward in one direction
  unsigned long scrollTime = cycleTime - PAUSE_INTERVAL;
  int16_t offset = (int16_t)(scrollTime / SCROLL_SPEED);
  if (offset >= loopW) offset = 0;
  return offset;
}

void DisplayUI::drawScrollingText(int16_t x, int16_t y, const String &text,
                                  int16_t maxW, uint16_t color, unsigned long now) {
  if (text.length() == 0) return;

  int16_t textW = text.length() * 6;

  // Case 1: Fits comfortably in available space
  if (textW <= maxW) {
    canvas.setCursor(x, y);
    canvas.setTextSize(1);
    canvas.setTextColor(color);
    canvas.print(text);
    return;
  }

  // Case 2: Overflows available space -> infinite forward scroll every 10s
  int16_t offset = calculateScrollOffset(text, maxW, now);
  int16_t clipRightX = x + maxW;

  const int16_t gapSpaces = 5;
  int16_t textLen = text.length();
  int16_t loopChars = textLen + gapSpaces;
  int16_t loopW = loopChars * 6;

  int16_t effectiveOffset = offset % loopW;

  canvas.setTextSize(1);
  canvas.setTextColor(color);

  // Determine starting virtual index v based on offset
  int startV = effectiveOffset / 6;
  if (startV > 0) startV--; // margin for partial rendering

  for (int v = startV;; v++) {
    int16_t charX = x - effectiveOffset + (v * 6);
    if (charX >= clipRightX) break; // Reached right clip edge
    if (charX + 6 <= x) continue;   // Before left clip edge

    int idx = v % loopChars;
    char c = (idx < textLen) ? text[idx] : ' ';
    if (c != ' ') {
      canvas.drawChar(charX, y, c, color, COLOR_BG, 1);
    }
  }

  // Hardware clipping gutters:
  // Clean left gutter between album art (x=101) and text start (x=113)
  const int16_t artRight = 15 + ARTWORK_SIZE;
  if (x > artRight) {
    canvas.fillRect(artRight, y, x - artRight, 12, COLOR_BG);
  }
  // Clean right gutter between clipRightX (226) and screen right edge (240)
  if (clipRightX < SCREEN_WIDTH) {
    canvas.fillRect(clipRightX, y, SCREEN_WIDTH - clipRightX, 12, COLOR_BG);
  }
}

void DisplayUI::getBatteryInfo(uint8_t &pct, bool &isCharging) {
#if defined(BAT_ADC_PIN) && (BAT_ADC_PIN >= 0)
  static uint32_t smoothedMv = 0;
  uint32_t rawMv = analogReadMilliVolts(BAT_ADC_PIN) * 2;
  if (smoothedMv == 0) smoothedMv = rawMv;
  else smoothedMv = (smoothedMv * 7 + rawMv) / 8;

  if (smoothedMv >= 4200) {
    isCharging = true;
    pct = 100;
  } else if (smoothedMv <= 3350) {
    isCharging = false;
    pct = 0;
  } else {
    isCharging = false;
    pct = (uint8_t)(((smoothedMv - 3350) * 100) / (4200 - 3350));
    if (pct > 100) pct = 100;
  }
#else
  // Wokwi simulation / USB DevKit fallback
  isCharging = true;
  pct = 100;
#endif
}

void DisplayUI::drawStatusBar(int16_t x, int16_t y, int16_t rightX, const String &clockTime) {
  // 1. Clock (Left side of status row)
  if (clockTime.length() > 0) {
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_MUTED);
    canvas.setCursor(x, y);
    canvas.print(clockTime);
  }

  // 2. Battery & Wi-Fi (Right side of status row)
  uint8_t batPct = 100;
  bool isCharging = false;
  getBatteryInfo(batPct, isCharging);

  // Battery Capsule: 15px wide x 8px high
  const int16_t batW = 15;
  const int16_t batH = 8;
  const int16_t bx = rightX - batW;
  const int16_t by = y;

  // Battery body (rounded rect) & terminal cap
  canvas.drawRoundRect(bx, by, batW - 2, batH, 2, COLOR_MUTED);
  canvas.fillRect(bx + batW - 2, by + 2, 2, 4, COLOR_MUTED);

  if (isCharging) {
    // Crisp minimalist lightning bolt (white)
    canvas.drawLine(bx + 7, by + 1, bx + 5, by + 4, COLOR_TEXT);
    canvas.drawLine(bx + 5, by + 4, bx + 8, by + 4, COLOR_TEXT);
    canvas.drawLine(bx + 8, by + 4, bx + 6, by + 7, COLOR_TEXT);
  } else {
    // Proportional level fill (inner area: 9px wide x 4px high)
    int16_t fillW = (batPct * 9) / 100;
    if (fillW > 0) {
      uint16_t fillColor = (batPct <= 15) ? 0xF800 : COLOR_TEXT;
      canvas.fillRect(bx + 2, by + 2, fillW, 4, fillColor);
    }
  }

  // Battery Percentage Text (e.g. "85%")
  String pctStr = String(batPct) + "%";
  int16_t pctX = bx - 3 - (pctStr.length() * 6);
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_MUTED);
  canvas.setCursor(pctX, by);
  canvas.print(pctStr);

  // Wi-Fi 3-bar signal indicator
  int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int16_t wx = pctX - 6 - 8;
  uint16_t b1 = (rssi > -90) ? COLOR_TEXT : 0x3186;
  uint16_t b2 = (rssi > -75) ? COLOR_TEXT : 0x3186;
  uint16_t b3 = (rssi > -65) ? COLOR_TEXT : 0x3186;

  canvas.fillRect(wx, by + 5, 2, 3, b1);
  canvas.fillRect(wx + 3, by + 3, 2, 5, b2);
  canvas.fillRect(wx + 6, by + 1, 2, 7, b3);
}

void DisplayUI::drawPlaceholderArt(int16_t x, int16_t y, int16_t size) {
  // Strict minimalist: pure pitch-black with a crisp white musical note glyph
  canvas.fillRect(x, y, size, size, COLOR_BG);

  int16_t cx = x + size / 2;
  int16_t cy = y + size / 2;

  // Stems
  canvas.fillRect(cx - 7, cy - 12, 2, 18, COLOR_TEXT);
  canvas.fillRect(cx + 5, cy - 15, 2, 18, COLOR_TEXT);
  // Top beam
  canvas.fillRect(cx - 7, cy - 15, 14, 3, COLOR_TEXT);
  // Note heads
  canvas.fillCircle(cx - 8, cy + 6, 3, COLOR_TEXT);
  canvas.fillCircle(cx + 4, cy + 3, 3, COLOR_TEXT);
}

void DisplayUI::render(const TrackInfo &info, uint32_t currentElapsed) {
  unsigned long now = millis();

  // Clear off-screen buffer (0x0000 Pitch-Black)
  canvas.fillScreen(COLOR_BG);

  // ---------------------------------------------------------------------
  // 1. Equal 15px Padding on Top, Left, and Bottom (to timestamp row)
  //    artX = 15, artY = 15, ARTWORK_SIZE = 86
  //    art_bottom = 15 + 86 = 101
  //    timestamp_top = 116
  //    distance from art_bottom to timestamp_top = 116 - 101 = 15px!
  // ---------------------------------------------------------------------
  const int16_t PAD_ART = 15;
  const int16_t artX = PAD_ART;
  const int16_t artY = PAD_ART;

  if (hasArtwork && info.isRunning && info.state != "stopped") {
    canvas.drawRGBBitmap(artX, artY, artworkBuffer, ARTWORK_SIZE, ARTWORK_SIZE);
  } else {
    drawPlaceholderArt(artX, artY, ARTWORK_SIZE);
  }

  // If paused, overlay minimalist Dynamic Island-style pause pill
  if (info.isRunning && info.state == "paused") {
    const int16_t badgeW = 16;
    const int16_t badgeH = 16;
    const int16_t badgeX = artX + ARTWORK_SIZE - badgeW - 3;
    const int16_t badgeY = artY + ARTWORK_SIZE - badgeH - 3;

    // Dark pill container with subtle border
    canvas.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, COLOR_BG);
    canvas.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, 0x3186);

    // Two crisp white pause bars (2px x 8px)
    canvas.fillRect(badgeX + 4, badgeY + 4, 2, 8, COLOR_TEXT);
    canvas.fillRect(badgeX + 10, badgeY + 4, 2, 8, COLOR_TEXT);
  }

  // ---------------------------------------------------------------------
  // 2. Track Metadata & Status Bar (Right side of art)
  // ---------------------------------------------------------------------
  const int16_t GAP = 12;
  const int16_t tx = artX + ARTWORK_SIZE + GAP;       // 15 + 86 + 12 = 113
  const int16_t textRightX = SCREEN_WIDTH - 14;       // 226 (14px right margin)
  const int16_t maxW = textRightX - tx;               // 113 pixels wide

  // Top Status Bar: Clock, Wi-Fi RSSI, Battery % & Icon
  drawStatusBar(tx, artY, textRightX, info.clock);

  if (!info.isRunning || info.state == "stopped") {
    // Idle state
    int16_t idleY = 35;
    drawScrollingText(tx, idleY, "Apple Music", maxW, COLOR_TEXT, now);
    drawScrollingText(tx, idleY + 18, "Ready / Idle", maxW, COLOR_MUTED, now);
    drawScrollingText(tx, idleY + 36, "No track playing", maxW, COLOR_MUTED, now);
  } else {
    // Check if track changed to reset scroll animation cycle
    if (info.title != lastTrackTitle || info.artist != lastTrackArtist) {
      lastTrackTitle = info.title;
      lastTrackArtist = info.artist;
      trackStartTime = now;
    }

    bool hasAlbum = (info.album.length() > 0 && info.album != info.title);

    // Dynamic vertical layout:
    // With album: Title at 34, Artist at 52, Album at 69
    // Without album: Title at 40, Artist at 60
    int16_t y = hasAlbum ? 34 : 40;

    // Title (1 line, white, 10s infinite forward marquee)
    drawScrollingText(tx, y, info.title, maxW, COLOR_TEXT, now);
    y += hasAlbum ? 18 : 20;

    // Artist (1 line, white, 10s infinite forward marquee)
    drawScrollingText(tx, y, info.artist, maxW, COLOR_TEXT, now);
    y += 17;

    // Album (1 line, muted gray, 10s infinite forward marquee)
    if (hasAlbum) {
      drawScrollingText(tx, y, info.album, maxW, COLOR_MUTED, now);
    }
  }

  // ---------------------------------------------------------------------
  // 3. Bottom Bar: Timestamps & Progress Bar (Optical Centerline y=119.5)
  // ---------------------------------------------------------------------
  uint32_t clampedElapsed =
      (currentElapsed > info.duration) ? info.duration : currentElapsed;
  String elapsedStr = formatTime(clampedElapsed);
  String totalStr = (info.duration > 0) ? formatTime(info.duration) : "--:--";

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT);

  // Timestamps text sits at y = 116 (glyphs from y=116..123, vertical center = 119.5)
  const int16_t textY = 116;
  const int16_t barY = 119;  // Progress bar sits right on the numbers' vertical midline
  const int16_t barH = 2;    // Sleek 2px progress bar

  // Left elapsed timestamp
  const int16_t MARGIN_BOTTOM_X = 14;
  canvas.setCursor(MARGIN_BOTTOM_X, textY);
  canvas.print(elapsedStr);

  // Right total timestamp
  int16_t durX = (SCREEN_WIDTH - MARGIN_BOTTOM_X) - (totalStr.length() * 6);
  canvas.setCursor(durX, textY);
  canvas.print(totalStr);

  // Center progress bar (evenly spaced between left and right timestamps)
  const int16_t PAD = 7;
  int16_t barX = MARGIN_BOTTOM_X + (elapsedStr.length() * 6) + PAD;
  int16_t barW = (durX - PAD) - barX;

  if (barW > 20) {
    // Subtle dark gray track background (#333333 / 0x3186)
    canvas.fillRect(barX, barY, barW, barH, COLOR_BAR_BG);

    // Active crisp white progress fill
    int fillW = (info.duration > 0)
                    ? (int)(((uint64_t)clampedElapsed * barW) / info.duration)
                    : 0;
    if (fillW > barW) fillW = barW;
    if (fillW > 0) {
      canvas.fillRect(barX, barY, fillW, barH, COLOR_TEXT);
    }

    // Modern playhead scrubber knob (centered on bar midline y=119)
    canvas.fillCircle(barX + fillW, barY, 2, COLOR_TEXT);
  }

  // 4. Push complete frame to physical/virtual display in a single burst
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void DisplayUI::renderStatus(const String &line1, const String &line2) {
  canvas.fillScreen(COLOR_BG);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);

  canvas.setCursor(16, 50);
  canvas.print(line1);

  if (line2.length() > 0) {
    canvas.setCursor(16, 70);
    canvas.print(line2);
  }

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void DisplayUI::renderSetupScreen(const String &apName, const String &apIP) {
  canvas.fillScreen(COLOR_BG);

  // Top header bar (subtle dark gray strip)
  canvas.fillRect(0, 0, SCREEN_WIDTH, 18, 0x18E3);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setTextSize(1);
  canvas.setCursor(12, 5);
  canvas.print("CARDPUTER SETUP MODE");

  // Step 1: Wi-Fi AP
  canvas.setTextColor(0xAD55); // light muted gray
  canvas.setCursor(12, 26);
  canvas.print("1. Connect Wi-Fi to AP:");

  canvas.fillRect(12, 38, SCREEN_WIDTH - 24, 18, 0x10A2);
  canvas.drawRect(12, 38, SCREEN_WIDTH - 24, 18, 0x3186);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(20, 43);
  canvas.print(apName);

  // Step 2: Browser URL
  canvas.setTextColor(0xAD55);
  canvas.setCursor(12, 64);
  canvas.print("2. Open in browser:");

  canvas.fillRect(12, 76, SCREEN_WIDTH - 24, 18, 0x10A2);
  canvas.drawRect(12, 76, SCREEN_WIDTH - 24, 18, 0x3186);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(20, 81);
  canvas.print("http://" + apIP);

  // Bottom hint
  canvas.setTextColor(0x632C); // subtle gray
  canvas.setCursor(12, 108);
  canvas.print("Or enter via USB Serial CLI");

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}