#!/usr/bin/env python3
"""
Cardputer Now Playing - macOS Companion Bridge
Query Music.app via native AppleScript/JXA, downscale cover art to 72x72 RGB565/JPEG,
and serve HTTP endpoints for ESP32 / Wokwi simulator.

Endpoints:
  GET /api/now-playing : Current track metadata & playback state (JSON)
  GET /artwork.raw     : 90x90 16-bit RGB565 binary artwork (16,200 bytes)
  GET /artwork.rgb565  : Alias for /artwork.raw
  GET /artwork.jpg     : 90x90 downscaled JPEG
  GET /health          : Status check
  GET /                : Minimalist web preview & diagnostics dashboard
"""

import http.server
import socketserver
import subprocess
import json
import hashlib
import struct
import os
import sys
import time
import tempfile
import argparse

ARTWORK_SIZE = 72  # 72x72 pixels thumbnail
CACHE_DIR = tempfile.mkdtemp(prefix="cardputer_music_")

class MusicBridge:
    def __init__(self, art_size=ARTWORK_SIZE):
        self.art_size = art_size
        self.last_track_key = None
        now_ts = time.time()
        tz_offset = -time.timezone if (time.daylight == 0) else -time.altzone
        self.last_metadata = {
            "running": False,
            "state": "stopped",
            "title": "",
            "artist": "",
            "album": "",
            "duration": 0,
            "elapsed": 0,
            "artwork_id": "none",
            "clock": time.strftime("%H:%M"),
            "epoch": int(now_ts),
            "tz_offset": tz_offset
        }
        self.raw_rgb565 = self._generate_default_rgb565()
        self.last_query_time = 0
        self.cached_query = None

    def control_playback(self, action):
        """Send playback commands to Music.app via AppleScript."""
        cmd_map = {
            "play": 'tell application "Music" to play',
            "pause": 'tell application "Music" to pause',
            "toggle": 'tell application "Music" to playpause',
            "playpause": 'tell application "Music" to playpause',
            "next": 'tell application "Music" to next track',
            "previous": 'tell application "Music" to previous track',
            "forward": 'tell application "Music" to set player position to ((player position) + 10)',
            "backward": 'tell application "Music" to set player position to ((player position) - 10)',
            "seek_forward": 'tell application "Music" to set player position to ((player position) + 10)',
            "seek_backward": 'tell application "Music" to set player position to ((player position) - 10)',
        }
        script = cmd_map.get(action.lower())
        if not script:
            return False, "Unknown action"
        try:
            subprocess.run(["osascript", "-e", script], check=True, timeout=2)
            self.cached_query = None
            return True, "Success"
        except Exception as e:
            return False, str(e)

    def _generate_default_rgb565(self):
        """Generate a 90x90 black square with a minimalist crisp white music note."""
        buf = bytearray(self.art_size * self.art_size * 2)
        # All black 0x0000
        # Draw a small 16x16 minimalist musical note in the center
        cx = self.art_size // 2
        cy = self.art_size // 2
        white_hi = 0xFF
        white_lo = 0xFF

        def set_pixel(x, y):
            if 0 <= x < self.art_size and 0 <= y < self.art_size:
                idx = (y * self.art_size + x) * 2
                buf[idx] = white_hi
                buf[idx + 1] = white_lo

        # Note stem 1 (left)
        for y in range(cy - 12, cy + 8):
            set_pixel(cx - 6, y)
            set_pixel(cx - 5, y)
        # Note stem 2 (right)
        for y in range(cy - 16, cy + 4):
            set_pixel(cx + 6, y)
            set_pixel(cx + 7, y)
        # Top connecting beam
        for x in range(cx - 6, cx + 8):
            # angled beam
            y_beam = cy - 12 - int((x - (cx - 6)) * 0.3)
            set_pixel(x, y_beam)
            set_pixel(x, y_beam - 1)
            set_pixel(x, y_beam - 2)
        # Note heads (filled circles)
        for dx in range(-4, 3):
            for dy in range(-3, 4):
                if dx*dx + dy*dy <= 9:
                    set_pixel(cx - 7 + dx, cy + 7 + dy)
                    set_pixel(cx + 5 + dx, cy + 3 + dy)

        return bytes(buf)

    def query_music(self):
        """Run JXA script to get current track metadata from Music.app."""
        now = time.time()
        # Rate limit sub-process calls to at most once per 200ms
        if now - self.last_query_time < 0.2 and self.cached_query:
            return self.cached_query

        jxa_script = '''
        var music = Application("Music");
        if (!music.running()) {
            JSON.stringify({running: false, state: "stopped"});
        } else {
            var state = music.playerState();
            if (state === "stopped") {
                JSON.stringify({running: true, state: "stopped"});
            } else {
                var track = music.currentTrack;
                var info = {
                    running: true,
                    state: state,
                    title: track.name() || "",
                    artist: track.artist() || "",
                    album: track.album() || "",
                    duration: Math.round(track.duration() || 0),
                    elapsed: Math.round(music.playerPosition() || 0),
                    has_artwork: track.artworks.length > 0
                };
                JSON.stringify(info);
            }
        }
        '''
        try:
            res = subprocess.run(
                ['osascript', '-l', 'JavaScript', '-e', jxa_script],
                capture_output=True,
                text=True,
                timeout=2.0
            )
            out = res.stdout.strip()
            if out:
                data = json.loads(out)
            else:
                data = {"running": False, "state": "stopped"}
        except Exception as e:
            data = {"running": False, "state": "error", "error": str(e)}

        self.last_query_time = now
        self.cached_query = data
        self._update_state(data)
        return self.last_metadata

    def _update_state(self, data):
        state = data.get("state", "stopped")
        running = data.get("running", False)

        now_ts = time.time()
        tz_offset = -time.timezone if (time.daylight == 0) else -time.altzone

        if not running or state == "stopped":
            self.last_metadata = {
                "running": running,
                "state": "stopped",
                "title": "",
                "artist": "",
                "album": "",
                "duration": 0,
                "elapsed": 0,
                "artwork_id": "none",
                "clock": time.strftime("%H:%M"),
                "epoch": int(now_ts),
                "tz_offset": tz_offset
            }
            return

        title = data.get("title", "")
        artist = data.get("artist", "")
        album = data.get("album", "")
        duration = data.get("duration", 0)
        elapsed = data.get("elapsed", 0)
        has_art = data.get("has_artwork", False)

        track_key = f"{title}_{artist}_{album}"
        if track_key != self.last_track_key:
            self.last_track_key = track_key
            if has_art:
                self._extract_and_convert_artwork(track_key)
            else:
                self.raw_rgb565 = self._generate_default_rgb565()
                self.jpeg_data = b""
                self.last_metadata["artwork_id"] = "default"

        artwork_id = hashlib.md5(f"{track_key}_{len(self.raw_rgb565)}".encode()).hexdigest()[:12]
        self.last_metadata = {
            "running": True,
            "state": state,
            "title": title,
            "artist": artist,
            "album": album,
            "duration": duration,
            "elapsed": elapsed,
            "artwork_id": artwork_id,
            "clock": time.strftime("%H:%M"),
            "epoch": int(now_ts),
            "tz_offset": tz_offset
        }

    def _extract_and_convert_artwork(self, track_key):
        """Extract raw artwork via AppleScript and downscale using macOS sips."""
        raw_art_path = os.path.join(CACHE_DIR, "raw_art.tmp")
        bmp_art_path = os.path.join(CACHE_DIR, f"art_{self.art_size}.bmp")
        jpg_art_path = os.path.join(CACHE_DIR, f"art_{self.art_size}.jpg")

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
            res = subprocess.run(['osascript', '-e', applescript], capture_output=True, text=True, timeout=3.0)
            if "OK" not in res.stdout:
                self.raw_rgb565 = self._generate_default_rgb565()
                return

            # Use sips to resize to square JPEG
            subprocess.run([
                'sips', '-z', str(self.art_size), str(self.art_size),
                raw_art_path, '--out', jpg_art_path
            ], capture_output=True, timeout=3.0)

            if os.path.exists(jpg_art_path):
                with open(jpg_art_path, 'rb') as f:
                    self.jpeg_data = f.read()

            # Use sips to create 24-bit BMP
            subprocess.run([
                'sips', '-s', 'format', 'bmp',
                '-z', str(self.art_size), str(self.art_size),
                raw_art_path, '--out', bmp_art_path
            ], capture_output=True, timeout=3.0)

            if os.path.exists(bmp_art_path):
                self.raw_rgb565 = self._bmp_to_rgb565(bmp_art_path)
            else:
                self.raw_rgb565 = self._generate_default_rgb565()

        except Exception as e:
            print(f"[Error extracting artwork] {e}", file=sys.stderr)
            self.raw_rgb565 = self._generate_default_rgb565()

    def _bmp_to_rgb565(self, bmp_path):
        """Convert standard BMP into 16-bit big-endian RGB565 byte buffer."""
        try:
            with open(bmp_path, 'rb') as f:
                data = f.read()

            offset = struct.unpack('<I', data[10:14])[0]
            width = struct.unpack('<i', data[18:22])[0]
            height = struct.unpack('<i', data[22:26])[0]
            bpp = struct.unpack('<H', data[28:30])[0]

            is_bottom_up = height > 0
            height = abs(height)
            width = abs(width)

            row_size = ((bpp * width + 31) // 32) * 4
            bytes_per_pixel = bpp // 8

            out = bytearray(width * height * 2)

            for y in range(height):
                bmp_y = (height - 1 - y) if is_bottom_up else y
                row_offset = offset + bmp_y * row_size
                for x in range(width):
                    px_offset = row_offset + x * bytes_per_pixel
                    b = data[px_offset]
                    g = data[px_offset + 1]
                    r = data[px_offset + 2]

                    # 16-bit RGB565: R(5) G(6) B(5)
                    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                    idx = (y * width + x) * 2
                    # Big-endian for ST7789 SPI
                    out[idx] = (val >> 8) & 0xFF
                    out[idx + 1] = val & 0xFF

            return bytes(out)
        except Exception as e:
            print(f"[Error converting BMP to RGB565] {e}", file=sys.stderr)
            return self._generate_default_rgb565()


bridge = MusicBridge()


INDEX_HTML = """<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>Cardputer Now Playing</title>
  <style>
    @import url('https://cdn.jsdelivr.net/npm/@amansanoj/brand/globals.css');

    :root {
      --background: #050505;
      --card: #0d0d0d;
      --border: #252525;
      --text: #e6e6e6;
      --primary: #afbdd9;
      --secondary: #f0a133;
      --accent: #df9a9e;
      --muted: #808080;
      --radius: 8px;
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body { height: 100%; }

    body {
      font-family: var(--font-body);
      background: var(--background);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      padding: 32px 20px;
      -webkit-font-smoothing: antialiased;
    }

    .card {
      width: 100%;
      max-width: 440px;
      display: flex;
      gap: 18px;
      align-items: center;
      border: 1px solid var(--border);
      border-radius: var(--radius);
      background: var(--card);
      padding: 20px;
    }

    .art-wrapper {
      position: relative;
      width: 72px;
      height: 72px;
      flex-shrink: 0;
    }

    .art {
      width: 72px;
      height: 72px;
      border-radius: 4px;
      background: #151515;
      object-fit: cover;
      border: 1px solid var(--border);
      display: block;
    }

    .pause-overlay {
      position: absolute;
      bottom: 3px;
      right: 3px;
      width: 16px;
      height: 16px;
      border-radius: 4px;
      background: #270c0e;
      border: 1px solid var(--accent);
      display: none;
      align-items: center;
      justify-content: center;
      gap: 2px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.4);
    }

    .pause-overlay.visible {
      display: flex;
    }

    .pause-overlay span {
      width: 2px;
      height: 8px;
      background: var(--accent);
      border-radius: 1px;
    }

    .info {
      display: flex;
      flex-direction: column;
      justify-content: center;
      overflow: hidden;
      min-width: 0;
      gap: 3px;
    }

    .title {
      font-family: var(--font-display);
      font-size: 1.05rem;
      font-weight: 600;
      color: var(--text);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .artist {
      font-size: 0.85rem;
      color: var(--muted);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .album {
      font-size: 0.75rem;
      color: var(--muted);
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }

    .time {
      font-family: var(--font-mono);
      font-size: 0.8rem;
      color: var(--primary);
      margin-top: 4px;
    }

    .controls {
      margin-top: 18px;
      display: flex;
      gap: 8px;
      justify-content: center;
      width: 100%;
      max-width: 440px;
    }

    .btn {
      flex: 1;
      text-align: center;
      background: var(--card);
      color: var(--text);
      border: 1px solid var(--border);
      padding: 9px 8px;
      border-radius: var(--radius);
      cursor: pointer;
      font-family: var(--font-body);
      font-size: 0.8rem;
      font-weight: 500;
      outline: none;
      user-select: none;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      transition: border-color 0.15s ease, background-color 0.15s ease, transform 0.05s ease;
    }
    .btn:hover { border-color: var(--primary); background: #151515; }
    .btn:active { transform: scale(0.96); }

    .endpoints {
      margin-top: 24px;
      font-family: var(--font-mono);
      font-size: 0.7rem;
      color: var(--muted);
      text-align: center;
      line-height: 1.6;
    }
    .endpoints a { color: var(--primary); text-decoration: none; }
    .endpoints a:hover { text-decoration: underline; }
    .endpoints code {
      font-family: var(--font-mono);
      color: var(--primary);
      background: #151515;
      padding: 2px 6px;
      border-radius: 4px;
      border: 1px solid var(--border);
    }
  </style>
  <script>
    (function () {
      var isDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      document.documentElement.classList.toggle('dark', isDark);
    })();

    var currentArtId = '__ARTWORK_ID__';

    function updateDashboard() {
      fetch('/api/now-playing')
        .then(function(res) { return res.json(); })
        .then(function(data) {
          var isPlaying = (data.state === 'playing');
          var titleEl = document.getElementById('track-title');
          var artistEl = document.getElementById('track-artist');
          var albumEl = document.getElementById('track-album');
          var timeEl = document.getElementById('track-time');
          var playBtn = document.getElementById('btn-play');
          var artEl = document.getElementById('track-art');
          var pauseOverlay = document.getElementById('pause-overlay');

          if (titleEl) titleEl.textContent = data.title || (isPlaying ? 'Unknown Track' : 'Not Playing');
          if (artistEl) artistEl.textContent = data.artist || '';
          if (albumEl) albumEl.textContent = data.album || '';

          var el = data.elapsed || 0;
          var du = data.duration || 0;
          var pad = function(n) { return (n < 10 ? '0' : '') + n; };
          var timeStr = pad(Math.floor(el / 60)) + ':' + pad(el % 60) + ' / ' + pad(Math.floor(du / 60)) + ':' + pad(du % 60);
          if (timeEl) timeEl.textContent = timeStr;

          // Pause overlay badge on album art
          if (pauseOverlay) {
            if (data.state === 'paused') {
              pauseOverlay.classList.add('visible');
            } else {
              pauseOverlay.classList.remove('visible');
            }
          }

          if (playBtn) {
            playBtn.textContent = isPlaying ? '❚❚ Pause' : '▶ Play';
          }

          // ONLY update artwork if artwork_id actually changes!
          if (artEl && data.artwork_id && data.artwork_id !== currentArtId) {
            currentArtId = data.artwork_id;
            artEl.src = '/artwork.jpg?id=' + encodeURIComponent(currentArtId);
          }
        })
        .catch(function(err) {
          console.debug('Sync status:', err);
        });
    }

    // Smooth background polling every 2s (fetching lightweight ~200B JSON only)
    setInterval(updateDashboard, 2000);

    function sendControl(url) {
      fetch(url)
        .then(function() {
          setTimeout(updateDashboard, 150);
        })
        .catch(function(err) {
          console.error('Control error:', err);
        });
    }
  </script>
</head>
<body>
  <div class="card">
    <div class="art-wrapper">
      <img id="track-art" class="art" src="/artwork.jpg?id=__ARTWORK_ID__" onerror="this.src='data:image/svg+xml;utf8,<svg xmlns=\\'http://www.w3.org/2000/svg\\' width=\\'72\\' height=\\'72\\' viewBox=\\'0 0 72 72\\'><rect width=\\'72\\' height=\\'72\\' fill=\\'%23000\\'/><text x=\\'36\\' y=\\'41\\' font-size=\\'11\\' fill=\\'%23fff\\' text-anchor=\\'middle\\'>NO ART</text></svg>'">
      <div id="pause-overlay" class="pause-overlay __PAUSE_CLASS__">
        <span></span>
        <span></span>
      </div>
    </div>
    <div class="info">
      <div id="track-title" class="title">__TITLE__</div>
      <div id="track-artist" class="artist">__ARTIST__</div>
      <div id="track-album" class="album">__ALBUM__</div>
      <div id="track-time" class="time">__TIME__</div>
    </div>
  </div>
  <div class="controls">
    <button class="btn" type="button" onclick="sendControl('/api/previous')">|◀ Prev</button>
    <button class="btn" type="button" onclick="sendControl('/api/backward')">◀◀ -10s</button>
    <button id="btn-play" class="btn" type="button" onclick="sendControl('/api/toggle')">__PLAY_PAUSE_BTN__</button>
    <button class="btn" type="button" onclick="sendControl('/api/forward')">▶▶ +10s</button>
    <button class="btn" type="button" onclick="sendControl('/api/next')">▶| Next</button>
  </div>
  <div class="endpoints">
    Endpoints:
    <a href="/api/now-playing" target="_blank">/api/now-playing</a> |
    <a href="/artwork.raw" target="_blank">/artwork.raw (72x72 RGB565)</a> |
    <a href="/artwork.jpg" target="_blank">/artwork.jpg</a>
  </div>
</body>
</html>
"""


class RequestHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Concise logging
        sys.stdout.write(f"[{time.strftime('%H:%M:%S')}] {self.command} {self.path} - {args[1]}\n")

    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        super().end_headers()

    def do_GET(self):
        path = self.path.split('?')[0]

        if path == '/api/now-playing':
            meta = bridge.query_music()
            body = json.dumps(meta, indent=2).encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        elif path in ('/artwork.raw', '/artwork.rgb565'):
            bridge.query_music()
            raw = bridge.raw_rgb565
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        elif path == '/artwork.jpg':
            bridge.query_music()
            jpg = bridge.jpeg_data
            if not jpg:
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-Type', 'image/jpeg')
            self.send_header('Content-Length', str(len(jpg)))
            self.end_headers()
            self.wfile.write(jpg)

        elif path == '/health':
            self.send_response(200)
            self.send_header('Content-Type', 'text/plain')
            self.end_headers()
            self.wfile.write(b"OK")

        elif path in ('/api/toggle', '/api/playpause', '/api/play', '/api/pause', '/api/next', '/api/previous',
                      '/api/forward', '/api/backward', '/api/seek_forward', '/api/seek_backward'):
            action = path.split('/')[-1]
            success, msg = bridge.control_playback(action)
            res = {"success": success, "message": msg, "action": action}
            body = json.dumps(res).encode('utf-8')
            self.send_response(200 if success else 500)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        elif path == '/':
            meta = bridge.query_music()
            state = meta.get('state', 'stopped')
            title = meta.get('title') or ('Not Playing' if state == 'stopped' else 'Unknown Track')
            artist = meta.get('artist') or ''
            album = meta.get('album') or ''
            elapsed = meta.get('elapsed', 0)
            duration = meta.get('duration', 0)
            time_str = f"{elapsed // 60:02d}:{elapsed % 60:02d} / {duration // 60:02d}:{duration % 60:02d}"
            play_btn_text = "❚❚ Pause" if state == 'playing' else "▶ Play"

            pause_class = "visible" if state == 'paused' else ""

            html = INDEX_HTML.replace('__ARTWORK_ID__', meta.get('artwork_id', 'none')) \
                             .replace('__TITLE__', title) \
                             .replace('__ARTIST__', artist) \
                             .replace('__ALBUM__', album) \
                             .replace('__TIME__', time_str) \
                             .replace('__PLAY_PAUSE_BTN__', play_btn_text) \
                             .replace('__PAUSE_CLASS__', pause_class)

            body = html.encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        path = self.path.split('?')[0]
        if path in ('/api/toggle', '/api/playpause', '/api/play', '/api/pause', '/api/next', '/api/previous',
                    '/api/forward', '/api/backward', '/api/seek_forward', '/api/seek_backward'):
            action = path.split('/')[-1]
            success, msg = bridge.control_playback(action)
            res = {"success": success, "message": msg, "action": action}
            body = json.dumps(res).encode('utf-8')
            self.send_response(200 if success else 500)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()


def get_local_ip():
    import socket
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def main():
    parser = argparse.ArgumentParser(description="Apple Music ESP32 Companion Bridge")
    parser.add_argument("--port", type=int, default=58329, help="HTTP server port (default: 58329)")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="Listen host (default: 0.0.0.0)")
    args = parser.parse_args()

    local_ip = get_local_ip()

    print("=" * 60)
    print(" Cardputer Now Playing - ESP32 / Wokwi Companion Bridge")
    print("=" * 60)
    print(f" * Listening on        : http://{args.host}:{args.port}")
    print(f" * Mac Local IP        : http://{local_ip}:{args.port}")
    print(f" * For Wokwi Simulator : http://host.wokwi.internal:{args.port}")
    print(f" * Metadata Endpoint   : http://host.wokwi.internal:{args.port}/api/now-playing")
    print(f" * Artwork RGB565      : http://host.wokwi.internal:{args.port}/artwork.raw (72x72 px)")
    print("=" * 60)

    print(" Press Ctrl+C to stop.\n")

    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer((args.host, args.port), RequestHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down bridge service...")


if __name__ == '__main__':
    main()
