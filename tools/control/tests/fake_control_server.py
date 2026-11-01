"""A stand-in for a game's control API: a WebSocket server on 127.0.0.1 that answers requests from a Python function.

    server = FakeControlServer(lambda method, params: {"pong": True})
    port = server.start()
    ...
    server.stop()

The handler returns the "result" of the response, or raises FakeError(code, message) for an error response, or QuitServer to
answer and then drop the connection (a game that quits). `requests` keeps (method, params) of every request in order.
Standard library only; the frames come from bagi_control_client.
"""

import json
import socket
import threading
from typing import Any, Callable

from bagi_control_client import OPCODE_CLOSE, OPCODE_PING, OPCODE_PONG, OPCODE_TEXT, FrameReader, accept_key, encode_frame


class FakeError(Exception):
    def __init__(self, code: str, message: str = ""):
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class QuitServer(Exception):
    """Raised by a handler after it has its answer: the connection is dropped and the server stops listening."""

    def __init__(self, result: Any = None):
        super().__init__("quit")
        self.result = result


class FakeControlServer:
    def __init__(self, handler: Callable[[str, dict | None], Any]):
        self._handler = handler
        self._listener: socket.socket | None = None
        self._threads: list[threading.Thread] = []
        self._connections: list[socket.socket] = []
        self._lock = threading.Lock()
        self._stopped = threading.Event()
        self.requests: list[tuple[str, dict | None]] = []
        self.quit_requested = threading.Event()

    def start(self, port: int = 0) -> int:
        """Listens on `port` (0: a free one) and gives the port."""
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", port))
        listener.listen()
        self._listener = listener
        accept = threading.Thread(target=self._accept_loop, daemon=True)
        accept.start()
        self._threads.append(accept)
        return listener.getsockname()[1]

    def stop(self) -> None:
        self._stopped.set()
        if self._listener is not None:
            self._listener.close()
        with self._lock:
            for connection in self._connections:
                try:
                    connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                connection.close()

    def _accept_loop(self) -> None:
        assert self._listener is not None
        while not self._stopped.is_set():
            try:
                connection, _ = self._listener.accept()
            except OSError:
                return
            with self._lock:
                self._connections.append(connection)
            thread = threading.Thread(target=self._serve, args=(connection,), daemon=True)
            thread.start()
            self._threads.append(thread)

    def _serve(self, connection: socket.socket) -> None:
        try:
            reader = self._handshake(connection)
            if reader is None:
                return
            self._frame_loop(connection, reader)
        except OSError:
            pass
        finally:
            try:
                connection.close()
            except OSError:
                pass

    @staticmethod
    def _handshake(connection: socket.socket) -> FrameReader | None:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = connection.recv(4096)
            if not chunk:
                return None
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        key = None
        for line in head.decode("ascii", errors="replace").split("\r\n")[1:]:
            name, _, value = line.partition(":")
            if name.strip().lower() == "sec-websocket-key":
                key = value.strip()
        if key is None:
            return None
        response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_key(key) + "\r\n\r\n"
        connection.sendall(response.encode("ascii"))
        reader = FrameReader()
        reader.feed(rest)
        return reader

    def _frame_loop(self, connection: socket.socket, reader: FrameReader) -> None:
        message = bytearray()
        while True:
            frame = reader.pop()
            if frame is None:
                chunk = connection.recv(65536)
                if not chunk:
                    return
                reader.feed(chunk)
                continue
            fin, opcode, payload = frame
            if opcode == OPCODE_PING:
                connection.sendall(encode_frame(OPCODE_PONG, payload))
            elif opcode == OPCODE_CLOSE:
                connection.sendall(encode_frame(OPCODE_CLOSE, payload[:2]))
                return
            elif opcode == OPCODE_TEXT or opcode == 0x0:
                message += payload
                if fin:
                    text, message = message.decode("utf-8"), bytearray()
                    if self._answer(connection, text):
                        return

    def _answer(self, connection: socket.socket, text: str) -> bool:
        """Answers one request; True when the connection is to be dropped after it."""
        request = json.loads(text)
        method, params = request["method"], request.get("params")
        with self._lock:
            self.requests.append((method, params))
        quit_after = False
        try:
            result = self._handler(method, params)
            response: dict[str, Any] = {"id": request["id"], "ok": True, "result": result}
        except FakeError as error:
            response = {"id": request["id"], "ok": False, "error": {"code": error.code, "message": error.message}}
        except QuitServer as quit_request:
            response = {"id": request["id"], "ok": True, "result": quit_request.result}
            quit_after = True
        connection.sendall(encode_frame(OPCODE_TEXT, json.dumps(response).encode("utf-8")))
        if quit_after:
            self.quit_requested.set()
        return quit_after
