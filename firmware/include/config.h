#pragma once
#include <stdint.h>

// Target selection (can also be defined via platformio.ini build_flags)
#if !defined(TARGET_WOKWI_SIMULATOR) && !defined(TARGET_M5_CARDPUTER_ADV) &&   \
    !defined(TARGET_M5_CARDPUTER) && !defined(TARGET_CUSTOM_ESP32S3)
#define TARGET_WOKWI_SIMULATOR
#endif

#if defined(TARGET_M5_CARDPUTER_ADV)
// =====================================================================
// M5Stack Cardputer-Adv (SKU: K132-Adv, Stamp-S3A core) Pinout
// =====================================================================
#define TFT_CS 37
#define TFT_DC 34   // RS (Command/Data) is G34 on Cardputer-Adv
#define TFT_RST 33  // RST is G33
#define TFT_MOSI 35 // DAT (SPI MOSI) is G35 on Cardputer-Adv
#define TFT_SCLK 36 // SCK (SPI Clock) is G36 on Cardputer-Adv
#define TFT_BL 38   // DISP_BL & RGB LED PWR_EN switch (Set HIGH)
#define BAT_ADC_PIN 10 // Battery voltage sensing ADC (ratio 2.0)
#define BOARD_NAME "Cardputer-Adv"
#elif defined(TARGET_M5_CARDPUTER)
// =====================================================================
// Physical M5Stack Cardputer v1.0 / v1.1 Pinout
// =====================================================================
#define TFT_CS 37
#define TFT_DC 4
#define TFT_RST 33
#define TFT_MOSI 6
#define TFT_SCLK 8
#define TFT_BL 38 // Backlight control pin
#define BAT_ADC_PIN 10 // Battery voltage sensing ADC (ratio 2.0)
#define BOARD_NAME "Cardputer v1.x"
#elif defined(TARGET_CUSTOM_ESP32S3)
// Custom board pins
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_BL -1
#define BAT_ADC_PIN -1
#define BOARD_NAME "Custom ESP32-S3"
#else // TARGET_WOKWI_SIMULATOR
// =====================================================================
// Wokwi simulation pinout (ESP32-S3 hardware SPI default pins)
// =====================================================================
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_BL -1 // No backlight pin needed in Wokwi
#define BAT_ADC_PIN -1
#define BOARD_NAME "Wokwi Sim"
#endif

// =====================================================================
// DISPLAY & CANVAS CONFIGURATION
// =====================================================================
#define SCREEN_WIDTH        240
#define SCREEN_HEIGHT       135
#define HEADER_HEIGHT       16
#define FOOTER_HEIGHT       17
#define ARTWORK_SIZE        72

// Modular Layout Metrics (Uniform 8px padding system)
#define PADDING_LEFT        8
#define PADDING_RIGHT       8
#define PADDING_TOP         8  // 8px padding below header divider line (y=16) -> content starts at y=24
#define CONTENT_START_Y     (HEADER_HEIGHT + PADDING_TOP) // 24



// =====================================================================
// 60-30-10 COLOR PALETTE (RGB565)
// =====================================================================
// 60% Dominant (Canvas, Structural Containers, Body Text)
#define COLOR_BG            0x0000 // Deep Canvas Background (#050505 / ST7789 pitch black)
#define COLOR_HEADER_BG     0x0861 // Header dark strip (#0d0d0d)
#define COLOR_FOOTER_BG     0x0861 // Footer dark strip (#0d0d0d)
#define COLOR_DIVIDER       0x18E3 // Subtle divider border (#1a1a1a)
#define COLOR_CARD_BG       0x0861 // Card surface (#0d0d0d)
#define COLOR_PILL_BG       0x10A2 // Button pill background (#151515)
#define COLOR_PILL_BORDER   0x3186 // Button pill border (#333333)
#define COLOR_TEXT          0xE73C // Main text (#e6e6e6)
#define COLOR_MUTED         0x8410 // Muted labels & timestamps (#808080)
#define COLOR_BAR_BG        0x2124 // Track background (#252525)

// 30% Structural Hierarchy & Brand (Primary: #afbdd9)
#define COLOR_PRIMARY       0xADFB // App Name / Titles / Active Playhead (#afbdd9)
#define COLOR_PRIMARY_LIGHT 0xDF1D // Primary light shade (#dce2ef)
#define COLOR_PRIMARY_DIM   0x320D // Primary dark border/bg (#314368)

// 10% High-Impact Accents (Secondary: #f0a133, Accent: #df9a9e)
#define COLOR_SECONDARY     0xF506 // UI Hints & Keyboard Shortcuts (#f0a133)
#define COLOR_SECONDARY_DIM 0x59C0 // Secondary dark shade (#5f3a07)
#define COLOR_ACCENT        0xDCD3 // Pause overlay & alerts (#df9a9e)
#define COLOR_ACCENT_BG     0x2061 // Accent container background (#270c0e)
#define COLOR_ACCENT_LIGHT  0xE596 // Accent light shade (#e7b1b4)


// =====================================================================
// WI-FI & COMPANION BRIDGE NETWORKING
// =====================================================================
#define DEFAULT_PORT 58329
#define AP_SSID "cardputer-nowplaying-setup"
#define BTN_SETUP_PIN 0 // GPIO 0 (G0 / Boot button on Cardputer)

// Fallback defaults for Wokwi simulation:
#define WOKWI_DEFAULT_SSID "Wokwi-GUEST"
#define WOKWI_DEFAULT_PASS ""
#define WOKWI_DEFAULT_HOST "host.wokwi.internal"

#define METADATA_PATH "/api/now-playing"
#define ARTWORK_PATH "/artwork.raw"

// Polling interval in milliseconds
#define POLL_INTERVAL_MS 3000