#pragma once
#include <Arduino.h>

class KeyboardDriver {
public:
  KeyboardDriver();
  bool begin();
  char getKey();
  bool isHardwarePresent() const { return hardwarePresent; }

private:
  bool hardwarePresent;
};
