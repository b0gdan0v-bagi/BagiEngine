"""The peers a test of bagi_bridge.py plays: a WebSocket whose Host, Origin and frames the test chooses, and a page of the
control link on top of it.

    page = FakePage(page_port, token, lambda channel, request: "the answer")
    page.next_message()      # "<channel> <request>" or "<channel>", as the bridge sent it
    page.push("a push")
    page.close()

Standard library only; the frames come from bagi_control_client.
"""

import base64
import json
import os
import queue
import socket
import struct
import threading
from typing import Any, Callable

from bagi_control_client import OPCODE_CLOSE, OPCODE_PING, OPCODE_PONG, OPCODE_TEXT, FrameReader, HandshakeError, encode_frame

WAIT_SECONDS = 10.0
PAGE_ORIGIN = "http://localhost:8080"


class RawWebSocket:
    """One connection to a server on 127.0.0.1; a refused handshake raises HandshakeError."""

    def __init__(self, port: int, origin: str | None = None, host: str | None = None):
        self._socket = socket.create_connection(("127.0.0.1", port), timeout=WAIT_SECONDS)
        self._reader = FrameReader()
        self._send_lock = threading.Lock()
        lines = [
            "GET /link HTTP/1.1",
            f"Host: {host if host is not None else f'127.0.0.1:{port}'}",
            "Upgrade: websocket",
            "Connection: Upgrade",
            f"Sec-WebSocket-Key: {base64.b64encode(os.urandom(16)).decode('ascii')}",
            "Sec-WebSocket-Version: 13",
        ]
        if origin is not None:
            lines.append(f"Origin: {origin}")
        self._socket.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self._socket.recv(4096)
            if not chunk:
                raise ConnectionResetError("the server left during the handshake")
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        status = int(head.split(b" ")[1])
        if status != 101:
            while chunk := self._socket.recv(4096):
                rest += chunk
            self._socket.close()
            raise HandshakeError(status, rest.decode("utf-8", errors="replace"))
        self._reader.feed(rest)

    def send_bytes(self, data: bytes) -> None:
        with self._send_lock:
            self._socket.sendall(data)

    def send(self, opcode: int, payload: bytes = b"", fin: bool = True) -> None:
        self.send_bytes(encode_frame(opcode, payload, os.urandom(4), fin))

    def send_text(self, text: str) -> None:
        self.send(OPCODE_TEXT, text.encode("utf-8"))

    def receive(self, timeout: float | None = WAIT_SECONDS) -> tuple[int, bytes] | None:
        """The next frame as (opcode, payload); None at the end of the stream."""
        while (frame := self._reader.pop()) is None:
            try:
                self._socket.settimeout(timeout)
                chunk = self._socket.recv(256 * 1024)
            except TimeoutError:
                raise
            except OSError:
                return None
            if not chunk:
                return None
            self._reader.feed(chunk)
        return frame[1], frame[2]

    def receive_close(self) -> tuple[int, str]:
        """Skips to the close frame and gives its code and reason."""
        while (frame := self.receive()) is not None:
            if frame[0] == OPCODE_CLOSE:
                return struct.unpack("!H", frame[1][:2])[0], frame[1][2:].decode("utf-8")
        raise AssertionError("the connection ended without a close frame")

    def close(self) -> None:
        try:
            self._socket.shutdown(socket.SHUT_RDWR)  # wakes a thread blocked in recv
        except OSError:
            pass
        self._socket.close()


class FakePage:
    """A page that has dialled the bridge and sent its hello. `handler(channel, request)` gives the answer to send (None:
    none); every message of the bridge is also kept for next_message()."""

    def __init__(self, port: int, token: str, handler: Callable[[int, str], str | None] | None = None, origin: str = PAGE_ORIGIN):
        self._handler = handler
        self.socket = RawWebSocket(port, origin=origin)
        self.messages: queue.Queue[str] = queue.Queue()
        self.close_status: tuple[int, str] | None = None
        self._closed = threading.Event()
        self.socket.send_text(json.dumps({"bagiControlLink": 1, "token": token}))
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def send(self, text: str) -> None:
        self.socket.send_text(text)

    def push(self, text: str) -> None:
        self.send(f"0 {text}")

    def next_message(self) -> str:
        return self.messages.get(timeout=WAIT_SECONDS)

    def wait_closed(self) -> tuple[int, str] | None:
        """The code and reason of the bridge's close frame (None: the connection ended without one)."""
        assert self._closed.wait(WAIT_SECONDS), "the bridge did not close the page"
        return self.close_status

    def close(self) -> None:
        if not self._closed.is_set():
            try:
                self.socket.send(OPCODE_CLOSE, struct.pack("!H", 1001))
            except OSError:
                pass
            self._closed.wait(WAIT_SECONDS)
        self.socket.close()

    def _serve(self) -> None:
        try:
            while (frame := self.socket.receive(timeout=None)) is not None:
                opcode, payload = frame
                if opcode == OPCODE_CLOSE:
                    self.close_status = struct.unpack("!H", payload[:2])[0], payload[2:].decode("utf-8")
                    self.socket.send(OPCODE_CLOSE, payload[:2])
                    break
                if opcode == OPCODE_PING:
                    self.socket.send(OPCODE_PONG, payload)
                elif opcode == OPCODE_TEXT:
                    self._answer(payload.decode("utf-8"))
        except OSError:
            pass
        finally:
            self._closed.set()
            self.socket.close()

    def _answer(self, message: str) -> None:
        self.messages.put(message)
        channel, space, request = message.partition(" ")
        if space and self._handler is not None:
            answer = self._handler(int(channel), request)
            if answer is not None:
                self.send(f"{channel} {answer}")


def json_answers(respond: Callable[[str, dict | None], Any]) -> Callable[[int, str], str]:
    """A handler of FakePage that answers like a game: `respond(method, params)` gives the "result"."""

    def handler(channel: int, request: str) -> str:
        parsed = json.loads(request)
        return json.dumps({"id": parsed["id"], "ok": True, "result": respond(parsed["method"], parsed.get("params"))})

    return handler
