#pragma once
// One shortcut map for Wi-Fi, BLE and tests. Setup uses only a G0 hold.
inline const char* playbackCommand(char key) {
  switch (key) {
    case ' ': return "toggle";
    case '[': case '{': return "previous";
    case ']': case '}': return "next";
    case ',': case '<': return "backward";
    case '.': case '>': return "forward";
    default: return nullptr;
  }
}
