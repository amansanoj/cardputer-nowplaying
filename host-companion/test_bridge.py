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

    # 4. Test control playback command dispatch
    ok, err = bridge.control_playback("unknown_cmd")
    assert not ok, "Unknown action should fail"
    print("✓ Control playback validation verified (unknown action rejected)")

    # 5. Test web dashboard HTML rendering
    from bridge import INDEX_HTML
    rendered_html = INDEX_HTML.replace('__ARTWORK_ID__', meta.get('artwork_id', 'none')) \
                              .replace('__TITLE__', meta.get('title') or 'Not Playing') \
                              .replace('__ARTIST__', meta.get('artist') or '') \
                              .replace('__ALBUM__', meta.get('album') or '') \
                              .replace('__TIME__', '01:23 / 03:45') \
                              .replace('__PLAY_PAUSE_BTN__', '❚❚ Pause') \
                              .replace('__PAUSE_CLASS__', 'visible')
    assert '<title>Cardputer Now Playing</title>' in rendered_html
    assert '__TITLE__' not in rendered_html
    assert '__PLAY_PAUSE_BTN__' not in rendered_html
    assert '__ARTWORK_ID__' not in rendered_html
    assert '__PAUSE_CLASS__' not in rendered_html
    # 6. Test WebSocket frame encoding & decoding
    from bridge import make_ws_frame, read_ws_frame
    test_msg = '{"action":"test_ping"}'
    frame = make_ws_frame(test_msg.encode('utf-8'), opcode=0x1)
    assert frame[0] == 0x81, "Opcode 1 with FIN must be 0x81"
    assert frame[1] == len(test_msg), "Frame length must match message length"
    assert frame[2:] == test_msg.encode('utf-8')
    print(f"✓ RFC 6455 WebSocket frame encoding verified: {len(frame)} bytes")

    # 7. Test live WebSocket handshake calculation
    import socket
    import base64
    import hashlib
    import threading
    import json
    import time
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    accept_str = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    expected_accept = base64.b64encode(hashlib.sha1(accept_str.encode('utf-8')).digest()).decode('utf-8')
    assert expected_accept == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "RFC 6455 test vector failed"
    print("✓ RFC 6455 Sec-WebSocket-Accept handshake calculation matches RFC test vector")

    # 8. Test live WebSocket server connection & initial frame delivery
    from bridge import ThreadingTCPServer, RequestHandler
    test_port = 58392
    server = ThreadingTCPServer(('127.0.0.1', test_port), RequestHandler)
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
    received_meta = json.loads(payload.decode('utf-8'))
    assert "state" in received_meta, "Pushed metadata must include playback state"
    sock.close()
    server.shutdown()
    print("✓ Live WebSocket server handshake and initial metadata frame push verified")

    print("\nAll Companion Bridge unit checks passed successfully!")


if __name__ == '__main__':
    run_tests()
