"""
A minimal WebSocket client (RFC 6455) for ComfyUI's progress stream `/ws?clientId=...`: text messages in, nothing out but
pongs and the close. The frame codec is the control client's (`tools/control/bagi_control_client.py`), put on `sys.path`.
"""

import base64
import os
import socket
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

from bagi_control_client import FrameReader, accept_key, encode_frame  # noqa: E402

OP_CONTINUATION = 0x0
OP_TEXT = 0x1
OP_BINARY = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA
RECV_CHUNK = 65536


class WebSocketClosed(Exception):
    pass


class WebSocketClient:
    """`recv(timeout)` returns the next whole message as (opcode, payload), or None when nothing came within the timeout."""

    def __init__(self, host, port, path, connect_timeout=10.0):
        self._socket = socket.create_connection((host, port), timeout=connect_timeout)
        self._reader = FrameReader()
        self._fragments = None
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
        )
        self._socket.sendall(request.encode("ascii"))
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self._socket.recv(RECV_CHUNK)
            if not chunk:
                raise WebSocketClosed("the server closed the connection during the handshake")
            head += chunk
        head, _, rest = head.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in f"{lines[0]} ":
            raise WebSocketClosed(f"no WebSocket upgrade: {lines[0]}")
        headers = {name.strip().lower(): value.strip() for name, _, value in (line.partition(":") for line in lines[1:])}
        if headers.get("sec-websocket-accept") != accept_key(key):
            raise WebSocketClosed("the Sec-WebSocket-Accept value is wrong")
        self._reader.feed(rest)

    def recv(self, timeout):
        self._socket.settimeout(timeout)
        while True:
            frame = self._reader.pop()
            if frame is None:
                try:
                    chunk = self._socket.recv(RECV_CHUNK)
                except socket.timeout:
                    return None
                if not chunk:
                    raise WebSocketClosed("the server closed the connection")
                self._reader.feed(chunk)
                continue
            fin, opcode, payload = frame
            if opcode == OP_PING:
                self._send(OP_PONG, payload)
            elif opcode == OP_CLOSE:
                raise WebSocketClosed("the server sent close")
            elif opcode == OP_CONTINUATION:
                if self._fragments is None:
                    continue
                self._fragments[1].extend(payload)
                if fin:
                    opcode, data = self._fragments
                    self._fragments = None
                    return opcode, bytes(data)
            elif opcode in (OP_TEXT, OP_BINARY):
                if fin:
                    return opcode, payload
                self._fragments = (opcode, bytearray(payload))

    def _send(self, opcode, payload=b""):
        self._socket.sendall(encode_frame(opcode, payload, mask=os.urandom(4)))

    def close(self):
        try:
            self._send(OP_CLOSE)
        except OSError:
            pass
        self._socket.close()
