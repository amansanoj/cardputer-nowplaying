#if __has_include("display_ui.h")
#include "display_ui.h"
#else
#include "../include/display_ui.h"
#endif

DisplayUI::DisplayUI()
    : tft(TFT_CS, TFT_DC, TFT_RST), canvas(SCREEN_WIDTH, SCREEN_HEIGHT),
      hasArtwork(false), lastTrackKey(""), sharedPauseStartTime(0),
      sharedScrollStartTime(0), isSharedScrolling(false), lastKnownClock("") {
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

int16_t DisplayUI::getLoopWidth(const String &text, int16_t maxW) {
  int16_t textW = text.length() * 6;
  if (textW <= maxW) return 0; // Text fits inside column, no scrolling needed
  const int16_t gapSpaces = 5;
  return (text.length() + gapSpaces) * 6;
}

void DisplayUI::splitTitle(const String &title, int16_t maxW, String &line1, String &line2) {
  int maxChars = maxW / 6;
  if (title.length() <= (size_t)maxChars) {
    line1 = title;
    line2 = "";
    return;
  }

  // Find natural word break point near maxChars (spaces, hyphens, slashes, parens)
  int splitIdx = -1;
  int searchMin = maxChars > 10 ? maxChars - 8 : 4;
  for (int i = maxChars; i >= searchMin; i--) {
    char c = title[i];
    if (c == ' ' || c == '-' || c == '/' || c == '(' || c == ':') {
      splitIdx = i;
      break;
    }
  }

  if (splitIdx == -1) {
    splitIdx = maxChars;
  }

  line1 = title.substring(0, splitIdx);
  line1.trim();
  line2 = title.substring(splitIdx);
  line2.trim();
}

void DisplayUI::drawScrollingText(int16_t x, int16_t y, const String &text,
                                  int16_t maxW, uint16_t color, int16_t offset) {
  if (text.length() == 0) return;

  int16_t textW = text.length() * 6;
  int16_t clipRightX = x + maxW;

  // Case 1: Fits comfortably in available space or parked at start
  if (textW <= maxW || offset == 0) {
    int maxVisible = maxW / 6;
    int len = text.length();
    canvas.setTextSize(1);
    canvas.setTextColor(color);
    for (int i = 0; i < len && i <= maxVisible; i++) {
      int16_t charX = x + (i * 6);
      if (charX >= clipRightX) break;
      canvas.drawChar(charX, y, text[i], color, COLOR_BG, 1);
    }
    if (x > 4) canvas.fillRect(x - 4, y, 4, 12, COLOR_BG);
    if (clipRightX < SCREEN_WIDTH) canvas.fillRect(clipRightX, y, SCREEN_WIDTH - clipRightX, 12, COLOR_BG);
    return;
  }

  // Case 2: Actively scrolling forward
  const int16_t gapSpaces = 5;
  int16_t textLen = text.length();
  int16_t loopChars = textLen + gapSpaces;
  int16_t loopW = loopChars * 6;

  int16_t effectiveOffset = offset % loopW;

  canvas.setTextSize(1);
  canvas.setTextColor(color);

  int startV = effectiveOffset / 6;
  if (startV > 0) startV--;

  for (int v = startV;; v++) {
    int16_t charX = x - effectiveOffset + (v * 6);
    if (charX >= clipRightX) break;
    if (charX + 6 <= x) continue;

    int idx = v % loopChars;
    char c = (idx < textLen) ? text[idx] : ' ';
    if (c != ' ') {
      canvas.drawChar(charX, y, c, color, COLOR_BG, 1);
    }
  }

  if (x > 4) canvas.fillRect(x - 4, y, 4, 12, COLOR_BG);
  if (clipRightX < SCREEN_WIDTH) canvas.fillRect(clipRightX, y, SCREEN_WIDTH - clipRightX, 12, COLOR_BG);
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

void DisplayUI::drawControlIcon(int16_t x, int16_t y, ControlIcon icon, uint16_t color) {
  switch (icon) {
    case ICON_PREV: // |◀
      canvas.fillRect(x, y, 2, 7, color);
      canvas.fillTriangle(x + 6, y, x + 6, y + 6, x + 2, y + 3, color);
      break;
    case ICON_REWIND: // ◀◀
      canvas.fillTriangle(x + 3, y, x + 3, y + 6, x, y + 3, color);
      canvas.fillTriangle(x + 7, y, x + 7, y + 6, x + 4, y + 3, color);
      break;
    case ICON_PLAY: // ▶
      canvas.fillTriangle(x, y, x, y + 6, x + 6, y + 3, color);
      break;
    case ICON_PAUSE: // ❚❚
      canvas.fillRect(x, y, 2, 7, color);
      canvas.fillRect(x + 4, y, 2, 7, color);
      break;
    case ICON_PLAYPAUSE: // ▶||
      canvas.fillTriangle(x, y, x, y + 6, x + 4, y + 3, color);
      canvas.fillRect(x + 6, y, 1, 7, color);
      break;
    case ICON_FORWARD: // ▶▶
      canvas.fillTriangle(x, y, x, y + 6, x + 3, y + 3, color);
      canvas.fillTriangle(x + 4, y, x + 4, y + 6, x + 7, y + 3, color);
      break;
    case ICON_NEXT: // ▶|
      canvas.fillTriangle(x, y, x, y + 6, x + 4, y + 3, color);
      canvas.fillRect(x + 5, y, 2, 7, color);
      break;
    case ICON_SETUP: // gear icon
      canvas.drawRect(x + 1, y + 1, 5, 5, color);
      canvas.fillRect(x + 2, y + 2, 3, 3, color);
      break;
    default:
      break;
  }
}

void DisplayUI::drawHeader(const String &clockTime, const String &screenTitle) {
  // 1. Header background bar
  canvas.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, COLOR_HEADER_BG);

  // 2. Resolve Clock Time:
  // - Priority 1: Current incoming clockTime if valid and non-empty
  // - Priority 2: ESP32 local hardware clock (from NTP / bridge sync)
  // - Priority 3: Last known valid clock string (never flashes --:--)
  // - Fallback: "--:--" only on very first boot before any sync
  String displayClock = "";
  if (clockTime.length() > 0 && clockTime != "--:--") {
    lastKnownClock = clockTime;
    displayClock = clockTime;
  } else {
    time_t nowSec = time(nullptr);
    struct tm tmInfo;
    if (gmtime_r(&nowSec, &tmInfo) && tmInfo.tm_year >= (2024 - 1900)) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
      displayClock = String(buf);
      lastKnownClock = displayClock;
    } else if (lastKnownClock.length() > 0) {
      displayClock = lastKnownClock;
    } else {
      displayClock = "--:--";
    }
  }

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(6, 4);
  canvas.print(displayClock);

  // 3. Screen Title in center: "Now Playing" in COLOR_PRIMARY (#afbdd9)
  if (screenTitle.length() > 0) {
    int16_t titleW = screenTitle.length() * 6;
    int16_t titleX = (SCREEN_WIDTH - titleW) / 2;
    canvas.setTextColor(COLOR_PRIMARY);
    canvas.setCursor(titleX, 4);
    canvas.print(screenTitle);
  }

  // 4. Status indicators on right: Wi-Fi RSSI + Battery info
  uint8_t batPct = 100;
  bool isCharging = false;
  getBatteryInfo(batPct, isCharging);

  // Battery Capsule: 15px wide x 8px high
  const int16_t rightX = SCREEN_WIDTH - 6;
  const int16_t batW = 15;
  const int16_t batH = 8;
  const int16_t bx = rightX - batW;
  const int16_t by = 4;

  canvas.drawRoundRect(bx, by, batW - 2, batH, 2, COLOR_MUTED);
  canvas.fillRect(bx + batW - 2, by + 2, 2, 4, COLOR_MUTED);

  if (isCharging) {
    // Charging lightning bolt in vibrant secondary accent (#f0a133)
    canvas.drawLine(bx + 7, by + 1, bx + 5, by + 4, COLOR_SECONDARY);
    canvas.drawLine(bx + 5, by + 4, bx + 8, by + 4, COLOR_SECONDARY);
    canvas.drawLine(bx + 8, by + 4, bx + 6, by + 7, COLOR_SECONDARY);
  } else {
    int16_t fillW = (batPct * 9) / 100;
    if (fillW > 0) {
      uint16_t fillColor = (batPct <= 15) ? COLOR_ACCENT : COLOR_PRIMARY;
      canvas.fillRect(bx + 2, by + 2, fillW, 4, fillColor);
    }
  }

  // Battery Percentage Text (e.g. "95%")
  String pctStr = String(batPct) + "%";
  int16_t pctX = bx - 3 - (pctStr.length() * 6);
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_TEXT);
  canvas.setCursor(pctX, by);
  canvas.print(pctStr);

  // Wi-Fi Signal indicator (3 bars, warm secondary accent on active connection)
  int8_t rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
  int16_t wx = pctX - 6 - 8;
  uint16_t b1 = (rssi > -90) ? COLOR_SECONDARY : COLOR_BAR_BG;
  uint16_t b2 = (rssi > -75) ? COLOR_SECONDARY : COLOR_BAR_BG;
  uint16_t b3 = (rssi > -65) ? COLOR_SECONDARY : COLOR_BAR_BG;

  canvas.fillRect(wx, by + 5, 2, 3, b1);
  canvas.fillRect(wx + 3, by + 3, 2, 5, b2);
  canvas.fillRect(wx + 6, by + 1, 2, 7, b3);

  // Header bottom divider line
  canvas.drawFastHLine(0, HEADER_HEIGHT, SCREEN_WIDTH, COLOR_DIVIDER);
}

void DisplayUI::drawFooter(const FooterControl *controls, size_t count) {
  const int16_t fy = SCREEN_HEIGHT - FOOTER_HEIGHT; // 135 - 17 = 118

  // Footer top divider line & background bar
  canvas.drawFastHLine(0, fy - 1, SCREEN_WIDTH, COLOR_DIVIDER);
  canvas.fillRect(0, fy, SCREEN_WIDTH, FOOTER_HEIGHT, COLOR_FOOTER_BG);

  if (count == 0) return;

  int16_t slotW = SCREEN_WIDTH / count;
  for (size_t i = 0; i < count; i++) {
    const FooterControl &ctrl = controls[i];
    int16_t slotX = i * slotW;
    int16_t pillW = slotW - 6;
    int16_t pillX = slotX + 3;
    int16_t pillY = fy + 2;
    int16_t pillH = FOOTER_HEIGHT - 4; // 13px

    // 60-30-10 styling:
    // Normal: pill in dark background, key hints in vibrant secondary (#f0a133), icons in text (#e6e6e6)
    // Active/Pressed: pill in primary dark (#314368) with accent highlight border (#df9a9e)
    uint16_t bg = ctrl.active ? COLOR_PRIMARY_DIM : COLOR_PILL_BG;
    uint16_t border = ctrl.active ? COLOR_ACCENT : COLOR_PILL_BORDER;
    uint16_t textCol = ctrl.active ? COLOR_ACCENT_LIGHT : COLOR_SECONDARY;
    uint16_t iconCol = ctrl.active ? COLOR_TEXT : COLOR_TEXT;

    canvas.fillRoundRect(pillX, pillY, pillW, pillH, 3, bg);
    canvas.drawRoundRect(pillX, pillY, pillW, pillH, 3, border);

    // Compute layout inside pill
    // Key hint in secondary (#f0a133), label in muted/text, icon in text
    int16_t keyLen = ctrl.keyHint.length();
    int16_t labelLen = ctrl.label.length();
    int16_t iconW = (ctrl.icon != ICON_NONE) ? 7 : 0;
    
    // Check if label fits alongside key hint (e.g. "[R] Reboot" on wide pills)
    bool showLabel = (labelLen > 0) && ((iconW + (keyLen + labelLen + 3) * 6) <= (pillW - 6));
    
    int16_t textW = keyLen * 6;
    if (showLabel) {
      textW += (labelLen + 1) * 6; // key + space + label
    }
    int16_t gap = (iconW > 0 && textW > 0) ? 3 : 0;
    int16_t totalW = iconW + gap + textW;

    int16_t startX = pillX + (pillW - totalW) / 2;
    int16_t contentY = pillY + (pillH - 7) / 2;

    if (ctrl.icon != ICON_NONE) {
      drawControlIcon(startX, contentY, ctrl.icon, iconCol);
      startX += iconW + gap;
    }

    if (keyLen > 0) {
      canvas.setTextSize(1);
      canvas.setTextColor(textCol);
      canvas.setCursor(startX, contentY);
      canvas.print(ctrl.keyHint);
      startX += keyLen * 6;
    }

    if (showLabel) {
      startX += 6; // space separator
      canvas.setTextSize(1);
      canvas.setTextColor(ctrl.active ? COLOR_TEXT : COLOR_MUTED);
      canvas.setCursor(startX, contentY);
      canvas.print(ctrl.label);
    }
  }
}

void DisplayUI::drawPlaybackFooter(const String &state, char activeKey) {
  bool isPlaying = (state == "playing");
  FooterControl controls[5];

  controls[0] = { "Prev", "P", ICON_PREV, (activeKey == 'p' || activeKey == 'P') };
  controls[1] = { "-10s", "<", ICON_REWIND, (activeKey == ',' || activeKey == '<' || activeKey == '[') };
  controls[2] = { isPlaying ? "Pause" : "Play", "Spc", isPlaying ? ICON_PAUSE : ICON_PLAY, (activeKey == ' ') };
  controls[3] = { "+10s", ">", ICON_FORWARD, (activeKey == '.' || activeKey == '>' || activeKey == ']') };
  controls[4] = { "Next", "N", ICON_NEXT, (activeKey == 'n' || activeKey == 'N') };

  drawFooter(controls, 5);
}

void DisplayUI::drawArtwork(int16_t x, int16_t y, int16_t size) {
  canvas.drawRGBBitmap(x, y, artworkBuffer, size, size);

  // Mask 4 outer corner pixels with background for a clean radius 4 curve
  const int16_t r = 4;
  for (int16_t dy = 0; dy < r; dy++) {
    for (int16_t dx = 0; dx < r; dx++) {
      int16_t distSq = (r - 1 - dx) * (r - 1 - dx) + (r - 1 - dy) * (r - 1 - dy);
      if (distSq > (r - 1) * (r - 1)) {
        canvas.drawPixel(x + dx, y + dy, COLOR_BG);
        canvas.drawPixel(x + size - 1 - dx, y + dy, COLOR_BG);
        canvas.drawPixel(x + dx, y + size - 1 - dy, COLOR_BG);
        canvas.drawPixel(x + size - 1 - dx, y + size - 1 - dy, COLOR_BG);
      }
    }
  }

  // Draw subtle rounded outline matching placeholder card & pause overlay
  canvas.drawRoundRect(x, y, size, size, 4, COLOR_DIVIDER);
}

void DisplayUI::drawPlaceholderArt(int16_t x, int16_t y, int16_t size) {
  canvas.fillRoundRect(x, y, size, size, 4, COLOR_CARD_BG);
  canvas.drawRoundRect(x, y, size, size, 4, COLOR_DIVIDER);

  int16_t cx = x + size / 2;
  int16_t cy = y + size / 2;

  // Stems
  canvas.fillRect(cx - 6, cy - 10, 2, 16, COLOR_PRIMARY);
  canvas.fillRect(cx + 4, cy - 13, 2, 16, COLOR_TEXT);
  // Top beam
  canvas.fillRect(cx - 6, cy - 13, 12, 3, COLOR_PRIMARY);
  // Note heads
  canvas.fillCircle(cx - 7, cy + 5, 3, COLOR_PRIMARY);
  canvas.fillCircle(cx + 3, cy + 2, 3, COLOR_TEXT);
}

void DisplayUI::render(const TrackInfo &info, uint32_t currentElapsed, char activeKey) {
  unsigned long now = millis();

  // Clear off-screen buffer
  canvas.fillScreen(COLOR_BG);

  // 1. Standard Header (Time, App Name: "Now Playing", Wi-Fi, Battery)
  drawHeader(info.clock, "Now Playing");

  // 2. Middle Area (y = 24..116)
  // ---------------------------------------------------------------------
  // 2A. Artwork on Left (72x72 at x = 8, y = 24)
  // Top padding: 8px from header divider (y=16) to art/text (y=24)
  // Bottom padding: 7px to timestamps (y=103), 7-8px from timestamps to footer
  // Left padding: 8px, Right padding: 8px
  // ---------------------------------------------------------------------
  const int16_t artX = PADDING_LEFT;      // 8
  const int16_t artY = CONTENT_START_Y;  // 24

  if (hasArtwork && info.isRunning && info.state != "stopped") {
    drawArtwork(artX, artY, ARTWORK_SIZE);
  } else {
    drawPlaceholderArt(artX, artY, ARTWORK_SIZE);
  }

  // If paused, overlay minimalist Dynamic Island pause badge in ACCENT (#df9a9e)
  if (info.isRunning && info.state == "paused") {
    const int16_t badgeW = 16;
    const int16_t badgeH = 16;
    const int16_t badgeX = artX + ARTWORK_SIZE - badgeW - 3;
    const int16_t badgeY = artY + ARTWORK_SIZE - badgeH - 3;

    canvas.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, COLOR_ACCENT_BG);
    canvas.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, COLOR_ACCENT);
    canvas.fillRect(badgeX + 4, badgeY + 4, 2, 8, COLOR_ACCENT);
    canvas.fillRect(badgeX + 10, badgeY + 4, 2, 8, COLOR_ACCENT);
  }

  // ---------------------------------------------------------------------
  // 2B. Track Metadata (Right side of artwork)
  // Reserved 2 lines for song title always, 1 line artist, 1 line album
  // ---------------------------------------------------------------------
  const int16_t GAP = 10;
  const int16_t tx = artX + ARTWORK_SIZE + GAP;       // 8 + 72 + 10 = 90
  const int16_t textRightX = SCREEN_WIDTH - PADDING_RIGHT; // 232
  const int16_t maxW = textRightX - tx;               // 142 pixels wide

  // Top padding of text exactly matches top padding of image (y = 24)
  const int16_t line1Y = CONTENT_START_Y;       // 24: Title Line 1 (8px padding from header)
  const int16_t line2Y = line1Y + 14;          // 38: Title Line 2 (Strictly reserved)
  const int16_t line3Y = line2Y + 16;          // 54: Artist (1 line)
  const int16_t line4Y = line3Y + 16;          // 70: Album (1 line)

  if (!info.isRunning || info.state == "stopped") {
    // Idle state: just say "Not Playing" in muted tone
    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_MUTED);
    canvas.setCursor(tx, line1Y);
    canvas.print("Not Playing");
  } else {
    // Split song title into up to 2 lines
    String titleLine1 = "";
    String titleLine2 = "";
    splitTitle(info.title, maxW, titleLine1, titleLine2);

    // Reset marquee timer when track changes
    String currentTrackKey = info.title + "\t" + info.artist + "\t" + info.album;
    if (currentTrackKey != lastTrackKey) {
      lastTrackKey = currentTrackKey;
      sharedPauseStartTime = now;
      sharedScrollStartTime = 0;
      isSharedScrolling = false;
    }

    bool hasAlbum = (info.album.length() > 0 && info.album != info.title);

    // Compute loop widths on shared clock for all text lines
    int16_t t1LoopW = getLoopWidth(titleLine1, maxW);
    int16_t t2LoopW = getLoopWidth(titleLine2, maxW);
    int16_t artistLoopW = getLoopWidth(info.artist, maxW);
    int16_t albumLoopW = hasAlbum ? getLoopWidth(info.album, maxW) : 0;
    int16_t maxLoopW = max(max(t1LoopW, t2LoopW), max(artistLoopW, albumLoopW));

    const unsigned long PAUSE_DURATION = 10000; // Shared 10-second pause
    const unsigned long SCROLL_SPEED = 40;      // 40ms per pixel
    unsigned long maxScrollDuration = (unsigned long)maxLoopW * SCROLL_SPEED;

    if (maxLoopW > 0) {
      if (!isSharedScrolling) {
        if (sharedPauseStartTime == 0) sharedPauseStartTime = now;
        if (now - sharedPauseStartTime >= PAUSE_DURATION) {
          isSharedScrolling = true;
          sharedScrollStartTime = now;
        }
      } else {
        unsigned long scrollElapsed = now - sharedScrollStartTime;
        if (scrollElapsed >= maxScrollDuration) {
          isSharedScrolling = false;
          sharedPauseStartTime = now;
        }
      }
    } else {
      isSharedScrolling = false;
    }

    auto getLineOffset = [&](int16_t loopW) -> int16_t {
      if (loopW == 0 || !isSharedScrolling) return 0;
      unsigned long scrollElapsed = now - sharedScrollStartTime;
      int16_t offset = (int16_t)(scrollElapsed / SCROLL_SPEED);
      if (offset >= loopW) return 0;
      return offset;
    };

    int16_t t1Offset = getLineOffset(t1LoopW);
    int16_t t2Offset = getLineOffset(t2LoopW);
    int16_t artistOffset = getLineOffset(artistLoopW);
    int16_t albumOffset = getLineOffset(albumLoopW);

    // Line 1: Title Line 1 (white #e6e6e6)
    drawScrollingText(tx, line1Y, titleLine1, maxW, COLOR_TEXT, t1Offset);

    // Line 2: Title Line 2 (white #e6e6e6, reserved - blank if not used)
    if (titleLine2.length() > 0) {
      drawScrollingText(tx, line2Y, titleLine2, maxW, COLOR_TEXT, t2Offset);
    }

    // Line 3: Artist (muted gray #808080)
    drawScrollingText(tx, line3Y, info.artist, maxW, COLOR_MUTED, artistOffset);

    // Line 4: Album (muted gray #808080)
    if (hasAlbum) {
      drawScrollingText(tx, line4Y, info.album, maxW, COLOR_MUTED, albumOffset);
    }
  }

  // ---------------------------------------------------------------------
  // 2C. Timestamps & Progress Bar (y = 101..107)
  // ---------------------------------------------------------------------
  uint32_t clampedElapsed =
      (currentElapsed > info.duration) ? info.duration : currentElapsed;
  String elapsedStr = formatTime(clampedElapsed);
  String totalStr = (info.duration > 0) ? formatTime(info.duration) : "--:--";

  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_MUTED);

  const int16_t textY = 103;
  const int16_t barY = 106;
  const int16_t barH = 2;

  // Left elapsed timestamp
  canvas.setCursor(artX, textY);
  canvas.print(elapsedStr);

  // Right total timestamp
  int16_t durX = textRightX - (totalStr.length() * 6);
  canvas.setCursor(durX, textY);
  canvas.print(totalStr);

  // Center progress bar in Primary (#afbdd9)
  const int16_t PAD = 5;
  int16_t barX = artX + (elapsedStr.length() * 6) + PAD;
  int16_t barW = (durX - PAD) - barX;

  if (barW > 20) {
    canvas.fillRect(barX, barY, barW, barH, COLOR_BAR_BG);
    int fillW = (info.duration > 0)
                    ? (int)(((uint64_t)clampedElapsed * barW) / info.duration)
                    : 0;
    if (fillW > barW) fillW = barW;
    if (fillW > 0) {
      canvas.fillRect(barX, barY, fillW, barH, COLOR_PRIMARY);
    }
    // Modern playhead scrubber knob in Primary
    canvas.fillCircle(barX + fillW, barY + 1, 2, COLOR_PRIMARY);
  }

  // ---------------------------------------------------------------------
  // 3. Standard Footer (Media Controls & Shortcuts)
  // ---------------------------------------------------------------------
  drawPlaybackFooter(info.state, activeKey);

  // 4. Push complete frame to physical/virtual display in a single burst
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void DisplayUI::renderStatus(const String &line1, const String &line2) {
  canvas.fillScreen(COLOR_BG);
  drawHeader("", "Now Playing");

  // Center container card
  int16_t cardX = 16;
  int16_t cardY = 32;
  int16_t cardW = SCREEN_WIDTH - 32;
  int16_t cardH = 64;

  canvas.fillRoundRect(cardX, cardY, cardW, cardH, 4, COLOR_CARD_BG);
  canvas.drawRoundRect(cardX, cardY, cardW, cardH, 4, COLOR_PRIMARY_DIM);

  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setTextSize(1);

  int16_t l1W = line1.length() * 6;
  int16_t l1X = (SCREEN_WIDTH - l1W) / 2;
  canvas.setCursor(l1X, cardY + 18);
  canvas.print(line1);

  if (line2.length() > 0) {
    canvas.setTextColor(COLOR_MUTED);
    int16_t l2W = line2.length() * 6;
    int16_t l2X = (SCREEN_WIDTH - l2W) / 2;
    canvas.setCursor(l2X, cardY + 36);
    canvas.print(line2);
  }

  FooterControl statusControls[1] = {
    { "Setup", "G0", ICON_SETUP, false }
  };
  drawFooter(statusControls, 1);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}

void DisplayUI::renderSetupScreen(const String &apName, const String &apIP) {
  canvas.fillScreen(COLOR_BG);
  drawHeader("", "Now Playing");

  // Step 1: Wi-Fi AP (UI hint in secondary #f0a133)
  canvas.setTextSize(1);
  canvas.setTextColor(COLOR_SECONDARY);
  canvas.setCursor(12, 24);
  canvas.print("1. ");
  canvas.setTextColor(COLOR_TEXT);
  canvas.print("Connect Wi-Fi to AP:");

  canvas.fillRoundRect(12, 36, SCREEN_WIDTH - 24, 18, 2, COLOR_PILL_BG);
  canvas.drawRoundRect(12, 36, SCREEN_WIDTH - 24, 18, 2, COLOR_PRIMARY_DIM);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(20, 41);
  canvas.print(apName);

  // Step 2: Browser URL (UI hint in secondary #f0a133)
  canvas.setTextColor(COLOR_SECONDARY);
  canvas.setCursor(12, 60);
  canvas.print("2. ");
  canvas.setTextColor(COLOR_TEXT);
  canvas.print("Open in browser:");

  canvas.fillRoundRect(12, 72, SCREEN_WIDTH - 24, 18, 2, COLOR_PILL_BG);
  canvas.drawRoundRect(12, 72, SCREEN_WIDTH - 24, 18, 2, COLOR_PRIMARY_DIM);
  canvas.setTextColor(COLOR_PRIMARY);
  canvas.setCursor(20, 77);
  canvas.print("http://" + apIP);

  // Bottom hint
  canvas.setTextColor(COLOR_MUTED);
  canvas.setCursor(12, 98);
  canvas.print("Or config via USB Serial CLI");

  FooterControl setupControls[2] = {
    { "Exit", "G0", ICON_NONE, false },
    { "Reboot", "R", ICON_NONE, false }
  };
  drawFooter(setupControls, 2);

  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), SCREEN_WIDTH, SCREEN_HEIGHT);
}