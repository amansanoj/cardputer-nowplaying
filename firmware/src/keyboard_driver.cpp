#include "keyboard_driver.h"
#include <Wire.h>

#define TCA8418_I2C_ADDR    0x34
#define REG_CFG             0x01
#define REG_INT_STAT        0x02
#define REG_KEY_LCK_EC      0x03
#define REG_KEY_EVENT_A     0x04
#define REG_KP_GPIO1        0x1D
#define REG_KP_GPIO2        0x1E

// Cardputer physical layout mapped to 4 rows x 14 columns
static const char keyMapNormal[4][14] = {
  { '`', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b' },
  { '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\' },
  {  0 ,   0 , 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\n' },
  {  0 ,   0 ,  0 , 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', ' ' }
};

static const char keyMapShift[4][14] = {
  { '~', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b' },
  { '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|' },
  {  0 ,   0 , 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '\n' },
  {  0 ,   0 ,  0 , 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', ' ' }
};

static const char keyMapFn[4][14] = {
  { 0x1B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ';', '\'', '\n' },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ',', '.', '/', ' ' }
};

static bool shiftActive = false;
static bool fnActive    = false;
static bool isTcaPresent = false;
static bool isIoMatrixPresent = false;

// Standard Cardputer 74HC138 matrix pins
static const int ioOutputPins[3] = {8, 9, 11};
static const int ioInputPins[7]  = {13, 15, 3, 4, 5, 6, 7};
static const uint8_t X_map_chart[7][2] = {
  {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}, {12, 13}
};

// Previous state bitmask for 4 rows x 14 cols (56 keys)
static uint64_t prevKeyMatrix = 0;

static uint8_t tcaReadReg(uint8_t reg) {
  Wire.beginTransmission(TCA8418_I2C_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)TCA8418_I2C_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

static void tcaWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA8418_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static bool probeTcaOnPins(int sda, int scl) {
  Wire.begin(sda, scl, 100000);
  Wire.setTimeOut(15); // Strict 15ms timeout prevents hang on unpulled lines
  Wire.beginTransmission(TCA8418_I2C_ADDR);
  uint8_t err = Wire.endTransmission();
  if (err == 0) {
    return true;
  }
  Wire.end();
  return false;
}

KeyboardDriver::KeyboardDriver() : hardwarePresent(false) {}

bool KeyboardDriver::begin() {
#if defined(TARGET_WOKWI_SIMULATOR)
  hardwarePresent = false;
  Serial.println("[Keyboard] Wokwi Simulator active. Serial keyboard ready.");
  return false;
#else
  // 1. Probe TCA8418 on SDA=8, SCL=9 (Cardputer-Adv internal)
  if (probeTcaOnPins(8, 9)) {
    isTcaPresent = true;
    hardwarePresent = true;
    Serial.println("[Keyboard] TCA8418 detected on pins (8, 9)!");
  }
  // 2. Probe TCA8418 on SDA=2, SCL=1 (Cardputer Grove / alternate)
  else if (probeTcaOnPins(2, 1)) {
    isTcaPresent = true;
    hardwarePresent = true;
    Serial.println("[Keyboard] TCA8418 detected on pins (2, 1)!");
  }

  if (isTcaPresent) {
    tcaWriteReg(REG_KP_GPIO1, 0x7F);
    tcaWriteReg(REG_KP_GPIO2, 0xFF);
    tcaWriteReg(REG_CFG, 0x01);
    tcaWriteReg(REG_INT_STAT, 0x01);
    return true;
  }

  // 3. Fallback to standard Cardputer 74HC138 matrix
  for (int pin : ioOutputPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  for (int pin : ioInputPins) {
    pinMode(pin, INPUT_PULLUP);
  }
  isIoMatrixPresent = true;
  hardwarePresent = true;
  Serial.println("[Keyboard] Standard Cardputer 74HC138 IO matrix initialized!");
  return true;
#endif
}

char KeyboardDriver::getKey() {
#if !defined(TARGET_WOKWI_SIMULATOR)
  // 1. TCA8418 scan (Cardputer-Adv)
  if (isTcaPresent) {
    uint8_t count = tcaReadReg(REG_KEY_LCK_EC) & 0x0F;
    while (count > 0) {
      uint8_t event = tcaReadReg(REG_KEY_EVENT_A);
      tcaWriteReg(REG_INT_STAT, 0x01);
      count--;

      bool isPress = (event & 0x80) != 0;
      uint8_t keyCode = (event & 0x7F);
      if (keyCode >= 1) {
        uint8_t buffer = keyCode - 1;
        uint8_t raw_row = buffer / 10;
        uint8_t raw_col = buffer % 10;

        uint8_t col = raw_row * 2 + (raw_col > 3 ? 1 : 0);
        uint8_t row = (raw_col + 4) % 4;

        if (row == 2 && col == 1) {
          shiftActive = isPress;
          continue;
        }
        if (row == 2 && col == 0) {
          fnActive = isPress;
          continue;
        }

        if (isPress && row < 4 && col < 14) {
          char c = fnActive ? keyMapFn[row][col] : (shiftActive ? keyMapShift[row][col] : keyMapNormal[row][col]);
          if (c) {
            Serial.printf("[Key] TCA8418 key: '%c' (row %u, col %u)\n", c, row, col);
            return c;
          }
        }
      }
    }
  }
  // 2. Standard Cardputer 74HC138 Matrix scan
  else if (isIoMatrixPresent) {
    uint64_t currentMatrix = 0;

    for (int i = 0; i < 8; i++) {
      digitalWrite(ioOutputPins[0], (i & 0x01) ? HIGH : LOW);
      digitalWrite(ioOutputPins[1], (i & 0x02) ? HIGH : LOW);
      digitalWrite(ioOutputPins[2], (i & 0x04) ? HIGH : LOW);
      delayMicroseconds(5);

      for (int j = 0; j < 7; j++) {
        if (digitalRead(ioInputPins[j]) == LOW) {
          uint8_t col = (i > 3) ? X_map_chart[j][0] : X_map_chart[j][1];
          uint8_t raw_y = (i > 3) ? (i - 4) : i;
          uint8_t row = 3 - raw_y;

          if (row < 4 && col < 14) {
            uint8_t keyIndex = row * 14 + col;
            currentMatrix |= ((uint64_t)1 << keyIndex);
          }
        }
      }
    }

    // Reset outputs
    digitalWrite(ioOutputPins[0], LOW);
    digitalWrite(ioOutputPins[1], LOW);
    digitalWrite(ioOutputPins[2], LOW);

    // Update modifiers
    shiftActive = (currentMatrix & ((uint64_t)1 << (2 * 14 + 1))) != 0; // Row 2, Col 1
    fnActive    = (currentMatrix & ((uint64_t)1 << (2 * 14 + 0))) != 0; // Row 2, Col 0

    // Detect newly pressed keys (rising edge in currentMatrix vs prevKeyMatrix)
    uint64_t newlyPressed = currentMatrix & ~prevKeyMatrix;
    prevKeyMatrix = currentMatrix;

    if (newlyPressed != 0) {
      for (uint8_t row = 0; row < 4; row++) {
        for (uint8_t col = 0; col < 14; col++) {
          uint8_t keyIndex = row * 14 + col;
          if (newlyPressed & ((uint64_t)1 << keyIndex)) {
            // Skip pure modifier keys
            if ((row == 2 && col <= 1) || (row == 3 && col <= 2)) continue;

            char c = fnActive ? keyMapFn[row][col] : (shiftActive ? keyMapShift[row][col] : keyMapNormal[row][col]);
            if (c) {
              Serial.printf("[Key] Cardputer key: '%c' (row %u, col %u)\n", c, row, col);
              return c;
            }
          }
        }
      }
    }
  }
#endif

  // Serial input fallback (for Wokwi simulation and USB console)
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return 0;
    Serial.printf("[Key] Serial key: '%c'\n", c);
    return c;
  }

  return 0;
}
