# Wireless Apple Music Now Playing Display
### ESP32-S3 + ST7789 IPS LCD (240x135 Landscape)

A minimalist, wireless **Now Playing** display for Apple Music on macOS. Features zero-flicker double-buffered rendering on a 240x135 ST7789 IPS LCD, smooth local second-by-second timestamp interpolation, and a native macOS companion service with zero third-party Python dependencies.

Supports both the **Wokwi Simulator** (for rapid virtual iteration) and physical hardware such as the **M5Stack Cardputer / Cardputer-Adv** or generic ESP32-S3 boards.

---

## Aesthetic & Display Specifications

- **Canvas**: 240 × 135 pixels (Landscape)
- **Palette**: Strict Minimalist
  - Background: Pure Pitch-Black (`#000000` / `0x0000`)
  - Typography: Crisp White (`#FFFFFF` / `0xFFFF`)
  - Accent / Track Bar: Muted Gray (`0x4208`) with White Playhead Indicator
- **Geometry & Padding**:
  - **Cover Art**: Square `86x86 px`, positioned at `(x=15, y=15)`.
  - **Consistent Padding**: Top padding (15px) = Left padding (15px) = Bottom distance to timestamp row (15px: $116 - 101 = 15\text{px}$).
  - **Metadata Column**: Starting at `x=112`:
    - Title: Reserved 2 lines, bold typography with word wrap or auto-truncation
    - Artist: 1 line
    - Album: 1 line
    - Entire 4-line metadata column is vertically centered against the 86px album art.
  - **Bottom Bar**:
    - Timestamp Left: `MM:SS` (elapsed)
    - Center: 2px progress bar with playhead knob on optical centerline (`y = 119.5`)
    - Timestamp Right: `MM:SS` (duration)
- **Rendering Engine**: `GFXcanvas16` off-screen double-buffering. The entire frame is rendered in RAM and transferred in a single burst via SPI to eliminate flicker.

---

## Architecture Overview

```
┌─────────────────────────────────┐           HTTP (WiFi)           ┌──────────────────────────────────────┐
│       macOS Host Companion      │ ◄─────────────────────────────► │            ESP32-S3 Device           │
│                                 │   GET /api/now-playing (JSON)   │                                      │
│  - AppleScript/JXA (Music.app)  │   GET /artwork.raw (RGB565)     │  - Wokwi Simulator or Physical Board │
│  - Native sips 86x86 Downscale  │                                 │  - 240x135 ST7789 IPS LCD            │
│  - Port 5001 HTTP Server        │                                 │  - Smooth 1s Local Interpolation     │
└─────────────────────────────────┘                                 │  - GFXcanvas16 Double-Buffering      │
                                                                    └──────────────────────────────────────┘
```

---

## Project Structure

```
cardputer-nowplaying/
├── host-companion/
│   ├── bridge.py           # Native macOS AppleScript & HTTP server (Zero pip dependencies)
│   ├── test_bridge.py      # Automated test verifying AppleScript query & RGB565 conversion
│   └── README.md           # Companion bridge documentation
├── firmware/
│   ├── platformio.ini      # PlatformIO build configuration (esp32s3 & cardputer-adv)
│   ├── include/
│   │   ├── config.h        # Pin assignments, WiFi settings & server endpoints
│   │   ├── display_ui.h    # Double-buffered graphics & typography header
│   │   ├── music_client.h  # HTTP client & JSON deserializer header
│   │   └── arduino_compat.h# Arduino compatibility helpers
│   └── src/
│       ├── main.cpp        # Application loop & time interpolation
│       ├── display_ui.cpp  # GFXcanvas16 sprite rendering
│       └── music_client.cpp# Network polling and binary artwork stream
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
    * Listening on        : http://0.0.0.0:5001
    * Mac Local IP        : http://192.168.1.X:5001
    * For Wokwi Simulator : http://host.wokwi.internal:5001
    * Metadata Endpoint   : http://host.wokwi.internal:5001/api/now-playing
    * Artwork RGB565      : http://host.wokwi.internal:5001/artwork.raw (86x86 px)
   ============================================================
   ```
3. Open `http://localhost:5001/` in your browser for a live preview of the current track and downscaled album art.

---

## 2. Running in Wokwi Simulator

Wokwi connects to your host Mac through its internal virtual gateway:
- **Virtual WiFi AP**: `Wokwi-GUEST` (password: `""`)
- **Host Alias**: `host.wokwi.internal` (maps to `localhost` on your Mac)

### Wokwi in VS Code
1. Install the **Wokwi Simulator** extension in VS Code.
2. Build the simulator firmware target:
   ```bash
   cd firmware && pio run -e esp32s3
   ```
3. Press `Cmd + Shift + P`, select **Wokwi: Start Simulator**.
4. The virtual ESP32-S3 will connect to `Wokwi-GUEST`, query `http://host.wokwi.internal:5001`, fetch the 86x86 album art, and render track updates in real time.

---

## 3. Deploying to Physical Hardware (M5Stack Cardputer / Cardputer-Adv)

### Target Environment: `cardputer-adv`

To build and upload directly to an M5Stack Cardputer or Cardputer-Adv:

1. Configure WiFi credentials in [`firmware/include/config.h`](file:///Users/aman/dev/amansanoj/cardputer-nowplaying/firmware/include/config.h):
   ```cpp
   #define WIFI_SSID     "Your-Home-WiFi"
   #define WIFI_PASSWORD "Your-WiFi-Password"
   #define SERVER_HOST   "http://192.168.1.X:5001" // Your Mac's LAN IP
   ```
2. Build and upload:
   ```bash
   cd firmware
   pio run -e cardputer-adv -t upload -t monitor
   ```

---

## 4. Hardware Pinout Reference

| Signal | ESP32-S3 (Wokwi Diagram) | M5Stack Cardputer / Adv | Description |
|---|---|---|---|
| **MOSI / SDA** | GPIO 6 | GPIO 35 | SPI Data |
| **SCLK / SCL** | GPIO 8 | GPIO 36 | SPI Clock |
| **CS** | GPIO 5 | GPIO 37 | Chip Select |
| **DC** | GPIO 4 | GPIO 34 | Data / Command |
| **RST** | GPIO 7 | GPIO 33 | Display Reset |
| **BL** | N/A | GPIO 38 | Backlight Power / Control |

---

## 5. Technical Highlights

- **Binary Artwork Format (`/artwork.raw`)**: Converts the downscaled 86x86 image directly into 16-bit RGB565 big-endian format (14,792 bytes). The ESP32 copies this directly into display memory with zero CPU overhead and zero JPEG decoder dependencies.
- **Smart Change Detection**: The bridge computes an MD5 `artwork_id`. The ESP32 only requests the 14.8KB artwork when the track actually changes, saving bandwidth and battery.
- **Local Time Interpolation**: The ESP32 polls the server every 3 seconds to stay lightweight, but interpolates the elapsed time every 1,000ms locally so the timestamp increments smoothly with zero lag.
