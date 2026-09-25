#!/usr/bin/env python3
"""
Test script for the macOS Companion Bridge logic.
Verifies AppleScript querying, JSON schema, downscaling, and RGB565 generation.
"""

import sys
import os

# Add host-companion directory to path
sys.path.insert(0, os.path.dirname(__file__))

from bridge import MusicBridge, ARTWORK_SIZE

def run_tests():
    print("Testing MusicBridge...")
    bridge = MusicBridge(art_size=ARTWORK_SIZE)

    # 1. Test default placeholder generation
    raw_default = bridge._generate_default_rgb565()
    expected_bytes = ARTWORK_SIZE * ARTWORK_SIZE * 2
    assert len(raw_default) == expected_bytes, f"Expected {expected_bytes} bytes, got {len(raw_default)}"
    print(f"✓ Default RGB565 placeholder generated: {len(raw_default)} bytes")

    # 2. Test live query to Music.app
    meta = bridge.query_music()
    assert isinstance(meta, dict), "Metadata must be a dictionary"
    assert "state" in meta, "Metadata must contain 'state'"
    assert "title" in meta, "Metadata must contain 'title'"
    assert "artist" in meta, "Metadata must contain 'artist'"
    assert "artwork_id" in meta, "Metadata must contain 'artwork_id'"
    print(f"✓ Query Music.app successful: state={meta['state']}, title='{meta['title']}', artist='{meta['artist']}'")

    # 3. Test raw RGB565 buffer size
    raw = bridge.raw_rgb565
    assert len(raw) == expected_bytes, f"Expected {expected_bytes} bytes, got {len(raw)}"
    print(f"✓ RGB565 buffer valid: {len(raw)} bytes ({ARTWORK_SIZE}x{ARTWORK_SIZE} 16-bit)")

    print("\nAll Companion Bridge unit checks passed successfully!")

if __name__ == '__main__':
    run_tests()
