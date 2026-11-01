"""Minimal client of the engine's control API: WebSocket (RFC 6455) carrying JSON, standard library only.

Start a game with `--control` (see src/Modules/RemoteControl/CLAUDE.md), then:

    from bagi_control_client import BagiControlClient

    with BagiControlClient(port=8765) as client:
        print(client.call("get_state"))
        client.call("perform_action", {"type": "swap", "from": {"col": 0, "row": 0}, "to": {"col": 1, "row": 0}})

A response with "ok": false raises ControlError; the pushed events that arrive while a call waits are kept in
`client.events`.
"""

import base64
import hashlib
import json
import os
import socket
import struct
from typing import Any

WEBSOCKET_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OPCODE_CONTINUATION = 0x0
OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765
DEFAULT_TIMEOUT = 10.0

_MAX_HANDSHAKE_BYTES = 16 * 1024
_RECV_CHUNK = 64 * 1024


class ControlError(Exception):
    """An error response of the engine: `code` is the protocol code (parse_error, illegal_action, ...)."""

    def __init__(self, code: str, message: str):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class ConnectionClosed(Exception):
    """The engine closed the connection (a close frame or the end of the stream)."""

    def __init__(self, code: int | None = None, reason: str = ""):
        super().__init__(f"connection closed (code {code}) {reason}".strip())
        self.code = code
        self.reason = reason


class HandshakeError(Exception):
    """The engine did not switch protocols: `status` is the HTTP status it answered with."""

    def __init__(self, status: int, body: str):
        super().__init__(f"handshake refused with HTTP {status}: {body}")
        self.status = status
        self.body = body


def accept_key(client_key: str) -> str:
    """The Sec-WebSocket-Accept value the server has to answer for `client_key` (RFC 6455, section 4.2.2)."""
    digest = hashlib.sha1((client_key + WEBSOCKET_GUID).encode("ascii")).digest()
    return base64.b64encode(digest).decode("ascii")


def encode_frame(opcode: int, payload: bytes = b"", mask: bytes | None = None, fin: bool = True) -> bytes:
    """One frame. A client's frames have to be masked: pass a 4-byte `mask`."""
    first = (0x80 if fin else 0) | opcode
    mask_bit = 0x80 if mask is not None else 0
    length = len(payload)
    if length < 126:
        header = struct.pack("!BB", first, mask_bit | length)
    elif length <= 0xFFFF:
        header = struct.pack("!BBH", first, mask_bit | 126, length)
    else:
        header = struct.pack("!BBQ", first, mask_bit | 127, length)
    if mask is None:
        return header + payload
    return header + mask + _apply_mask(payload, mask)


def _apply_mask(payload: bytes, mask: bytes) -> bytes:
    if not payload:
        return b""
    # XOR the whole payload against the repeated key as one big integer: much faster than a byte loop
    repeated = (mask * (len(payload) // 4 + 1))[: len(payload)]
    value = int.from_bytes(payload, "big") ^ int.from_bytes(repeated, "big")
    return value.to_bytes(len(payload), "big")


class FrameReader:
    """Cuts the byte stream into frames: feed() what arrives, pop() the complete frames as (fin, opcode, payload)."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> None:
        self._buffer += data

    def pop(self) -> tuple[bool, int, bytes] | None:
        buffer = self._buffer
        if len(buffer) < 2:
            return None
        fin = bool(buffer[0] & 0x80)
        opcode = buffer[0] & 0x0F
        masked = bool(buffer[1] & 0x80)
        length = buffer[1] & 0x7F
        position = 2
        if length == 126:
            if len(buffer) < position + 2:
                return None
            (length,) = struct.unpack_from("!H", buffer, position)
            position += 2
        elif length == 127:
            if len(buffer) < position + 8:
                return None
            (length,) = struct.unpack_from("!Q", buffer, position)
            position += 8
        mask = None
        if masked:
            if len(buffer) < position + 4:
                return None
            mask = bytes(buffer[position : position + 4])
            position += 4
        if len(buffer) < position + length:
            return None
        payload = bytes(buffer[position : position + length])
        del buffer[: position + length]
        if mask is not None:
            payload = _apply_mask(payload, mask)
        return fin, opcode, payload


class BagiControlClient:
    """One connection to a game started with --control. Use it as a context manager or call close()."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, timeout: float = DEFAULT_TIMEOUT, origin: str | None = None):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.origin = origin
        self.events: list[dict[str, Any]] = []
        self._socket: socket.socket | None = None
        self._reader = FrameReader()
        self._next_id = 1

    def __enter__(self) -> "BagiControlClient":
        self.connect()
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def connect(self) -> None:
        """Opens the connection and completes the WebSocket handshake; raises HandshakeError when the engine refuses."""
        self._socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        self._reader = FrameReader()

        key = base64.b64encode(os.urandom(16)).decode("ascii")
        lines = [
            "GET /control HTTP/1.1",
            f"Host: {self.host}:{self.port}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {key}",
            "Sec-WebSocket-Version: 13",
        ]
        if self.origin is not None:
            lines.append(f"Origin: {self.origin}")
        self._socket.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))

        head, rest = self._read_http_head()
        status_line, *header_lines = head.split("\r\n")
        status = int(status_line.split(" ")[1])
        headers = {}
        for line in header_lines:
            name, _, value = line.partition(":")
            headers[name.strip().lower()] = value.strip()
        if status != 101:
            body = self._read_http_body(headers, rest).decode("utf-8", errors="replace")
            self.close()
            raise HandshakeError(status, body)
        if headers.get("sec-websocket-accept") != accept_key(key):
            self.close()
            raise HandshakeError(status, "the Sec-WebSocket-Accept value is wrong")
        self._reader.feed(rest)

    def close(self) -> None:
        if self._socket is None:
            return
        try:
            self._socket.sendall(encode_frame(OPCODE_CLOSE, struct.pack("!H", 1000), os.urandom(4)))
        except OSError:
            pass
        try:
            self._socket.close()
        finally:
            self._socket = None

    def call(self, method: str, params: dict[str, Any] | None = None) -> Any:
        """Sends a request and returns its "result"; raises ControlError for an error response."""
        request_id = self._next_id
        self._next_id += 1
        request: dict[str, Any] = {"id": request_id, "method": method}
        if params is not None:
            request["params"] = params
        self.send_text(json.dumps(request))

        while True:
            message = self.receive_message()
            if "event" in message:
                self.events.append(message)
                continue
            if message.get("id") != request_id:
                raise ControlError("protocol_error", f"expected the response to request {request_id}, got {message}")
            if message.get("ok"):
                return message["result"]
            error = message.get("error", {})
            raise ControlError(error.get("code", "unknown"), error.get("message", ""))

    def send_text(self, text: str) -> None:
        """Sends `text` as one text message, as it is (for tests that send what is not a valid request)."""
        self._send(encode_frame(OPCODE_TEXT, text.encode("utf-8"), os.urandom(4)))

    def receive_message(self, timeout: float | None = None) -> dict[str, Any]:
        """The next JSON message of the engine, a response or an event; raises ConnectionClosed."""
        return json.loads(self.receive_text(timeout))

    def receive_text(self, timeout: float | None = None) -> str:
        """The next text message of the engine. Pings are answered here; a close frame raises ConnectionClosed."""
        message = bytearray()
        while True:
            frame = self._reader.pop()
            if frame is None:
                self._fill(timeout)
                continue
            fin, opcode, payload = frame
            if opcode == OPCODE_PING:
                self._send(encode_frame(OPCODE_PONG, payload, os.urandom(4)))
            elif opcode == OPCODE_PONG:
                continue
            elif opcode == OPCODE_CLOSE:
                code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else None
                raise ConnectionClosed(code, payload[2:].decode("utf-8", errors="replace"))
            elif opcode in (OPCODE_TEXT, OPCODE_BINARY, OPCODE_CONTINUATION):
                message += payload
                if fin:
                    return message.decode("utf-8")

    def _send(self, data: bytes) -> None:
        if self._socket is None:
            raise ConnectionClosed()
        self._socket.sendall(data)

    def _fill(self, timeout: float | None) -> None:
        if self._socket is None:
            raise ConnectionClosed()
        self._socket.settimeout(self.timeout if timeout is None else timeout)
        chunk = self._socket.recv(_RECV_CHUNK)
        if not chunk:
            raise ConnectionClosed()
        self._reader.feed(chunk)

    def _read_http_body(self, headers: dict[str, str], received: bytes) -> bytes:
        """The body of a refusal: `received` plus what Content-Length says is still on its way."""
        assert self._socket is not None
        try:
            expected = int(headers.get("content-length", "0"))
        except ValueError:
            expected = 0
        body = received
        while len(body) < expected:
            chunk = self._socket.recv(_RECV_CHUNK)
            if not chunk:
                break
            body += chunk
        return body

    def _read_http_head(self) -> tuple[str, bytes]:
        """The HTTP response head, and the bytes that came after it (a refusal's body, or the first frames)."""
        assert self._socket is not None
        data = b""
        while b"\r\n\r\n" not in data:
            if len(data) > _MAX_HANDSHAKE_BYTES:
                raise HandshakeError(0, "the response head is too long")
            chunk = self._socket.recv(_RECV_CHUNK)
            if not chunk:
                raise ConnectionClosed()
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        return head.decode("ascii", errors="replace"), rest
