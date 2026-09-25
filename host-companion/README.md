# Cardputer Now Playing - macOS Companion Bridge

A lightweight local service that interfaces natively with macOS `Music.app` via AppleScript / JXA (`osascript`) and downscales album art via macOS's built-in `sips` engine into ESP32-ready formats (raw RGB565 and JPEG).

## Features
- **Zero Third-Party Python Dependencies**: Uses standard Python 3 and native macOS tools (`osascript`, `sips`). No `pip install` required!
- **Fast & Responsive**: JXA queries execute in < 50ms with sub-second polling support.
- **ESP32-Optimized Binary Artwork**: Generates raw 86x86 16-bit RGB565 binary buffers (`/artwork.raw`, 14,792 bytes) for instant zero-copy DMA/SPI rendering without JPEG decode CPU overhead.
- **Smart Change Detection**: Provides an `artwork_id` in `/api/now-playing` so the ESP32 only downloads artwork when the track actually changes.
- **Live Web Dashboard**: Visit `http://localhost:58329/` in your browser for a live preview of the metadata, thumbnail, and simulator target endpoints.

## Running the Bridge
```bash
python3 host-companion/bridge.py
```
By default, the server runs on dedicated obscure port `58329` (conflict-free, avoiding Apple AirPlay Receiver on 5000 and standard HTTP ports):
```bash
# Optional custom port:
python3 host-companion/bridge.py --port 58329
```

## Available Endpoints
- `GET /api/now-playing` - Returns JSON track metadata and Mac time:
  ```json
  {
    "running": true,
    "state": "playing",
    "title": "Song Title",
    "artist": "Artist Name",
    "album": "Album Name",
    "duration": 229,
    "elapsed": 129,
    "artwork_id": "8f14e45f94b4",
    "clock": "18:52"
  }
  ```
- `POST /api/toggle` (or `GET /api/toggle`) - Toggle Play/Pause in Apple Music.
- `POST /api/next` (or `GET /api/next`) - Skip to next track.
- `POST /api/previous` (or `GET /api/previous`) - Return to previous track.
- `GET /artwork.raw` or `/artwork.rgb565` - Raw 86x86 16-bit RGB565 binary data (14,792 bytes).
- `GET /artwork.jpg` - Downscaled 86x86 JPEG image.
- `GET /health` - Returns `OK`.
- `GET /` - Web preview & status dashboard.

## Connecting from Wokwi Simulator
In Wokwi's virtual WiFi environment (`Wokwi-GUEST`), connect to your Mac using:
```
http://host.wokwi.internal:58329/api/now-playing
http://host.wokwi.internal:58329/artwork.raw
```
*(On physical hardware, the device connects to your Mac's LAN IP address on port `58329`, e.g., `http://192.168.1.150:58329`).*

