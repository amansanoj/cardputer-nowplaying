#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "display_ui.h"

enum TransportMode : uint8_t {
  TRANSPORT_BLE = 0,
  TRANSPORT_WIFI = 1
};

struct AppConfig {
  uint8_t transportMode = TRANSPORT_BLE;
  String wifiSsid = "";
  String wifiPassword = "";
  String macHost = "";
  uint16_t port = DEFAULT_PORT;
  uint8_t rotation = 0; // 0 = default (3 for hardware, 1 for Wokwi)
  bool isConfigured = false;
};

class KeyboardDriver;

class ConfigManager {
public:
  ConfigManager();
  bool load(AppConfig& config);
  void save(const AppConfig& config);
  void clear();
  bool runSetupPortal(DisplayUI& ui, AppConfig& config, KeyboardDriver* keyboard = nullptr);

private:
  Preferences prefs;
  void sanitizeHost(String& host);
};
