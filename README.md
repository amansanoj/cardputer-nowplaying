# Wireless Apple Music Now Playing Display
### ESP32-S3 + ST7789 IPS LCD (240x135 Landscape)

A minimalist, wireless **Now Playing** display for Apple Music on macOS. Features zero-flicker double-buffered rendering on a 240x135 ST7789 IPS LCD, smooth local second-by-second timestamp interpolation, and a native macOS companion service with zero third-party Python dependencies.

Supports both the **Wokwi Simulator** (for rapid virtual iteration) and physical hardware such as the **M5Stack Cardputer / Cardputer-Adv** or generic ESP32-S3 boards.

---

### Aesthetic & Display Specifications

- **Canvas**: 240 × 135 pixels (Landscape)
- **Palette**: Strict Minimalist
  - Background: Pure Pitch-Black (`#000000` / `0x0000`)
  - Typography: Crisp White (`#FFFFFF` / `0xFFFF`)
  - Subtitle / Album / Muted: Neutral Gray (`#808080` / `0x8410`)
  - Track Bar Background: Subtle Dark Gray (`#333333` / `0x3186`)
- **Status Header (`y = 15`)**:
  - **Local Clock**: Synced from Mac local time (e.g. `18:52`) in subtle gray
  - **Wi-Fi RSSI Indicator**: 3-bar signal meter dynamically updated via `WiFi.RSSI()`
  - **Battery Meter**: Real-time voltage sensing with percentage (`100%`) and charging bolt indicator (`⚡`) or battery level fill
- **Geometry & Padding**:
  - **Cover Art**: Square `86x86 px`, positioned at `(x=15, y=15)`.
  - **Dynamic Island Pause Badge**: When paused, an elegant 16×16px rounded pill overlay with two crisp white pause bars (`❚❚`) appears on the bottom-right corner of the artwork, completely eliminating text layout shifts.
  - **Consistent Padding**: 15px top = 15px left = 15px bottom distance from art to the timeline bar.
  - **Metadata Column**: Starting at `x=113` with 113px text width:
    - **Title**: 1 line, white, marquee scrolling on long titles
    - **Artist**: 1 line, white, marquee scrolling on long names
    - **Album**: 1 line, muted gray, marquee scrolling on long names
    - **Synchronized Shared Clock Marquee**: Overflowing lines pause at the start together for **10 full seconds**, scroll forward simultaneously at 40ms/px, park smoothly upon finishing, and reset the shared 10-second timer once all lines have completed their wrap.
  - **Bottom Bar**:
    - Timestamp Left: `MM:SS` (elapsed)
    - Center: 2px progress bar with playhead knob on optical centerline (`y = 119.5`)
    - Timestamp Right: `MM:SS` (duration)
- **Rendering Engine**: `GFXcanvas16` off-screen double-buffering at 20 FPS (50ms interval). The entire frame is rendered in RAM and transferred in a single burst via SPI to eliminate flicker.

---

## Architecture Overview

```
┌──────────────────────────────────────┐           HTTP (WiFi)           ┌──────────────────────────────────────┐
│         macOS Host Companion         │ ◄─────────────────────────────► │            ESP32-S3 Device           │
│                                      │   GET /api/now-playing (JSON)   │                                      │
│  - AppleScript/JXA (Music.app)       │   POST /api/toggle, next, prev  │  - M5Stack Cardputer / Cardputer-Adv │
│  - Native sips 86x86 Downscale       │   GET /artwork.raw (RGB565)     │  - 240x135 ST7789 IPS LCD (20 FPS)   │
│  - Port 58329 HTTP Server            │                                 │  - Captive Portal Setup (NVS)        │
│  - Playback Controls (/api/toggle...)│                                 │  - Physical 56-key Keyboard Controls │
└──────────────────────────────────────┘                                 │  - Live Battery (GPIO 10) & RSSI     │
                                                                         └──────────────────────────────────────┘
```

---

## Project Structure

```
cardputer-nowplaying/
├── host-companion/
│   ├── bridge.py           # Native macOS AppleScript & HTTP server (Port 58329, zero pip deps)
│   ├── test_bridge.py      # Automated test verifying AppleScript query & RGB565 conversion
│   └── README.md           # Companion bridge documentation
├── firmware/
│   ├── platformio.ini      # PlatformIO build configuration (esp32s3 & cardputer-adv)
│   ├── include/
│   │   ├── config.h        # Pin assignments, WiFi settings & server endpoints
│   │   ├── config_manager.h# NVS Preferences storage & captive portal onboarding
│   │   ├── keyboard_driver.h# Cardputer-Adv TCA8418 I2C & Cardputer matrix driver
│   │   ├── display_ui.h    # Double-buffered graphics, status bar & synchronized marquee
│   │   ├── music_client.h  # Dynamic HTTP client, playback controls & JSON parser
│   │   └── arduino_compat.h# Arduino compatibility helpers
│   └── src/
│       ├── main.cpp        # Application loop, keyboard handler & 50ms render tick
│       ├── config_manager.cpp# Captive portal web server & DNS responder
│       ├── keyboard_driver.cpp# I2C TCA8418 keypad controller & serial fallback
│       ├── display_ui.cpp  # GFXcanvas16 double-buffering, status bar & marquee
│       └── music_client.cpp# Network polling, controls & binary artwork stream
├── releases/               # Factory and firmware release binaries
├── diagram.json            # Wokwi simulation circuit (ESP32-S3 + ST7789)
├── wokwi.toml              # Wokwi simulation configuration
├── st7789.chip.c           # Custom Wokwi ST7789 C simulator driver
├── st7789.chip.json        # Custom Wokwi chip pin specification
├── st7789.chip.wasm        # Compiled WebAssembly ST7789 simulation model
├── wokwi-api.h             # Wokwi chip API header
└── README.md
```

---

## 1. Quickstart: macOS Host Companion Bridge

The companion bridge runs natively on macOS using standard Python 3 and macOS built-in tools (`osascript` and `sips`). No virtual environments or `pip install` commands are needed!

1. Open a terminal and start the bridge:
   ```bash
   python3 host-companion/bridge.py
   ```
2. You will see:
   ```
   ============================================================
    Apple Music Now Playing - ESP32 / Wokwi Companion Bridge
   ============================================================
    * Listening on        : http://0.0.0.0:58329
    * Mac Local IP        : http://192.168.1.X:58329
    * For Wokwi Simulator : http://host.wokwi.internal:58329
    * Metadata Endpoint   : http://host.wokwi.internal:58329/api/now-playing
    * Artwork RGB565      : http://host.wokwi.internal:58329/artwork.raw (86x86 px)
   ============================================================
   ```
3. Open `http://localhost:58329/` in your browser for a live preview of the current track, downscaled album art, controls, and connection status.

---

## 2. First-Boot Onboarding & Wi-Fi Setup

On physical hardware (such as the M5Stack Cardputer / Cardputer-Adv), you **never need to hardcode Wi-Fi credentials**:

1. **First Boot**: If no credentials are saved in flash memory (NVS), the device automatically boots into **Setup Mode**.
2. **Connect**: On your phone or laptop, connect to the Wi-Fi access point:
   - **SSID**: `cardputer-nowplaying-setup`
   - *(No password required)*
3. **Configure**: Open your browser to `http://192.168.4.1` (or let the captive portal pop up automatically):
   - Select your Wi-Fi network from the scanned list (or enter manually)
   - Enter your Wi-Fi password
   - Enter your Mac's LAN IP address (e.g. `192.168.1.150`)
   - Port is pre-filled with dedicated port `58329`
   - Click **Save & Connect**
4. **Saved!** The Cardputer saves the settings permanently to flash (NVS `Preferences`) and connects.
5. **Re-entering Setup Mode Anytime**:
   - Press **`s`** on the Cardputer keyboard, OR
   - Hold the **G0 / Boot button** while powering on, OR
   - Connect via USB Serial (115200 baud) and type `s` or `SETUP`.

---

## 3. Physical Hardware Controls (Cardputer-Adv Keyboard)

When running on the Cardputer / Cardputer-Adv, you have full playback controls directly from the physical keyboard:

| Key | Action | Endpoint |
|---|---|---|
| **`Space`** | Play / Pause Toggle | `POST /api/toggle` |
| **`n`** or **`>`** | Next Track | `POST /api/next` |
| **`p`** or **`<`** | Previous Track | `POST /api/previous` |
| **`s`** | Re-enter Wi-Fi Setup Portal | Launches AP mode |
| **`r`** | Factory Reset (Clears NVS flash) | Wipes config & restarts |

*(Also supported via USB Serial monitor input when testing in the Wokwi simulator!)*

---

## 4. Flashing Pre-built Release Binaries

Download the ready-to-flash binaries from the **[GitHub Releases v1.0.0](https://github.com/amansanoj/cardputer-nowplaying/releases/tag/v1.0.0)** page:

- **`cardputer-adv-v1.0.0-factory.bin`**: Complete all-in-one factory image (bootloader + partition table + boot_app0 + application firmware). Flash directly at offset **`0x0`**.
- **`cardputer-adv-v1.0.0-firmware.bin`**: Application firmware only (flash at offset `0x10000`).
- **`wokwi-esp32s3-v1.0.0-firmware.bin`**: Firmware target for Wokwi Simulator or generic ESP32-S3 DevKits.

### Flash with Web Browser (Zero Install)
1. Open [ESP Web Flasher](https://espressif.github.io/esptool-js/) or [Adafruit WebSerial ESPTool](https://adafruit.github.io/Adafruit_WebSerial_ESPTool/) in Chrome or Edge.
2. Connect your Cardputer via USB and click **Connect** (115200 or 1500000 baud).
3. Choose `cardputer-adv-v1.0.0-factory.bin` at offset **`0x0`**.
4. Click **Program / Flash**.

### Flash with esptool
```bash
esptool.py --chip esp32s3 write_flash 0x0000 cardputer-adv-v1.0.0-factory.bin
```

---

## 5. Running in Wokwi Simulator

Wokwi connects to your host Mac through its internal virtual gateway:
- **Virtual WiFi AP**: `Wokwi-GUEST` (password: `""`)
- **Host Alias**: `host.wokwi.internal:58329`

### Wokwi in VS Code
1. Install the **Wokwi Simulator** extension in VS Code.
2. Build the simulator firmware target:
   ```bash
   cd firmware && pio run -e esp32s3
   ```
3. Press `Cmd + Shift + P`, select **Wokwi: Start Simulator**.
4. The virtual ESP32-S3 will connect to `Wokwi-GUEST`, query `http://host.wokwi.internal:58329`, fetch the 86x86 album art, and render track updates in real time.

---

## 6. Hardware Pinout Reference

| Signal | ESP32-S3 (Wokwi) | M5Stack Cardputer-Adv (K132-Adv) | M5Stack Cardputer v1.x | Description |
|---|---|---|---|---|
| **MOSI / SDA** | GPIO 11 | GPIO 35 | GPIO 6 | SPI Display Data |
| **SCLK / SCL** | GPIO 12 | GPIO 36 | GPIO 8 | SPI Display Clock |
| **CS** | GPIO 10 | GPIO 37 | GPIO 37 | Display Chip Select |
| **DC** | GPIO 9 | GPIO 34 | GPIO 4 | Display Data / Command |
| **RST** | GPIO 8 | GPIO 33 | GPIO 33 | Display Reset |
| **BL** | N/A | GPIO 38 | GPIO 38 | Backlight Power / Control |
| **BAT_ADC** | N/A | GPIO 10 | GPIO 10 | Battery Voltage ADC (Ratio 2.0) |
| **KEYPAD_SDA** | N/A | GPIO 8 | Matrix | TCA8418 I2C Keyboard SDA |
| **KEYPAD_SCL** | N/A | GPIO 9 | Matrix | TCA8418 I2C Keyboard SCL |
| **KEYPAD_INT** | N/A | GPIO 11 | Matrix | TCA8418 I2C Keypad Interrupt |
| **BTN_SETUP** | GPIO 0 | GPIO 0 (G0) | GPIO 0 (G0) | Setup Portal Button |

---

## 7. Technical Highlights

- **Dedicated Port `58329`**: Uses a high, unassigned ephemeral port to guarantee zero conflict with Apple AirPlay Receiver (port 5000), development servers (port 3000/8080), or other local tools.
- **Synchronized Shared Clock Marquee**: All overflowing text lines pause together for 10 seconds, scroll forward simultaneously in an infinite wrap, park smoothly upon completion, and reset the shared 10s timer only when all lines have finished.
- **Hardware-Clipped Text Gutters**: Characters are clipped to a strict 113px column using hardware blanking gutters, preventing text from ever bleeding onto the album art or screen borders.
- **Battery Sensing**: Reads the Cardputer's internal LiPo battery through GPIO 10 with a rolling exponential moving average, displaying exact percentage, level fill, and USB charging indicator (`⚡`).
- **Binary Artwork Format (`/artwork.raw`)**: Converts the downscaled 86x86 image directly into 16-bit RGB565 big-endian format (14,792 bytes). The ESP32 copies this directly into display memory with zero CPU overhead and zero JPEG decoder dependencies.
- **Smart Change Detection**: The bridge computes an MD5 `artwork_id`. The ESP32 only requests the 14.8KB artwork when the track actually changes, saving bandwidth and battery.
- **20 FPS Zero-Flicker Double-Buffering**: The display loop ticks at 50ms (20 FPS) for silky smooth text scrolling and local 1-second timestamp interpolation.
- **Local Time Interpolation**: The ESP32 polls the server every 3 seconds to stay lightweight, but interpolates the elapsed time every 1,000ms locally so the timestamp increments smoothly with zero lag.
