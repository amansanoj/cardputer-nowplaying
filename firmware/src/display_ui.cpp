#if __has_include("display_ui.h")
#include "display_ui.h"
#else
#include "../include/display_ui.h"
#endif

DisplayUI::DisplayUI()
    : tft(TFT_CS, TFT_DC, TFT_RST), canvas(SCREEN_WIDTH, SCREEN_HEIGHT),
      hasArtwork(false) {
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

void DisplayUI::drawTruncatedText(int16_t x, int16_t y, const String &text,
                                  int maxChars, uint8_t size) {
  canvas.setCursor(x, y);
  canvas.setTextSize(size);
  canvas.setTextColor(COLOR_TEXT);

  if ((int)text.length() <= maxChars) {
    canvas.print(text);
  } else {
    String truncated = text.substring(0, maxChars - 3) + "...";
    canvas.print(truncated);
  }
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

  // ---------------------------------------------------------------------
  // 2. Track Metadata (Right side of art, vertically centered to album art)
  // ---------------------------------------------------------------------
  const int16_t GAP = 12;
  const int16_t tx = artX + ARTWORK_SIZE + GAP;       // 15 + 86 + 12 = 113
  const int16_t textRightX = SCREEN_WIDTH - 14;       // 226 (14px right margin)
  const int maxChars = (textRightX - tx) / 6;         // 18-19 characters

  const int16_t LINE_H = 12;
  const int16_t GAP_META = 6;

  if (!info.isRunning || info.state == "stopped") {
    // Idle state — vertically centered to artwork
    int16_t idleH = LINE_H * 3 + 6;
    int16_t idleY = artY + (ARTWORK_SIZE - idleH) / 2;

    canvas.setTextSize(1);
    canvas.setTextColor(COLOR_TEXT);

    canvas.setCursor(tx, idleY);
    canvas.print("Apple Music");

    canvas.setCursor(tx, idleY + LINE_H + 3);
    canvas.print("Ready / Idle");

    canvas.setCursor(tx, idleY + (LINE_H + 3) * 2);
    canvas.print("No track playing");
  } else {
    // 2 lines reserved for Title, 1 line for Artist, 1 line for Album
    bool titleWraps = ((int)info.title.length() > maxChars);
    int16_t titleLines = titleWraps ? 2 : 1;

    // Total height of the text block
    int16_t blockH = (titleLines * LINE_H) + GAP_META + LINE_H + LINE_H;
    if (info.state == "paused") {
      blockH += 3 + LINE_H;
    }

    // Vertically center text block to the album art
    int16_t y = artY + (ARTWORK_SIZE - blockH) / 2;

    // Title (Supports up to 2 lines)
    if (!titleWraps) {
      drawTruncatedText(tx, y, info.title, maxChars);
      y += LINE_H;
    } else {
      int breakIdx = info.title.lastIndexOf(' ', maxChars);
      if (breakIdx <= 4) breakIdx = maxChars;
      String line1 = info.title.substring(0, breakIdx);
      String line2 = info.title.substring(breakIdx);
      line2.trim();
      drawTruncatedText(tx, y, line1, maxChars);
      y += LINE_H;
      drawTruncatedText(tx, y, line2, maxChars);
      y += LINE_H;
    }

    y += GAP_META;

    // Artist (1 line)
    drawTruncatedText(tx, y, info.artist, maxChars);
    y += LINE_H;

    // Album (1 line)
    drawTruncatedText(tx, y, info.album, maxChars);
    y += LINE_H;

    // Optional status tag if paused
    if (info.state == "paused") {
      y += 3;
      drawTruncatedText(tx, y, "[ PAUSED ]", maxChars);
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