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
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135
#define ARTWORK_SIZE 86

// Strict Minimalist Color Palette (Pure pitch-black & crisp white)
#define COLOR_BG 0x0000     // Pure Pitch-Black (#000000)
#define COLOR_TEXT 0xFFFF   // Crisp White (#FFFFFF)
#define COLOR_MUTED 0x8410  // Subtle Muted Gray (#808080)
#define COLOR_BAR_BG 0x3186 // Subtle dark gray track (#333333)

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