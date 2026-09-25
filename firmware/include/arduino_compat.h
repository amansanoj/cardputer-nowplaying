#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO) || __has_include(<Arduino.h>)
  #include <Arduino.h>
  #if __has_include(<Adafruit_GFX.h>)
    #include <Adafruit_GFX.h>
  #endif
  #if __has_include(<Adafruit_ST7789.h>)
    #include <Adafruit_ST7789.h>
  #endif
  #if __has_include(<SPI.h>)
    #include <SPI.h>
  #endif
  #if __has_include(<WiFi.h>)
    #include <WiFi.h>
  #endif
  #if __has_include(<HTTPClient.h>)
    #include <HTTPClient.h>
  #endif
  #if __has_include(<ArduinoJson.h>)
    #include <ArduinoJson.h>
  #endif
#elif defined(__cplusplus)
  // Fallback declarations for generic host IDE / Clangd language servers in C++
  #include <string>
  #include <cstring>
  #include <algorithm>

  class String : public std::string {
  public:
    using std::string::string;
    String(const char* s = "") : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(int v) : std::string(std::to_string(v)) {}
    String(unsigned int v) : std::string(std::to_string(v)) {}
    String substring(size_t from, size_t to = std::string::npos) const {
      if (to == std::string::npos) return std::string::substr(from);
      return std::string::substr(from, to - from);
    }
    int lastIndexOf(char ch, size_t fromIndex = std::string::npos) const {
      return (int)std::string::rfind(ch, fromIndex);
    }
    void trim() {}
  };

  class GFXcanvas16 {
  public:
    GFXcanvas16(uint16_t, uint16_t) {}
    void setTextWrap(bool) {}
    void fillScreen(uint16_t) {}
    void fillRect(int16_t, int16_t, int16_t, int16_t, uint16_t) {}
    void fillCircle(int16_t, int16_t, int16_t, uint16_t) {}
    void setCursor(int16_t, int16_t) {}
    void setTextSize(uint8_t) {}
    void setTextColor(uint16_t) {}
    void print(const String&) {}
    void print(const char*) {}
    void drawRGBBitmap(int16_t, int16_t, const uint16_t*, int16_t, int16_t) {}
    uint16_t* getBuffer() { return nullptr; }
  };

  class Adafruit_ST7789 {
  public:
    Adafruit_ST7789(int8_t, int8_t, int8_t) {}
    void init(uint16_t, uint16_t) {}
    void setRotation(uint8_t) {}
    void fillScreen(uint16_t) {}
    void drawRGBBitmap(int16_t, int16_t, const uint16_t*, int16_t, int16_t) {}
  };

  class IPAddress {
  public:
    String toString() const { return "192.168.1.100"; }
  };

  #define WIFI_STA 1
  #define WL_CONNECTED 3
  #define HTTP_CODE_OK 200
  #define OUTPUT 1
  #define HIGH 1
  #define LOW 0

  class WiFiClass {
  public:
    void mode(int) {}
    void begin(const char*, const char*) {}
    int status() { return WL_CONNECTED; }
    IPAddress localIP() { return IPAddress(); }
  };
  extern WiFiClass WiFi;

  class WiFiClient {
  public:
    size_t available() { return 0; }
    int readBytes(char*, size_t) { return 0; }
  };

  class HTTPClient {
  public:
    void begin(WiFiClient&, const String&) {}
    void setTimeout(uint16_t) {}
    int GET() { return HTTP_CODE_OK; }
    String getString() { return "{}"; }
    int getSize() { return 0; }
    WiFiClient* getStreamPtr() { return nullptr; }
    bool connected() { return false; }
    void end() {}
  };

  inline void pinMode(int, int) {}
  inline void digitalWrite(int, int) {}
  inline unsigned long millis() { return 0; }
  inline void delay(unsigned long) {}

  struct SerialClass {
    void begin(unsigned long) {}
    void println(const char* = "") {}
    void print(const char* = "") {}
    template<typename... Args>
    void printf(const char*, Args...) {}
  };
  extern SerialClass Serial;

  struct SPIClass {
    void begin(int, int, int, int) {}
  };
  extern SPIClass SPI;

  class JsonDocument {
  public:
    struct Element {
      String operator|(const char* def) const { return def; }
      uint32_t operator|(uint32_t def) const { return def; }
      int operator|(int def) const { return def; }
      bool operator|(bool def) const { return def; }
    };
    Element operator[](const char*) const { return Element(); }
  };

  struct DeserializationError {
    operator bool() const { return false; }
    const char* c_str() const { return "Ok"; }
  };

  inline DeserializationError deserializeJson(JsonDocument&, const String&) {
    return DeserializationError();
  }
#endif
