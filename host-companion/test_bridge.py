#!/usr/bin/env python3
"""
Test script for the unified macOS Companion Bridge logic.
Verifies AppleScript/JXA querying, JSON schema, Pillow/sips downscaling,
RGB565 generation, and WebSocket/HTTP server functionality.
"""

import sys
import os
import socket
import base64
import hashlib
import threading
import json
import time

# Add host-companion directory to path
sys.path.insert(0, os.path.dirname(__file__))

from bridge import (
    MusicController,
    CompanionBridge,
    ThreadedTCPServer,
    create_request_handler,
    make_ws_frame,
    read_ws_frame,
    rgb_to_rgb565,
    generate_default_rgb565,
    clean_text,
    ARTWORK_SIZE
)

def run_tests():
    print("Testing Unified MusicController...")
    controller = MusicController(art_size=ARTWORK_SIZE)

    # 1. Test clean_text helper
    dirty = "“Song’s Title”… – \u00a0Special"
    cleaned = clean_text(dirty)
    assert cleaned == '"Song\'s Title"... - Special', f"clean_text failed: {cleaned}"
    print(f"✓ Metadata sanitization clean_text verified: '{dirty}' -> '{cleaned}'")

    # 2. Test default placeholder generation
    raw_default = generate_default_rgb565(size=ARTWORK_SIZE)
    expected_bytes = ARTWORK_SIZE * ARTWORK_SIZE * 2
    assert len(raw_default) == expected_bytes, f"Expected {expected_bytes} bytes, got {len(raw_default)}"
    print(f"✓ Default RGB565 placeholder generated: {len(raw_default)} bytes")

    # 3. Test RGB to RGB565 conversion
    test_rgb = bytes([255, 0, 0, 0, 255, 0]) # 1 red pixel, 1 green pixel
    rgb565 = rgb_to_rgb565(test_rgb, 2, 1)
    assert len(rgb565) == 4
    # Red: R(31)=0xF800 (hi: 0xF8, lo: 0x00)
    assert rgb565[0] == 0xF8 and rgb565[1] == 0x00
    print("✓ Direct RGB24 to RGB565 in-memory conversion verified")

    # 4. Test live query to player (Apple Music or Spotify)
    meta = controller.query()
    assert isinstance(meta, dict), "Metadata must be a dictionary"
    assert "state" in meta, "Metadata must contain 'state'"
    assert "title" in meta, "Metadata must contain 'title'"
    assert "artist" in meta, "Metadata must contain 'artist'"
    assert "artwork_id" in meta, "Metadata must contain 'artwork_id'"
    assert "player" in meta, "Metadata must contain 'player'"
    print(f"✓ Query active player ({meta['player']}) successful: state={meta['state']}, title='{meta['title']}'")

    # 5. Test raw RGB565 buffer size
    raw = controller.cached_rgb565
    assert len(raw) == expected_bytes, f"Expected {expected_bytes} bytes, got {len(raw)}"
    print(f"✓ RGB565 buffer valid: {len(raw)} bytes ({ARTWORK_SIZE}x{ARTWORK_SIZE} 16-bit)")

    # 6. Test control playback command dispatch
    assert not controller.execute_command("unknown_invalid_cmd"), "Unknown action should fail"
    print("✓ Control playback validation verified (unknown action rejected)")

    # 7. Test RFC 6455 WebSocket frame encoding & decoding
    test_msg = '{"action":"test_ping"}'
    frame = make_ws_frame(test_msg.encode('utf-8'), opcode=0x1)
    assert frame[0] == 0x81, "Opcode 1 with FIN must be 0x81"
    assert frame[1] == len(test_msg), "Frame length must match message length"
    assert frame[2:] == test_msg.encode('utf-8')
    print(f"✓ RFC 6455 WebSocket frame encoding verified: {len(frame)} bytes")

    # 8. Test live WebSocket handshake calculation
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    accept_str = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    expected_accept = base64.b64encode(hashlib.sha1(accept_str.encode('utf-8')).digest()).decode('utf-8')
    assert expected_accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 test vector failed"
    print("✓ RFC 6455 Sec-WebSocket-Accept handshake calculation matches RFC test vector")

    # 9. Test live HTTP & WebSocket server
    bridge = CompanionBridge(controller)
    test_port = 58394
    handler = create_request_handler(bridge)
    server = ThreadedTCPServer(('127.0.0.1', test_port), handler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    time.sleep(0.2)

    sock = socket.socket()
    sock.connect(('127.0.0.1', test_port))
    handshake_req = (
        f"GET /ws HTTP/1.1\r\n"
        f"Host: 127.0.0.1:{test_port}\r\n"
        f"Upgrade: websocket\r\n"
        f"Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        f"Sec-WebSocket-Version: 13\r\n\r\n"
    )
    sock.sendall(handshake_req.encode())
    resp = sock.recv(1024)
    assert b"101 Switching Protocols" in resp
    assert b"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in resp

    opcode, payload = read_ws_frame(sock)
    assert opcode == 0x1, f"Expected text frame opcode 1, got {opcode}"

    try:
        sock.sendall(make_ws_frame(b"", opcode=0x8))
    except Exception:
        pass
    sock.close()
    server.shutdown()
    server.server_close()
    print("✓ Live WebSocket server handshake and initial metadata frame push verified")

    print("\nAll Unified Companion Bridge unit tests passed successfully!")


if __name__ == '__main__':
    run_tests()
