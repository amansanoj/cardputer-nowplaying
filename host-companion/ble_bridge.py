#!/usr/bin/env python3
"""
Cardputer Now Playing - Bluetooth Low Energy (BLE) Companion Bridge for macOS
=============================================================================
Communicates directly with Apple Music on macOS and pairs wirelessly with
Cardputer-NowPlaying over Bluetooth Low Energy.

- Zero Wi-Fi configuration required
- Zero cables / USB ports required
- Sub-50ms tactile media controls
- Automatic chunked artwork streaming (cached permanently on Cardputer SD card)

Usage:
  uv run --with bleak python3 host-companion/ble_bridge.py
  OR:
  python3 host-companion/ble_bridge.py  (if bleak is installed)
"""

import asyncio
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time
from typing import Optional

try:
    from bleak import BleakScanner, BleakClient
except ImportError:
    print("\n[Error] The 'bleak' library is required for macOS Bluetooth Low Energy.")
    print("Run with uv:")
    print("  uv run --with bleak python3 host-companion/ble_bridge.py\n")
    print("Or install via pip:")
    print("  pip3 install bleak\n")
    sys.exit(1)

# BLE UUIDs matching firmware/include/ble_manager.h
BLE_DEVICE_NAME        = "Cardputer-NowPlaying"
BLE_SERVICE_UUID       = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
BLE_CHAR_METADATA_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
BLE_CHAR_CONTROL_UUID  = "beb5483f-36e1-4688-b7f5-ea07361b26a8"
BLE_CHAR_ARTWORK_UUID  = "beb54840-36e1-4688-b7f5-ea07361b26a8"

ARTWORK_SIZE = 72
CHUNK_SIZE = 480  # fits safely inside 512 MTU

class MusicController:
    """Queries macOS Music.app via AppleScript."""
    def __init__(self):
        self.last_track_key = None
        self.cached_rgb565 = b""
        self.cached_art_id = "none"

    def query(self) -> dict:
        now_ts = time.time()
        tz_offset = -time.timezone if (time.daylight == 0) else -time.altzone
        clock_str = time.strftime("%H:%M")

        scpt = '''
        if application "Music" is running then
            tell application "Music"
                set pState to (player state as string)
                if pState is not "stopped" then
                    try
                        set tTrack to current track
                        set tName to name of tTrack
                        set tArtist to artist of tTrack
                        set tAlbum to album of tTrack
                        set tDur to duration of tTrack
                        set tPos to player position
                        set hasArt to (count of artworks of tTrack) > 0
                        return pState & "|||" & tName & "|||" & tArtist & "|||" & tAlbum & "|||" & (tDur as string) & "|||" & (tPos as string) & "|||" & (hasArt as string)
                    on error
                        return pState & "||||||||||||0|||0|||false"
                    end try
                else
                    return "stopped||||||||||||0|||0|||false"
                end if
            end tell
        else
            return "not_running||||||||||||0|||0|||false"
        end if
        '''
        try:
            res = subprocess.run(["osascript", "-e", scpt], capture_output=True, text=True, timeout=1.5)
            line = res.stdout.strip()
            parts = line.split("|||")
            if len(parts) >= 7:
                state, title, artist, album, dur_str, pos_str, art_flag = parts[:7]
                if state == "not_running":
                    return {"running": False, "state": "stopped", "title": "", "artist": "", "album": "", "duration": 0, "elapsed": 0, "artwork_id": "none", "clock": clock_str}

                try:
                    duration = int(float(dur_str.replace(",", ".")))
                except ValueError:
                    duration = 0
                try:
                    elapsed = int(float(pos_str.replace(",", ".")))
                except ValueError:
                    elapsed = 0

                has_art = (art_flag.lower() == "true")
                track_key = f"{artist}_{album}_{title}"
                if has_art and track_key != self.last_track_key:
                    self._extract_artwork(track_key)

                return {
                    "running": True,
                    "state": state.lower(),
                    "title": title,
                    "artist": artist,
                    "album": album,
                    "duration": duration,
                    "elapsed": elapsed,
                    "artwork_id": self.cached_art_id,
                    "clock": clock_str
                }
        except Exception:
            pass

        return {"running": False, "state": "stopped", "title": "", "artist": "", "album": "", "duration": 0, "elapsed": 0, "artwork_id": "none", "clock": clock_str}

    def execute_command(self, cmd: str):
        mapping = {
            "toggle": 'tell application "Music" to playpause',
            "next": 'tell application "Music" to next track',
            "prev": 'tell application "Music" to previous track',
            "ff": 'tell application "Music" to set player position to (player position + 10)',
            "rw": 'tell application "Music" to set player position to (player position - 10)'
        }
        if cmd in mapping:
            print(f"[BLE Control] Executing action: {cmd}")
            subprocess.run(["osascript", "-e", mapping[cmd]], timeout=1.0)

    def _extract_artwork(self, track_key: str):
        """Extract album artwork and convert to 72x72 RGB565."""
        self.last_track_key = track_key
        scpt = '''
        tell application "Music"
            if (count of artworks of current track) > 0 then
                set rawData to raw data of artwork 1 of current track
                return rawData
            end if
        end tell
        '''
        try:
            res = subprocess.run(["osascript", "-e", scpt], capture_output=True, timeout=2.0)
            if res.returncode == 0 and res.stdout:
                with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as tmp_in:
                    tmp_in.write(res.stdout)
                    tmp_in_path = tmp_in.name

                tmp_out_path = tmp_in_path + ".raw"
                cmd = ["sips", "-z", str(ARTWORK_SIZE), str(ARTWORK_SIZE), "-s", "format", "raw", tmp_in_path, "--out", tmp_out_path]
                subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2.0)

                if os.path.exists(tmp_out_path):
                    with open(tmp_out_path, "rb") as f:
                        raw_bytes = f.read()

                    # Convert RGBA/RGB to 16-bit RGB565
                    rgb565 = bytearray()
                    step = 4 if len(raw_bytes) >= ARTWORK_SIZE * ARTWORK_SIZE * 4 else 3
                    for i in range(0, len(raw_bytes), step):
                        if len(rgb565) >= ARTWORK_SIZE * ARTWORK_SIZE * 2:
                            break
                        r = raw_bytes[i]
                        g = raw_bytes[i+1] if i+1 < len(raw_bytes) else 0
                        b = raw_bytes[i+2] if i+2 < len(raw_bytes) else 0
                        p = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                        rgb565.append((p >> 8) & 0xFF)
                        rgb565.append(p & 0xFF)

                    self.cached_rgb565 = bytes(rgb565)
                    self.cached_art_id = hashlib.md5(f"{track_key}_{len(self.cached_rgb565)}".encode()).hexdigest()[:12]

                    try:
                        os.unlink(tmp_in_path)
                        os.unlink(tmp_out_path)
                    except OSError:
                        pass
                    return
        except Exception as e:
            print(f"[Artwork] Extraction error: {e}", file=sys.stderr)

        self.cached_art_id = "none"
        self.cached_rgb565 = b""


class BleCompanion:
    def __init__(self):
        self.music = MusicController()
        self.client: Optional[BleakClient] = None
        self.last_sent_meta = {}

    async def run(self):
        print("==================================================")
        print(" Cardputer Now Playing - BLE Companion (macOS)")
        print("==================================================")
        print(f" * Scanning for BLE Device: '{BLE_DEVICE_NAME}'...")
        print(" * Please ensure your Cardputer is powered on.\n")

        while True:
            try:
                device = await BleakScanner.find_device_by_filter(
                    lambda d, ad: d.name and BLE_DEVICE_NAME.lower() in d.name.lower(),
                    timeout=5.0
                )

                if not device:
                    print(f"[BLE] Scanning for '{BLE_DEVICE_NAME}'...", end="\r", flush=True)
                    await asyncio.sleep(2.0)
                    continue

                print(f"\n[BLE] Found Cardputer! ({device.address}). Connecting...")

                async with BleakClient(device) as client:
                    self.client = client
                    print("[BLE] Connected successfully to Cardputer-NowPlaying!")

                    # Subscribe to controls & artwork requests
                    await client.start_notify(BLE_CHAR_CONTROL_UUID, self._on_control_received)
                    print("[BLE] Subscribed to Cardputer keyboard controls.")

                    while client.is_connected:
                        meta = self.music.query()
                        if meta != self.last_sent_meta:
                            payload = json.dumps(meta).encode('utf-8')
                            try:
                                await client.write_gatt_char(BLE_CHAR_METADATA_UUID, payload, response=False)
                                self.last_sent_meta = meta
                            except Exception as e:
                                print(f"[BLE] Send metadata error: {e}")
                                break
                        await asyncio.sleep(0.3)

            except Exception as e:
                print(f"\n[BLE] Connection lost or error: {e}. Retrying in 3 seconds...")
                await asyncio.sleep(3.0)

    def _on_control_received(self, sender, data: bytearray):
        msg = data.decode('utf-8', errors='ignore').strip()
        if not msg:
            return

        if msg.startswith("GET_ART:"):
            art_id = msg.split(":", 1)[1]
            print(f"[BLE] Cardputer requested artwork ({art_id}). Streaming...")
            asyncio.create_task(self._stream_artwork())
        else:
            self.music.execute_command(msg)

    async def _stream_artwork(self):
        """Stream raw 72x72 RGB565 artwork in MTU-sized packets."""
        if not self.client or not self.music.cached_rgb565:
            return

        data = self.music.cached_rgb565
        total_len = len(data)
        chunks = [data[i:i + CHUNK_SIZE] for i in range(0, total_len, CHUNK_SIZE)]
        total_chunks = len(chunks)

        for idx, chunk in enumerate(chunks):
            header = bytearray([
                (idx >> 8) & 0xFF, idx & 0xFF,
                (total_chunks >> 8) & 0xFF, total_chunks & 0xFF
            ])
            packet = header + chunk
            try:
                await self.client.write_gatt_char(BLE_CHAR_ARTWORK_UUID, packet, response=False)
                await asyncio.sleep(0.01)  # small pacing between chunks
            except Exception as e:
                print(f"[BLE] Artwork stream error: {e}")
                return

        print(f"[BLE] Artwork streaming complete ({total_len} bytes in {total_chunks} chunks).")


if __name__ == "__main__":
    companion = BleCompanion()
    try:
        asyncio.run(companion.run())
    except KeyboardInterrupt:
        print("\n[BLE] Exiting.")
