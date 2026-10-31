#pragma once
// One shortcut map for Wi-Fi, BLE and tests. Setup uses only a G0 hold.
inline const char* playbackCommand(char key) {
  switch (key) {
    case ' ': return "toggle";
    case 'p': case 'P': return "previous";
    case 'n': case 'N': return "next";
    case ',': case '<': case '[': case '{': return "backward";
    case '.': case '>': case ']': case '}': return "forward";
    default: return nullptr;
  }
}
