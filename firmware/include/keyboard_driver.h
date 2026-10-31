#pragma once
#include <Arduino.h>

class KeyboardDriver {
public:
  KeyboardDriver();
  bool begin();
  char getKey(bool readSerial = true);
  bool isHardwarePresent() const { return hardwarePresent; }

private:
  bool hardwarePresent;
};
