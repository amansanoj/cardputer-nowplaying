#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "display_ui.h"

struct AppConfig {
  String wifiSsid = "";
  String wifiPassword = "";
  String macHost = "";
  uint16_t port = DEFAULT_PORT;
  bool isConfigured = false;
};

class ConfigManager {
public:
  ConfigManager();
  bool load(AppConfig& config);
  void save(const AppConfig& config);
  void clear();
  void runSetupPortal(DisplayUI& ui, AppConfig& config);

private:
  Preferences prefs;
  void sanitizeHost(String& host);
};
