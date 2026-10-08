#if __has_include("keyboard_driver.h")
#include "keyboard_driver.h"
#else
#include "../include/keyboard_driver.h"
#endif

#if !defined(TARGET_WOKWI_SIMULATOR)
#include <M5Cardputer.h>
#endif

KeyboardDriver::KeyboardDriver() : hardwarePresent(false) {}

bool KeyboardDriver::begin() {
#if !defined(TARGET_WOKWI_SIMULATOR)
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  hardwarePresent = true;
  Serial.println("[Keyboard] Official M5Cardputer keyboard driver initialized!");
  return true;
#else
  hardwarePresent = false;
  Serial.println("[Keyboard] Wokwi Simulator active. Serial keyboard ready.");
  return false;
#endif
}

char KeyboardDriver::getKey() {
#if !defined(TARGET_WOKWI_SIMULATOR)
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (status.space) {
      Serial.println("[Key] Space pressed");
      return ' ';
    }
    if (status.enter) {
      Serial.println("[Key] Enter pressed");
      return '\n';
    }
    if (status.del) {
      Serial.println("[Key] Backspace pressed");
      return '\b';
    }
    for (auto c : status.word) {
      if (c) {
        Serial.printf("[Key] Physical key pressed: '%c'\n", c);
        return c;
      }
    }
  }
#endif

  // Serial input fallback (for Wokwi simulation and USB console)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return 0;
    Serial.printf("[Key] Serial key received: '%c'\n", c);
    return c;
  }

  return 0;
}
