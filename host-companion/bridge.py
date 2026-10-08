#!/usr/bin/env python3
"""
Cardputer Now Playing - Unified macOS Companion Bridge
======================================================
Unified companion daemon for M5Stack Cardputer-Adv and Wokwi Simulator.
Supports:
  - Apple Music & Spotify playback detection and controls
  - In-memory thumbnail downscaling & RGB565 generation (Pillow + sips fallback)
  - RFC 6455 WebSocket push & HTTP REST endpoints (port 58329)
  - Bluetooth Low Energy (BLE) direct wireless connection for physical Cardputer-Adv
  - Transports: --transport [auto|both|wifi|ble]
"""

import http.server
import socketserver
import subprocess
import json
import hashlib
import struct
import base64
import socket
import threading
import os
import sys
import time
import tempfile
import argparse
import html
import atexit
import shutil
import unicodedata
import io
import asyncio
from typing import Optional

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

try:
    from bleak import BleakScanner, BleakClient
    HAS_BLEAK = True
except ImportError:
    BleakScanner, BleakClient = None, None
    HAS_BLEAK = False

# BLE UUIDs matching firmware/include/ble_manager.h
BLE_DEVICE_NAME        = "Cardputer-NowPlaying"
BLE_SERVICE_UUID       = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
BLE_CHAR_METADATA_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLE_CHAR_CONTROL_UUID  = "beb5483f-36e1-4688-b7f5-ea07361b26a8"
BLE_CHAR_ARTWORK_UUID  = "beb54840-36e1-4688-b7f5-ea07361b26a8"

ARTWORK_SIZE = 72
CHUNK_SIZE = 480  # fits safely inside 512 MTU
DEFAULT_PORT = 58329

CACHE_DIR = tempfile.mkdtemp(prefix="cardputer_companion_")
atexit.register(shutil.rmtree, CACHE_DIR, ignore_errors=True)


def clean_text(text: str) -> str:
    """Sanitize track metadata to clean ASCII for ST7789 embedded canvas."""
    if not text:
        return ""
    replacements = {
        '\u2018': "'", '\u2019': "'",
        '\u201c': '"', '\u201d': '"',
        '\u2013': '-', '\u2014': '-',
        '\u2026': '...', '\u00a0': ' '
    }
    cleaned = text
    for orig, rep in replacements.items():
        cleaned = cleaned.replace(orig, rep)
    
    # Normalize unicode to decomposed form then attempt ASCII conversion
    ascii_cand = unicodedata.normalize('NFKD', cleaned).encode('ascii', 'ignore').decode('ascii')
    import re
    ascii_cand = re.sub(r' +', ' ', ascii_cand).strip()
    
    # If ASCII filtering produced a non-empty string, use it; otherwise retain cleaned UTF-8
    if ascii_cand:
        return ascii_cand
    return re.sub(r' +', ' ', cleaned).strip()


def rgb_to_rgb565(raw_rgb: bytes, width: int, height: int) -> bytes:
    """Convert raw RGB24 bytes to big-endian RGB565 byte buffer."""
    out = bytearray(width * height * 2)
    for i in range(width * height):
        r = raw_rgb[i * 3]
        g = raw_rgb[i * 3 + 1]
        b = raw_rgb[i * 3 + 2]
        val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[i * 2] = (val >> 8) & 0xFF
        out[i * 2 + 1] = val & 0xFF
    return bytes(out)


def generate_default_rgb565(size: int = ARTWORK_SIZE) -> bytes:
    """Generate default placeholder art matching the embedded theme."""
    out = bytearray(size * size * 2)
    bg_val = 0x0842   # #0d0d0d in RGB565
    bar_val = 0xB5F6  # #afbdd9 Primary
    note_val = 0xE71C # #e6e6e6 Text

    for y in range(size):
        for x in range(size):
            idx = (y * size + x) * 2
            val = bg_val
            # Simple centered music icon note
            cx, cy = size // 2, size // 2
            if (cx - 6 <= x <= cx - 4 and cy - 10 <= y <= cy + 6) or \
               (cx + 4 <= x <= cx + 6 and cy - 13 <= y <= cy + 3) or \
               (cx - 6 <= x <= cx + 6 and cy - 13 <= y <= cy - 10):
                val = bar_val
            elif ((x - (cx - 7)) ** 2 + (y - (cy + 5)) ** 2 <= 9) or \
                 ((x - (cx + 3)) ** 2 + (y - (cy + 2)) ** 2 <= 9):
                val = note_val
            out[idx] = (val >> 8) & 0xFF
            out[idx + 1] = val & 0xFF
    return bytes(out)


class MusicController:
    """Unified player controller supporting Apple Music and Spotify."""

    def __init__(self, art_size: int = ARTWORK_SIZE):
        self.art_size = art_size
        self._lock = threading.Lock()
        self.last_track_key = None
        self.cached_rgb565 = generate_default_rgb565(art_size)
        self.cached_jpeg = b""
        self.cached_art_id = "default"
        self.last_query_time = 0
        self.cached_meta = None
        self.active_player = "Music"  # "Music" or "Spotify"

    def query(self) -> dict:
        with self._lock:
            now_ts = time.time()
            # Throttled query: max once per 250ms
            if now_ts - self.last_query_time < 0.25 and self.cached_meta:
                return dict(self.cached_meta)

            tz_offset = -time.timezone if (time.daylight == 0) else -time.altzone
            clock_str = time.strftime("%H:%M")

            jxa_script = '''
            (function() {
                var musicRunning = false;
                var spotifyRunning = false;
                try { musicRunning = Application("Music").running(); } catch(e) {}
                try { spotifyRunning = Application("Spotify").running(); } catch(e) {}

                if (musicRunning) {
                    var music = Application("Music");
                    var mState = music.playerState();
                    if (mState === "playing" || (!spotifyRunning && mState !== "stopped")) {
                        var mTrack = music.currentTrack;
                        return JSON.stringify({
                            player: "Music",
                            running: true,
                            state: mState,
                            title: mTrack.name() || "",
                            artist: mTrack.artist() || "",
                            album: mTrack.album() || "",
                            duration: Math.round(mTrack.duration() || 0),
                            elapsed: Math.round(music.playerPosition() || 0),
                            has_art: mTrack.artworks.length > 0
                        });
                    }
                }

                if (spotifyRunning) {
                    var spotify = Application("Spotify");
                    var sState = spotify.playerState();
                    if (sState !== "stopped") {
                        var sTrack = spotify.currentTrack;
                        return JSON.stringify({
                            player: "Spotify",
                            running: true,
                            state: sState,
                            title: sTrack.name() || "",
                            artist: sTrack.artist() || "",
                            album: sTrack.album() || "",
                            duration: Math.round((sTrack.duration() || 0) / 1000),
                            elapsed: Math.round(spotify.playerPosition() || 0),
                            has_art: (sTrack.artworkUrl() || "").length > 0,
                            artwork_url: sTrack.artworkUrl() || ""
                        });
                    }
                }

                if (musicRunning) {
                    return JSON.stringify({ player: "Music", running: true, state: "stopped" });
                }
                if (spotifyRunning) {
                    return JSON.stringify({ player: "Spotify", running: true, state: "stopped" });
                }
                return JSON.stringify({ player: "none", running: false, state: "stopped" });
            })();
            '''

            try:
                res = subprocess.run(
                    ['osascript', '-l', 'JavaScript', '-e', jxa_script],
                    capture_output=True,
                    text=True,
                    timeout=1.5
                )
                out = res.stdout.strip()
                data = json.loads(out) if out else {"running": False, "state": "stopped"}
            except Exception:
                data = {"running": False, "state": "stopped"}

            self.last_query_time = now_ts
            self.active_player = data.get("player", "Music")
            running = data.get("running", False)
            state = data.get("state", "stopped")

            if not running or state == "stopped":
                meta = {
                    "running": running,
                    "state": "stopped",
                    "title": "",
                    "artist": "",
                    "album": "",
                    "duration": 0,
                    "elapsed": 0,
                    "artwork_id": "none",
                    "clock": clock_str,
                    "epoch": int(now_ts),
                    "tz_offset": tz_offset,
                    "player": self.active_player
                }
                self.cached_meta = meta
                return meta

            title = clean_text(data.get("title", ""))
            artist = clean_text(data.get("artist", ""))
            album = clean_text(data.get("album", ""))
            duration = data.get("duration", 0)
            elapsed = data.get("elapsed", 0)
            has_art = data.get("has_art", False)
            art_url = data.get("artwork_url", "")

            track_key = f"{self.active_player}_{title}_{artist}_{album}"
            if track_key != self.last_track_key:
                self.last_track_key = track_key
                if has_art:
                    self._extract_artwork(track_key, art_url)
                else:
                    self.cached_art_id = "default"
                    self.cached_rgb565 = generate_default_rgb565(self.art_size)
                    self.cached_jpeg = b""

            meta = {
                "running": True,
                "state": state.lower(),
                "title": title,
                "artist": artist,
                "album": album,
                "duration": duration,
                "elapsed": elapsed,
                "artwork_id": self.cached_art_id,
                "clock": clock_str,
                "epoch": int(now_ts),
                "tz_offset": tz_offset,
                "player": self.active_player
            }
            self.cached_meta = meta
            return meta

    def execute_command(self, cmd: str) -> bool:
        """Execute playback action on the currently active media player."""
        player = self.active_player if self.active_player in ["Music", "Spotify"] else "Music"
        actions = {
            "toggle": f'tell application "{player}" to playpause',
            "play": f'tell application "{player}" to play',
            "pause": f'tell application "{player}" to pause',
            "next": f'tell application "{player}" to next track',
            "prev": f'tell application "{player}" to previous track',
            "previous": f'tell application "{player}" to previous track',
            "ff": f'tell application "{player}" to set player position to (player position + 10)',
            "rw": f'tell application "{player}" to set player position to (player position - 10)',
            "forward": f'tell application "{player}" to set player position to (player position + 10)',
            "backward": f'tell application "{player}" to set player position to (player position - 10)'
        }
        if cmd not in actions:
            return False

        try:
            subprocess.run(["osascript", "-e", actions[cmd]], timeout=1.0)
            self.last_query_time = 0  # Invalidate cached metadata
            return True
        except Exception as e:
            print(f"[Control] Failed to execute {cmd} on {player}: {e}", file=sys.stderr)
            return False

    def _extract_artwork(self, track_key: str, art_url: str = ""):
        """Extract artwork in-memory via Pillow when possible, falling back to sips."""
        raw_art_path = os.path.join(CACHE_DIR, "raw_art.tmp")

        if self.active_player == "Spotify" and art_url.startswith("http"):
            try:
                import urllib.request
                with urllib.request.urlopen(art_url, timeout=3.0) as resp:
                    raw_data = resp.read()
                if self._process_image_bytes(raw_data, track_key):
                    return
            except Exception as e:
                print(f"[Artwork] Spotify URL fetch failed: {e}", file=sys.stderr)

        # Apple Music extraction via osascript
        applescript = f'''
        set filePath to "{raw_art_path}"
        tell application "Music"
            if (count of artworks of current track) > 0 then
                set rawData to raw data of artwork 1 of current track
                set fp to open for access (POSIX file filePath) with write permission
                set eof fp to 0
                write rawData to fp
                close access fp
                return "OK"
            else
                return "NO_ART"
            end if
        end tell
        '''
        try:
            res = subprocess.run(['osascript', '-e', applescript], capture_output=True, text=True, timeout=2.5)
            if "OK" in res.stdout and os.path.exists(raw_art_path):
                with open(raw_art_path, 'rb') as f:
                    raw_data = f.read()
                if self._process_image_bytes(raw_data, track_key):
                    return
        except Exception as e:
            print(f"[Artwork] AppleScript extraction failed: {e}", file=sys.stderr)

        # Fallback to default placeholder
        self.cached_art_id = "default"
        self.cached_rgb565 = generate_default_rgb565(self.art_size)
        self.cached_jpeg = b""

    def _process_image_bytes(self, raw_data: bytes, track_key: str) -> bool:
        """Process image in-memory using Pillow or fallback to sips on disk."""
        if HAS_PIL:
            try:
                img = Image.open(io.BytesIO(raw_data)).convert('RGB')
                img = img.resize((self.art_size, self.art_size), Image.Resampling.LANCZOS)
                raw_rgb = img.tobytes()
                self.cached_rgb565 = rgb_to_rgb565(raw_rgb, self.art_size, self.art_size)

                bio = io.BytesIO()
                img.save(bio, format="JPEG", quality=85)
                self.cached_jpeg = bio.getvalue()

                self.cached_art_id = hashlib.md5(f"{track_key}_{self.cached_rgb565[:64]}".encode()).hexdigest()[:12]
                return True
            except Exception as e:
                print(f"[Artwork] Pillow in-memory resize failed: {e}", file=sys.stderr)

        # Fallback to sips
        try:
            tmp_in = os.path.join(CACHE_DIR, "tmp_in.dat")
            tmp_bmp = os.path.join(CACHE_DIR, "tmp_out.bmp")
            tmp_jpg = os.path.join(CACHE_DIR, "tmp_out.jpg")
            with open(tmp_in, 'wb') as f:
                f.write(raw_data)

            subprocess.run([
                'sips', '-s', 'format', 'bmp',
                '-z', str(self.art_size), str(self.art_size),
                tmp_in, '--out', tmp_bmp
            ], capture_output=True, timeout=2.5)

            subprocess.run([
                'sips', '-s', 'format', 'jpeg',
                '-z', str(self.art_size), str(self.art_size),
                tmp_in, '--out', tmp_jpg
            ], capture_output=True, timeout=2.5)

            if os.path.exists(tmp_bmp):
                with open(tmp_bmp, 'rb') as f:
                    bmp_data = f.read()
                self.cached_rgb565 = self._parse_bmp(bmp_data)
                self.cached_art_id = hashlib.md5(f"{track_key}_{self.cached_rgb565[:64]}".encode()).hexdigest()[:12]

            if os.path.exists(tmp_jpg):
                with open(tmp_jpg, 'rb') as f:
                    self.cached_jpeg = f.read()

            return len(self.cached_rgb565) == (self.art_size * self.art_size * 2)
        except Exception as e:
            print(f"[Artwork] sips fallback failed: {e}", file=sys.stderr)
            return False

    def _parse_bmp(self, data: bytes) -> bytes:
        """Parse standard 24bpp BMP into RGB565 handling both bottom-up and top-down DIBs."""
        try:
            offset = struct.unpack('<I', data[10:14])[0]
            width = abs(struct.unpack('<i', data[18:22])[0])
            raw_height = struct.unpack('<i', data[22:26])[0]
            is_bottom_up = raw_height > 0
            height = abs(raw_height)
            bpp = struct.unpack('<H', data[28:30])[0]

            row_size = ((bpp * width + 31) // 32) * 4
            bytes_per_pixel = bpp // 8
            out = bytearray(width * height * 2)

            for y in range(height):
                bmp_y = (height - 1 - y) if is_bottom_up else y
                row_offset = offset + bmp_y * row_size
                for x in range(width):
                    px = row_offset + x * bytes_per_pixel
                    b, g, r = data[px], data[px + 1], data[px + 2]
                    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    idx = (y * width + x) * 2
                    out[idx] = (val >> 8) & 0xFF
                    out[idx + 1] = val & 0xFF
            return bytes(out)
        except Exception:
            return generate_default_rgb565(self.art_size)


# =============================================================================
# WebSocket & HTTP Server for Local Wi-Fi / Wokwi Simulator
# =============================================================================

def make_ws_frame(data: bytes, opcode: int = 0x1) -> bytes:
    """Create an unmasked RFC 6455 WebSocket frame from server to client."""
    length = len(data)
    header = bytearray([0x80 | (opcode & 0x0F)])
    if length <= 125:
        header.append(length)
    elif length <= 65535:
        header.append(126)
        header.extend(struct.pack('!H', length))
    else:
        header.append(127)
        header.extend(struct.pack('!Q', length))
    return bytes(header) + data


def read_ws_frame(sock: socket.socket) -> tuple:
    """Read and decode an RFC 6455 WebSocket frame."""
    try:
        header = sock.recv(2)
        if len(header) < 2:
            return 0x8, b""
        byte1, byte2 = header[0], header[1]
        opcode = byte1 & 0x0F
        masked = (byte2 & 0x80) != 0
        payload_len = byte2 & 0x7F

        if payload_len == 126:
            ext = sock.recv(2)
            if len(ext) < 2: return 0x8, b""
            payload_len = struct.unpack('!H', ext)[0]
        elif payload_len == 127:
            ext = sock.recv(8)
            if len(ext) < 8: return 0x8, b""
            payload_len = struct.unpack('!Q', ext)[0]

        if masked:
            mask = sock.recv(4)
            if len(mask) < 4: return 0x8, b""
            payload = bytearray()
            while len(payload) < payload_len:
                chunk = sock.recv(payload_len - len(payload))
                if not chunk: return 0x8, b""
                payload.extend(chunk)
            unmasked = bytearray(payload_len)
            for i in range(payload_len):
                unmasked[i] = payload[i] ^ mask[i % 4]
            return opcode, bytes(unmasked)
        else:
            payload = bytearray()
            while len(payload) < payload_len:
                chunk = sock.recv(payload_len - len(payload))
                if not chunk: return 0x8, b""
                payload.extend(chunk)
            return opcode, bytes(payload)
    except (socket.error, OSError):
        return 0x8, b""


class CompanionBridge:
    def __init__(self, controller: MusicController):
        self.controller = controller
        self.ws_clients = set()
        self.ws_lock = threading.Lock()
        self.last_broadcast_state = {}
        self.running = True

        self.poller_thread = threading.Thread(target=self._ws_poller, daemon=True)
        self.poller_thread.start()

    def add_ws_client(self, client_sock):
        with self.ws_lock:
            self.ws_clients.add(client_sock)
        # Immediate sync on connection
        meta = self.controller.query()
        frame = make_ws_frame(json.dumps(meta).encode('utf-8'))
        try:
            client_sock.sendall(frame)
        except (socket.error, OSError):
            self.remove_ws_client(client_sock)

    def remove_ws_client(self, client_sock):
        with self.ws_lock:
            self.ws_clients.discard(client_sock)
        try:
            client_sock.close()
        except Exception:
            pass

    def broadcast_metadata(self, meta):
        frame = make_ws_frame(json.dumps(meta).encode('utf-8'))
        with self.ws_lock:
            dead_clients = []
            for client_sock in list(self.ws_clients):
                try:
                    client_sock.sendall(frame)
                except (socket.error, OSError):
                    dead_clients.append(client_sock)
            for dead in dead_clients:
                self.ws_clients.discard(dead)
                try: dead.close()
                except Exception: pass

    def _ws_poller(self):
        """Poll metadata every 500ms and broadcast on change."""
        while self.running:
            try:
                meta = self.controller.query()
                # Check if significant fields changed
                sig = (meta.get("state"), meta.get("title"), meta.get("artist"),
                       meta.get("elapsed"), meta.get("artwork_id"))
                if sig != self.last_broadcast_state:
                    self.last_broadcast_state = sig
                    self.broadcast_metadata(meta)
            except Exception as e:
                print(f"[WS Poller Error] {e}", file=sys.stderr)
            time.sleep(0.5)


INDEX_HTML = """<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Cardputer Now Playing</title>
  <style>
    :root {
      --font-body: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      --background: #050505;
      --card: #0d0d0d;
      --border: #252525;
      --text: #e6e6e6;
      --primary: #afbdd9;
      --muted: #808080;
      --accent: #f0a133;
    }
    body {
      background: var(--background);
      color: var(--text);
      font-family: var(--font-body);
      margin: 0;
      padding: 30px 20px;
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    .container {
      max-width: 440px;
      width: 100%;
    }
    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 20px;
      padding-bottom: 12px;
      border-bottom: 1px solid var(--border);
    }
    .title {
      font-size: 1.1rem;
      font-weight: 600;
      color: var(--primary);
    }
    .badge {
      font-size: 0.75rem;
      padding: 4px 8px;
      border-radius: 4px;
      background: #151515;
      color: var(--accent);
      border: 1px solid var(--border);
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 16px;
      display: flex;
      gap: 16px;
      margin-bottom: 20px;
    }
    .art {
      width: 80px;
      height: 80px;
      border-radius: 4px;
      border: 1px solid var(--border);
      background: #000;
      object-fit: cover;
    }
    .info {
      flex: 1;
      display: flex;
      flex-direction: column;
      justify-content: center;
      overflow: hidden;
    }
    .song-title {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--text);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .artist {
      font-size: 0.82rem;
      color: var(--muted);
      margin-top: 4px;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .controls {
      display: flex;
      justify-content: center;
      gap: 10px;
      margin-bottom: 20px;
    }
    button {
      background: #181818;
      border: 1px solid var(--border);
      color: var(--text);
      padding: 8px 16px;
      border-radius: 6px;
      font-size: 0.85rem;
      cursor: pointer;
    }
    button:hover {
      background: #252525;
      color: var(--primary);
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="title">Cardputer Now Playing</div>
      <div class="badge" id="player-badge">Active</div>
    </div>
    <div class="card">
      <img id="art-img" class="art" src="/artwork.jpg" alt="Art">
      <div class="info">
        <div class="song-title" id="track-title">Loading...</div>
        <div class="artist" id="track-artist">Connecting to bridge...</div>
      </div>
    </div>
    <div class="controls">
      <button onclick="control('previous')">&#9664;&#9664; Prev</button>
      <button onclick="control('backward')">-10s</button>
      <button onclick="control('toggle')">&#9654;&#10074;&#10074; Toggle</button>
      <button onclick="control('forward')">+10s</button>
      <button onclick="control('next')">Next &#9654;&#9654;</button>
    </div>
  </div>
  <script>
    function updateUI(data) {
      document.getElementById('track-title').textContent = data.title || (data.running ? "Not Playing" : "Player Closed");
      document.getElementById('track-artist').textContent = data.artist || (data.player || "Music");
      document.getElementById('player-badge').textContent = (data.player || "Music") + " (" + (data.state || "stopped") + ")";
      if (data.artwork_id && data.artwork_id !== "none") {
        document.getElementById('art-img').src = "/artwork.jpg?t=" + data.artwork_id;
      }
    }

    function control(action) {
      fetch('/api/' + action, { method: 'POST' }).then(() => poll());
    }

    function poll() {
      fetch('/api/now-playing')
        .then(r => r.json())
        .then(updateUI)
        .catch(console.error);
    }

    setInterval(poll, 1500);
    poll();
  </script>
</body>
</html>
"""


def create_request_handler(bridge_instance: CompanionBridge):
    class RequestHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass  # Suppress HTTP access noise

        def do_GET(self):
            parsed_path = self.path.split('?')[0]

            if parsed_path == "/ws":
                upgrade = self.headers.get("Upgrade", "").lower()
                if upgrade == "websocket":
                    key = self.headers.get("Sec-WebSocket-Key", "")
                    guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
                    accept = base64.b64encode(hashlib.sha1((key + guid).encode('utf-8')).digest()).decode('utf-8')

                    self.send_response(101)
                    self.send_header("Upgrade", "websocket")
                    self.send_header("Connection", "Upgrade")
                    self.send_header("Sec-WebSocket-Accept", accept)
                    self.end_headers()

                    client_sock = self.connection
                    bridge_instance.add_ws_client(client_sock)

                    while True:
                        opcode, payload = read_ws_frame(client_sock)
                        if opcode == 0x8 or not client_sock:
                            break
                        elif opcode == 0x9:  # Ping
                            pong = make_ws_frame(payload, opcode=0xA)
                            try: client_sock.sendall(pong)
                            except Exception: break
                        elif opcode == 0x1:  # Text Frame (Control commands from Cardputer)
                            try:
                                text_msg = payload.decode('utf-8', errors='ignore').strip()
                                action = None
                                if text_msg.startswith("{"):
                                    try:
                                        msg_data = json.loads(text_msg)
                                        action = msg_data.get("action")
                                    except Exception:
                                        pass
                                else:
                                    action = text_msg

                                if action:
                                    print(f"[WS Control] Received action: {action}")
                                    bridge_instance.controller.execute_command(action)
                                    # Immediate broadcast of updated state
                                    new_meta = bridge_instance.controller.query()
                                    bridge_instance.broadcast_metadata(new_meta)
                            except Exception as e:
                                print(f"[WS Control Error] {e}", file=sys.stderr)

                    bridge_instance.remove_ws_client(client_sock)
                    return

            if parsed_path in ["/api/now-playing", "/api/metadata"]:
                meta = bridge_instance.controller.query()
                data = json.dumps(meta).encode('utf-8')
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            elif parsed_path in ["/artwork.raw", "/artwork.rgb565"]:
                raw = bridge_instance.controller.cached_rgb565
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            elif parsed_path == "/artwork.jpg":
                jpg = bridge_instance.controller.cached_jpeg
                if not jpg:
                    # Serve placeholder image
                    jpg = b""
                self.send_response(200)
                self.send_header("Content-Type", "image/jpeg")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(jpg)))
                self.end_headers()
                self.wfile.write(jpg)

            elif parsed_path == "/health":
                res = b'{"status":"ok"}'
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(res)))
                self.end_headers()
                self.wfile.write(res)

            elif parsed_path == "/":
                data = INDEX_HTML.encode('utf-8')
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            elif parsed_path.startswith("/api/"):
                # Disallow mutating actions over GET
                self.send_response(405)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(b"405 Method Not Allowed - Use POST")

            else:
                self.send_response(404)
                self.end_headers()

        def do_POST(self):
            parsed_path = self.path.split('?')[0]
            action = parsed_path.replace("/api/", "").strip("/")
            if bridge_instance.controller.execute_command(action):
                resp = json.dumps({"status": "ok", "action": action}).encode('utf-8')
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Content-Length", str(len(resp)))
                self.end_headers()
                self.wfile.write(resp)
            else:
                resp = json.dumps({"status": "error", "message": f"Unknown action: {action}"}).encode('utf-8')
                self.send_response(400)
                self.send_header("Content-Type", "application/json")
                self.end_headers()
                self.wfile.write(resp)

    return RequestHandler


class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


# =============================================================================
# BLE Companion Task (Bleak Asyncio Worker)
# =============================================================================

class BleCompanionTask:
    def __init__(self, controller: MusicController):
        self.controller = controller
        self.client: Optional[BleakClient] = None
        self.last_sent_meta = {}
        self._art_lock = asyncio.Lock()
        self._current_art_task: Optional[asyncio.Task] = None
        self._current_streaming_id: Optional[str] = None
        self._is_streaming_art = False

    async def run(self):
        if not HAS_BLEAK:
            return

        print(f"[BLE] Scanning for '{BLE_DEVICE_NAME}'...")
        while True:
            try:
                device = await BleakScanner.find_device_by_filter(
                    lambda d, ad: d.name and BLE_DEVICE_NAME.lower() in d.name.lower(),
                    timeout=4.0
                )

                if not device:
                    await asyncio.sleep(2.0)
                    continue

                print(f"[BLE] Found Cardputer! ({device.address}). Connecting...")
                async with BleakClient(device) as client:
                    self.client = client
                    self._is_streaming_art = False
                    self._current_streaming_id = None
                    self.last_sent_meta = {}
                    print("[BLE] Connected successfully to Cardputer-NowPlaying!")

                    # Subscribe to Cardputer keyboard controls
                    await client.start_notify(BLE_CHAR_CONTROL_UUID, self._on_control_received)

                    while client.is_connected:
                        # Pause metadata push while actively streaming artwork chunks to give 100% bandwidth
                        if not self._is_streaming_art:
                            meta = self.controller.query()
                            if self._should_send_meta(meta):
                                payload = json.dumps(meta).encode('utf-8')
                                try:
                                    await client.write_gatt_char(BLE_CHAR_METADATA_UUID, payload, response=False)
                                    self.last_sent_meta = meta
                                except Exception as e:
                                    print(f"[BLE] Send metadata error: {e}")
                                    break
                        await asyncio.sleep(0.5)

            except Exception as e:
                print(f"[BLE] Connection lost or waiting: {e}")
                self._is_streaming_art = False
                await asyncio.sleep(3.0)

    def _should_send_meta(self, meta: dict) -> bool:
        if not self.last_sent_meta:
            return True
        # Always send immediately if track identity, player, or state changed
        for key in ["title", "artist", "album", "state", "artwork_id", "running", "player"]:
            if meta.get(key) != self.last_sent_meta.get(key):
                return True
        # If playing, send every 5 seconds to sync clock/progress without saturating BLE
        last_epoch = self.last_sent_meta.get("epoch", 0)
        curr_epoch = meta.get("epoch", 0)
        if curr_epoch - last_epoch >= 5:
            return True
        # Or if elapsed drifted significantly (> 2s jump from seek/scrub)
        last_elapsed = self.last_sent_meta.get("elapsed", 0)
        curr_elapsed = meta.get("elapsed", 0)
        expected_elapsed = last_elapsed + (curr_epoch - last_epoch)
        if abs(curr_elapsed - expected_elapsed) >= 2:
            return True
        return False

    def _on_control_received(self, sender, data: bytearray):
        msg = data.decode('utf-8', errors='ignore').strip()
        if not msg:
            return

        if msg.startswith("GET_ART:"):
            art_id = msg.split(":", 1)[1]
            if self._current_streaming_id == art_id and self._is_streaming_art:
                print(f"[BLE] Artwork stream for '{art_id}' already in progress. Ignoring duplicate request.")
                return

            # Cancel previous in-flight artwork stream if running
            if self._current_art_task and not self._current_art_task.done():
                self._current_art_task.cancel()

            print(f"[BLE] Cardputer requested artwork ({art_id}). Starting stream...")
            self._current_streaming_id = art_id
            self._current_art_task = asyncio.create_task(self._stream_artwork(art_id))
        else:
            self.controller.execute_command(msg)

    async def _stream_artwork(self, art_id: str):
        if not self.client or not self.client.is_connected:
            return

        async with self._art_lock:
            self._is_streaming_art = True
            try:
                data = self.controller.cached_rgb565
                if not data:
                    print(f"[BLE] No artwork data available for {art_id}.")
                    return

                total_len = len(data)
                mtu = getattr(self.client, "mtu_size", 512)
                # Ensure packet fits safely inside MTU: 6 bytes header + 3 bytes ATT overhead
                safe_chunk_size = max(64, min(480, mtu - 9))

                chunks = []
                offset = 0
                while offset < total_len:
                    chunks.append((offset, data[offset:offset + safe_chunk_size]))
                    offset += safe_chunk_size

                total_chunks = len(chunks)
                print(f"[BLE] Streaming {total_len} bytes in {total_chunks} chunks (chunk_size={safe_chunk_size})...")

                for idx, (chunk_offset, chunk_bytes) in enumerate(chunks):
                    if not self.client or not self.client.is_connected:
                        print("[BLE] Disconnected during artwork stream.")
                        return

                    header = bytearray([
                        (idx >> 8) & 0xFF, idx & 0xFF,
                        (total_chunks >> 8) & 0xFF, total_chunks & 0xFF,
                        (chunk_offset >> 8) & 0xFF, chunk_offset & 0xFF
                    ])
                    packet = header + chunk_bytes

                    try:
                        await self.client.write_gatt_char(BLE_CHAR_ARTWORK_UUID, packet, response=True)
                    except asyncio.CancelledError:
                        print(f"[BLE] Artwork stream for {art_id} cancelled.")
                        return
                    except Exception as e:
                        print(f"[BLE] Artwork stream error on chunk {idx}/{total_chunks}: {e}")
                        return

                print(f"[BLE] Artwork stream complete ({total_len} bytes in {total_chunks} chunks).")
            finally:
                self._is_streaming_art = False
                self._current_streaming_id = None


# =============================================================================
# CLI Entry Point
# =============================================================================

def main(args_list=None):
    parser = argparse.ArgumentParser(description="Cardputer Now Playing - macOS Companion Bridge")
    parser.add_argument("--transport", choices=["auto", "wifi", "ble", "both"], default="auto",
                        help="Transport mode (auto: runs both if bleak available, else Wi-Fi)")
    parser.add_argument("--host", default="0.0.0.0", help="HTTP host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="HTTP port (default: 58329)")
    args = parser.parse_args(args_list)

    controller = MusicController(art_size=ARTWORK_SIZE)
    bridge = CompanionBridge(controller)

    start_wifi = args.transport in ["auto", "wifi", "both"]
    start_ble = args.transport in ["auto", "ble", "both"]

    print("==================================================")
    print(" Cardputer Now Playing - macOS Companion Bridge")
    print("==================================================")
    print(f" * Media Player:   Apple Music + Spotify")
    print(f" * Image Pipeline: {'Pillow (In-Memory)' if HAS_PIL else 'macOS sips (Disk Fallback)'}")

    server = None
    if start_wifi:
        handler = create_request_handler(bridge)
        try:
            server = ThreadedTCPServer((args.host, args.port), handler)
            print(f" * HTTP/WS Bridge: http://{args.host}:{args.port}")
            print(f" * Wokwi Endpoint: http://host.wokwi.internal:{args.port}")
        except Exception as e:
            print(f"[Error] Failed to bind HTTP server to {args.host}:{args.port}: {e}")
            if not start_ble:
                sys.exit(1)

    if start_ble:
        if HAS_BLEAK:
            print(f" * BLE Wireless:   Enabled (Device: '{BLE_DEVICE_NAME}')")
        else:
            print(f" * BLE Wireless:   Disabled ('bleak' library not installed)")
            print("   Tip: Run with: uv run --with bleak python3 host-companion/bridge.py")
            if args.transport == "ble":
                sys.exit(1)
            start_ble = False

    print("==================================================\n")

    if server:
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

    if start_ble and HAS_BLEAK:
        ble_task = BleCompanionTask(controller)
        try:
            asyncio.run(ble_task.run())
        except KeyboardInterrupt:
            print("\n[Bridge] Shutting down...")
    elif server:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n[Bridge] Shutting down...")


if __name__ == "__main__":
    main()
