#include "keyboard_driver.h"
#include <Wire.h>

#define TCA8418_I2C_ADDR    0x34
#define REG_CFG             0x01
#define REG_INT_STAT        0x02
#define REG_KEY_LCK_EC      0x03
#define REG_KEY_EVENT_A     0x04
#define REG_KP_GPIO1        0x1D // ROW0..ROW6 (0x7F)
#define REG_KP_GPIO2        0x1E // COL0..COL7 (0xFF)

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

static bool shiftActive = false;
static bool isTcaPresent = false;
static bool isIoMatrixPresent = false;

// Fallback IO Matrix pins for standard Cardputer (non-Adv)
static const int ioOutputPins[3] = {8, 9, 11};
static const int ioInputPins[7]  = {13, 15, 3, 4, 5, 6, 7};

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

KeyboardDriver::KeyboardDriver() : hardwarePresent(false) {}

bool KeyboardDriver::begin() {
#if defined(TARGET_WOKWI_SIMULATOR)
  hardwarePresent = false;
  Serial.println("[Keyboard] Wokwi Simulator active. Serial keyboard ready.");
  return false;
#else
  // 1. First probe Cardputer-Adv TCA8418 I2C keyboard on SDA=2, SCL=1
  Wire.begin(2, 1, 400000);
  Wire.beginTransmission(TCA8418_I2C_ADDR);
  if (Wire.endTransmission() == 0) {
    // Configure 7 rows and 8 columns for keypad scanning
    tcaWriteReg(REG_KP_GPIO1, 0x7F);
    tcaWriteReg(REG_KP_GPIO2, 0xFF);
    tcaWriteReg(REG_CFG, 0x01);      // Enable keypad events & auto-increment
    tcaWriteReg(REG_INT_STAT, 0x01); // Clear interrupts

    isTcaPresent = true;
    hardwarePresent = true;
    Serial.println("[Keyboard] TCA8418 hardware keyboard detected on Cardputer-Adv!");
    return true;
  }

  // 2. If TCA8418 not detected, probe standard Cardputer IO Matrix (74HC138)
  for (int pin : ioOutputPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
  for (int pin : ioInputPins) {
    pinMode(pin, INPUT_PULLUP);
  }
  isIoMatrixPresent = true;
  hardwarePresent = true;
  Serial.println("[Keyboard] Standard Cardputer 74HC138 matrix keyboard initialized!");
  return true;
#endif
}

char KeyboardDriver::getKey() {
#if !defined(TARGET_WOKWI_SIMULATOR)
  // 1. Scan TCA8418 on Cardputer-Adv
  if (isTcaPresent) {
    uint8_t count = tcaReadReg(REG_KEY_LCK_EC) & 0x0F;
    while (count > 0) {
      uint8_t event = tcaReadReg(REG_KEY_EVENT_A);
      tcaWriteReg(REG_INT_STAT, 0x01); // Clear interrupt
      count--;

      bool isPress = (event & 0x80) != 0;
      uint8_t keyCode = (event & 0x7F);
      if (keyCode >= 1) {
        uint8_t buffer = keyCode - 1;
        uint8_t raw_row = buffer / 10;
        uint8_t raw_col = buffer % 10;

        // Cardputer interleaved matrix remap formula
        uint8_t col = raw_row * 2;
        if (raw_col > 3) col++;
        uint8_t row = (raw_col + 4) % 4;

        // Check Shift key (Row 2, Col 1)
        if (row == 2 && col == 1) {
          shiftActive = isPress;
          continue;
        }

        if (isPress && row < 4 && col < 14) {
          char c = shiftActive ? keyMapShift[row][col] : keyMapNormal[row][col];
          if (c) {
            Serial.printf("[Key] Physical key pressed: '%c' (row %u, col %u)\n", c, row, col);
            return c;
          }
        }
      }
    }
  }
  // 2. Scan Standard Cardputer IO Matrix
  else if (isIoMatrixPresent) {
    for (int i = 0; i < 8; i++) {
      digitalWrite(ioOutputPins[0], (i & 0x01) ? HIGH : LOW);
      digitalWrite(ioOutputPins[1], (i & 0x02) ? HIGH : LOW);
      digitalWrite(ioOutputPins[2], (i & 0x04) ? HIGH : LOW);
      delayMicroseconds(5);

      for (int r = 0; r < 7; r++) {
        if (digitalRead(ioInputPins[r]) == LOW) {
          // Debounce delay
          delay(15);
          if (digitalRead(ioInputPins[r]) == LOW) {
            uint8_t col = (i * 2) + (r >= 4 ? 1 : 0);
            uint8_t row = (r % 4);
            if (row < 4 && col < 14) {
              char c = shiftActive ? keyMapShift[row][col] : keyMapNormal[row][col];
              if (c) return c;
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
    Serial.printf("[Key] Serial key received: '%c'\n", c);
    return c;
  }

  return 0;
}
