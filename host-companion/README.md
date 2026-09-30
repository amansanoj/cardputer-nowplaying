# Cardputer Now Playing - macOS Companion Bridge

A unified, high-performance companion daemon for macOS that streams live playback status, track metadata, and binary album artwork from **Apple Music** and **Spotify** to the **M5Stack Cardputer-Adv** (ESP32-S3) and the **Wokwi Simulator**.

Supports **direct Bluetooth Low Energy (BLE)** wireless connectivity for the physical Cardputer (no Wi-Fi passwords, no router needed) as well as **HTTP REST & WebSocket** push for the Wokwi simulator and local Wi-Fi networks.

---

## Features

- **Direct BLE Wireless Connection**: Connects to the physical Cardputer over Bluetooth Low Energy (`Cardputer-NowPlaying`). Zero Wi-Fi setup, zero hotspot battery drain, and zero campus network restrictions.
- **Dual Player Support**: Seamlessly detects and controls both **Apple Music** and **Spotify** on macOS via AppleScript / JXA (`osascript`).
- **Binary RGB565 Artwork Streaming**:
  - In-memory downscaling to 72×72 pixels using Pillow (with native macOS `sips` disk fallback).
  - Directly generates 16-bit big-endian RGB565 buffers (10,368 bytes).
  - Streams artwork over BLE in MTU-optimized chunked packets with 6-byte sequence headers.
- **RFC 6455 WebSocket & HTTP REST**:
  - Live WebSocket push at `/ws` for sub-10ms latency updates.
  - Dedicated conflict-free HTTP port `58329` for REST polling and browser simulation.
- **Built-in Web Dashboard**: Visit `http://localhost:58329/` to inspect live metadata, preview downscaled artwork, and test controls.

---

## Quick Start

### 1. Bluetooth Low Energy (BLE) Mode (Recommended for Hardware)

For physical Cardputer hardware, run the bridge with `bleak` enabled. Using [`uv`](https://github.com/astral-sh/uv) requires no permanent pip installs:

```bash
uv run --with bleak python3 host-companion/bridge.py
```

Or with standard `pip`:
```bash
pip install bleak
python3 host-companion/bridge.py
```

The bridge automatically starts BLE scanning and connects as soon as your Cardputer powers on and advertises `Cardputer-NowPlaying`.

### 2. Standalone Wi-Fi / Simulator Mode (Zero Dependencies)

If running inside the Wokwi Simulator or without BLE, run with vanilla Python 3:

```bash
python3 host-companion/bridge.py
```

### 3. Transport Options

Use the `--transport` argument to control which interfaces are active:

| Flag | Behavior |
|---|---|
| `--transport auto` *(default)* | Runs BLE if `bleak` is installed, plus HTTP/WebSocket server. |
| `--transport ble` | Runs exclusively in BLE mode for physical Cardputer. |
| `--transport wifi` | Runs exclusively in HTTP/WebSocket mode (zero third-party dependencies). |
| `--transport both` | Forces both BLE and HTTP/WebSocket servers to launch. |

```bash
# Example: Custom port and BLE-only mode
uv run --with bleak python3 host-companion/bridge.py --transport ble
```

---

## BLE Protocol & GATT Architecture

When the physical Cardputer powers on, it starts BLE advertising as `Cardputer-NowPlaying`. The companion bridge acts as a BLE Central connecting to the Cardputer Peripheral.

### GATT Service UUIDs
- **Primary Service**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
- **Metadata Characteristic** (`WriteWithoutResponse` / `Write`): `beb5483e-36e1-4688-b7f5-ea07361b26a8`
  - Host pushes compact JSON payload whenever track state changes:
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
      "clock": "18:52",
      "player": "Music"
    }
    ```
- **Control Characteristic** (`Notify`): `beb5483f-36e1-4688-b7f5-ea07361b26a8`
  - Cardputer sends commands to host on keypress:
    - `toggle` — Play / Pause toggle
    - `next` — Skip to next track
    - `previous` — Previous track
    - `forward` — Fast-forward (+10s)
    - `backward` — Rewind (-10s)
    - `GET_ART:<artwork_id>` — Request binary artwork stream
- **Artwork Stream Characteristic** (`Write`): `beb54840-36e1-4688-b7f5-ea07361b26a8`
  - Host streams the 72×72 RGB565 buffer (10,368 bytes) in chunks up to 480 bytes.
  - **Packet Header (6 bytes)**:
    - `[0..1]`: `chunk_index` (uint16 big-endian)
    - `[2..3]`: `total_chunks` (uint16 big-endian)
    - `[4..5]`: `target_offset` (uint16 big-endian)
    - `[6..N]`: Raw RGB565 byte slice

---

## HTTP & WebSocket Endpoints (Wi-Fi / Simulator)

- `GET /api/now-playing` - Returns JSON track metadata and clock.
- `GET /artwork.raw` or `/artwork.rgb565` - Raw 72×72 16-bit big-endian RGB565 buffer (10,368 bytes).
- `GET /artwork.jpg` - Downscaled 72×72 JPEG thumbnail.
- `GET /ws` - RFC 6455 WebSocket endpoint pushing live metadata updates.
- `POST /api/toggle` - Toggle Play/Pause.
- `POST /api/next` - Skip to next track.
- `POST /api/previous` - Return to previous track.
- `POST /api/forward` - Seek +10s forward.
- `POST /api/backward` - Seek -10s backward.
- `GET /health` - Health check (`{"status":"ok"}`).
- `GET /` - Web preview & status dashboard.

### Connecting from Wokwi Simulator
In Wokwi's virtual Wi-Fi environment (`Wokwi-GUEST`), connect to your Mac via:
```text
http://host.wokwi.internal:58329/api/now-playing
http://host.wokwi.internal:58329/artwork.raw
```

