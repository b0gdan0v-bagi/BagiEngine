#!/usr/bin/env python3
"""The bridge between a game in a browser page and the control tools: a page dials it, and it offers the usual control endpoint.

    python3 tools/control/bagi_bridge.py [--control-port 8765] [--page-port 8766] [--token T] [--origin URL ...]

It prints the query to open the page with. While a page is linked, BagiControlClient, GameSession.connect and the MCP server
reach it on the control port as they reach a desktop game. The link protocol and the security rules: .claude/rules/control-web-link.md.
Standard library only.

    bridge = Bridge(control_port=0, page_port=0)
    page_port = bridge.start()  # runs in a thread of its own
    control_port = bridge.wait_linked(timeout=30.0)
    ...
    bridge.stop()
"""

import argparse
import asyncio
import concurrent.futures
import contextlib
import functools
import hmac
import itertools
import json
import re
import secrets
import struct
import sys
import threading
import time
from typing import Callable, Iterable

from bagi_control_client import (
    DEFAULT_PORT,
    OPCODE_BINARY,
    OPCODE_CLOSE,
    OPCODE_CONTINUATION,
    OPCODE_PING,
    OPCODE_PONG,
    OPCODE_TEXT,
    FrameReader,
    accept_key,
    encode_frame,
)

HOST = "127.0.0.1"
DEFAULT_CONTROL_PORT = DEFAULT_PORT
DEFAULT_PAGE_PORT = 8766
LINK_VERSION = 1

# The limits of the engine's own server, so that a client sees no difference: ControlService::kMaxRequestBytes and
# WebSocketServer::kMaxConnections, kMaxOutboxBytes, kHandshakeTimeout
MAX_REQUEST_BYTES = 1024 * 1024
MAX_CLIENTS = 4
MAX_OUTBOX_BYTES = 16 * 1024 * 1024
HANDSHAKE_TIMEOUT_SECONDS = 5.0
MAX_PAGE_MESSAGE_BYTES = MAX_OUTBOX_BYTES  # an answer with its channel prefix
MAX_HELLO_BYTES = 1024  # all that is buffered for a peer that has shown no token
MAX_HEAD_BYTES = 8 * 1024
HELLO_TIMEOUT_SECONDS = 5.0
BUSY_HEAD_SECONDS = 0.1  # how long a client over the limit has to send its request before the 503
CLOSE_LINGER_SECONDS = 1.0  # how long a peer has to end the connection after a close frame or a refusal
START_TIMEOUT_SECONDS = 10.0
STOP_TIMEOUT_SECONDS = 10.0
IDLE_SECONDS = 1.0  # a bare join() cannot be interrupted on Windows

GENERATED_TOKEN_BYTES = 24
MAX_PORT = 65535

CLOSE_NORMAL = 1000
CLOSE_GOING_AWAY = 1001
CLOSE_PROTOCOL_ERROR = 1002
CLOSE_UNSUPPORTED_DATA = 1003
CLOSE_INVALID_PAYLOAD = 1007
CLOSE_POLICY_VIOLATION = 1008
CLOSE_TOO_BIG = 1009
CLOSE_INTERNAL_ERROR = 1011

REASON_BAD_TOKEN = "bad_token"
REASON_BAD_HELLO = "bad_hello"
REASON_BUSY = "busy"
REASON_CONTROL_PORT = "control_port"

_KNOWN_OPCODES = (OPCODE_CONTINUATION, OPCODE_TEXT, OPCODE_BINARY, OPCODE_CLOSE, OPCODE_PING, OPCODE_PONG)
_FIRST_CONTROL_OPCODE = OPCODE_CLOSE
_MAX_CONTROL_PAYLOAD = 125
_MAX_CLOSE_REASON = _MAX_CONTROL_PAYLOAD - 2
_RECV_CHUNK = 256 * 1024
_HEAD_CHUNK = 4096
_MAX_QUOTED = 100
_HTTP_REASONS = {400: "Bad Request", 403: "Forbidden", 426: "Upgrade Required", 431: "Request Header Fields Too Large", 503: "Service Unavailable"}

_TOKEN = re.compile(r"[A-Za-z0-9_-]{1,128}")
_CHANNEL = re.compile(rb"0|[1-9][0-9]{0,18}")
_CLIENT_KEY = re.compile(r"[A-Za-z0-9+/]{22}==")
_LOOPBACK_AUTHORITY = re.compile(r"(?:127\.0\.0\.1|localhost|\[::1\])(?::[0-9]{1,5})?", re.IGNORECASE | re.ASCII)
_ORIGIN = re.compile(r"(https?)://(\[[0-9a-f:.]+\]|[a-z0-9.-]+)(?::([0-9]{1,5}))?/?", re.IGNORECASE | re.ASCII)
_DEFAULT_PORTS = {"http": 80, "https": 443}
_SINGLE_HEADERS = ("host", "origin", "sec-websocket-key", "sec-websocket-version")
_LIST_HEADERS = ("upgrade", "connection")

Log = Callable[[str], None]


def is_token(text: str) -> bool:
    return _TOKEN.fullmatch(text) is not None


def parse_origin(text: str) -> tuple[str, str, int] | None:
    """`http(s)://host[:port]` as (scheme, host, port) with the default port filled in; None for anything else."""
    match = _ORIGIN.fullmatch(text)
    if match is None:
        return None
    scheme, host = match.group(1).lower(), match.group(2).lower()
    port = int(match.group(3)) if match.group(3) is not None else _DEFAULT_PORTS[scheme]
    return (scheme, host, port) if 0 < port <= MAX_PORT else None


def _is_loopback_origin(origin: str) -> bool:
    for scheme in ("http://", "https://"):
        if origin.startswith(scheme):
            return _LOOPBACK_AUTHORITY.fullmatch(origin[len(scheme) :].split("/", 1)[0]) is not None
    return False


def _quoted(text: str) -> str:
    """A text of the peer as a message may repeat it: printable ASCII, cut short."""
    printable = "".join(letter if " " <= letter <= "~" else "?" for letter in text[:_MAX_QUOTED])
    return printable + ("..." if len(text) > _MAX_QUOTED else "")


def _is_close_code(code: int) -> bool:
    return 1000 <= code <= 1003 or 1007 <= code <= 1014 or 3000 <= code <= 4999


class _Refused(Exception):
    """A handshake that gets an HTTP error instead of the 101."""

    def __init__(self, status: int, message: str):
        super().__init__(message)
        self.status = status
        self.message = message


class _Violation(Exception):
    """Bytes that end the connection with the close code `code`."""

    def __init__(self, code: int, reason: str):
        super().__init__(reason)
        self.code = code
        self.reason = reason


class _MessageReader(FrameReader):
    """The messages of a client's byte stream. A frame is judged by its header, so one that breaks a rule of RFC 6455 or
    announces a message over `limit` bytes is refused before its payload is buffered."""

    def __init__(self, limit: int) -> None:
        super().__init__()
        self.limit = limit
        self._fragments = bytearray()
        self._fragmented_opcode: int | None = None

    def pop_message(self) -> tuple[int, bytes] | None:
        """The next whole message or control frame as (opcode, payload); None when more bytes are needed. Raises _Violation."""
        while True:
            self._check_header()
            frame = self.pop()
            if frame is None:
                return None
            fin, opcode, payload = frame
            if opcode >= _FIRST_CONTROL_OPCODE:
                return opcode, payload
            if opcode != OPCODE_CONTINUATION:
                if fin:
                    return opcode, payload
                self._fragmented_opcode = opcode
            self._fragments += payload
            if fin:
                message = (self._fragmented_opcode, bytes(self._fragments))
                self._fragments = bytearray()
                self._fragmented_opcode = None
                return message

    def _check_header(self) -> None:
        head = self._buffer  # the bytes FrameReader has not cut into frames yet
        if len(head) < 2:
            return
        fin = bool(head[0] & 0x80)
        opcode = head[0] & 0x0F
        if head[0] & 0x70:
            raise _Violation(CLOSE_PROTOCOL_ERROR, "reserved bits are set")
        if opcode not in _KNOWN_OPCODES:
            raise _Violation(CLOSE_PROTOCOL_ERROR, "unknown opcode")
        if not head[1] & 0x80:
            raise _Violation(CLOSE_PROTOCOL_ERROR, "the frames of a client have to be masked")
        length = head[1] & 0x7F
        if opcode >= _FIRST_CONTROL_OPCODE:
            if not fin or length > _MAX_CONTROL_PAYLOAD:
                raise _Violation(CLOSE_PROTOCOL_ERROR, "a control frame is fragmented or too long")
            return
        if (opcode == OPCODE_CONTINUATION) != (self._fragmented_opcode is not None):
            raise _Violation(CLOSE_PROTOCOL_ERROR, "a fragment out of order")
        extended = {126: 2, 127: 8}.get(length)
        if extended is not None:
            if len(head) < 2 + extended:
                return
            length = int.from_bytes(head[2 : 2 + extended], "big")
        if len(self._fragments) + length > self.limit:
            raise _Violation(CLOSE_TOO_BIG, "the message is too long")


class _Connection:
    """One upgraded WebSocket, the bridge being its server. `on_closing` runs once, when the bridge decides to end it."""

    def __init__(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, limit: int, log: Log):
        self.writer = writer
        self.frames = _MessageReader(limit)
        self.closing = False
        self.on_closing: Callable[[], None] | None = None
        self._reader = reader
        self._log = log

    async def receive(self) -> tuple[int, bytes] | None:
        """The next data message as (opcode, payload); None once the connection is over. Control frames and broken
        frames are dealt with here, and after a close frame of ours whatever the peer still sends is discarded."""
        while True:
            message = None
            if not self.closing:
                try:
                    message = self.frames.pop_message()
                except _Violation as violation:
                    self._log(f"closing a connection that broke the protocol: {violation.reason}")
                    self.close(violation.code, violation.reason)
                    continue
            if message is None:
                try:
                    data = await self._reader.read(_RECV_CHUNK)
                except OSError:
                    return None
                if not data:
                    return None
                if not self.closing:
                    self.frames.feed(data)
                continue
            opcode, payload = message
            if opcode == OPCODE_PING:
                self.send(encode_frame(OPCODE_PONG, payload))
            elif opcode == OPCODE_CLOSE:
                self._answer_close(payload)
            elif opcode != OPCODE_PONG:
                return message

    def write(self, frame: bytes) -> bool:
        """Queues `frame` whatever is already queued; False when the connection is ending."""
        if self.closing or self.writer.transport.is_closing():
            return False
        self.writer.write(frame)
        return True

    def send(self, frame: bytes) -> None:
        """Queues `frame` without waiting; a peer that has too much unread is dropped instead."""
        if self.writer.transport.get_write_buffer_size() + len(frame) > MAX_OUTBOX_BYTES:
            self._log("dropping a connection that does not read")
            self.abort()
            return
        self.write(frame)

    def close(self, code: int, reason: str = "") -> None:
        if self.closing:
            return
        transport = self.writer.transport
        if self.write(encode_frame(OPCODE_CLOSE, struct.pack("!H", code) + reason.encode("ascii")[:_MAX_CLOSE_REASON])):
            with contextlib.suppress(OSError):
                self.writer.write_eof()
            asyncio.get_running_loop().call_later(CLOSE_LINGER_SECONDS, transport.abort)
        self._start_closing()

    def abort(self) -> None:
        self.writer.transport.abort()
        if not self.closing:
            self._start_closing()

    def _start_closing(self) -> None:
        self.closing = True
        if self.on_closing is not None:
            self.on_closing()

    def _answer_close(self, payload: bytes) -> None:
        if not payload:
            self.close(CLOSE_NORMAL)
        elif len(payload) >= 2 and _is_close_code(int.from_bytes(payload[:2], "big")):
            self.close(int.from_bytes(payload[:2], "big"))
        else:
            self.close(CLOSE_PROTOCOL_ERROR, "a bad close frame")


async def _read_head(reader: asyncio.StreamReader) -> tuple[bytes, bytes]:
    """The head of an HTTP request and the bytes that came after it."""
    data = bytearray()
    while (end := data.find(b"\r\n\r\n")) < 0 and len(data) <= MAX_HEAD_BYTES:
        chunk = await reader.read(_HEAD_CHUNK)
        if not chunk:
            raise ConnectionResetError("the peer left during the handshake")
        data += chunk
    if end < 0 or end > MAX_HEAD_BYTES:
        raise _Refused(431, "the request head is too long")
    return bytes(data[:end]), bytes(data[end + 4 :])


def _check_request(head: bytes, admits_origin: Callable[[str | None], bool]) -> str:
    """The Sec-WebSocket-Key of an opening handshake that may connect; raises _Refused. The checks of the engine's server
    (ParseHandshakeRequest), with the judgement of `Origin` left to the side that was dialled."""
    request, *lines = [line.removesuffix("\r") for line in head.decode("latin-1").split("\n")]
    if re.fullmatch(r"GET .+ HTTP/1\.1", request) is None:
        raise _Refused(400, "expected a GET request over HTTP/1.1")
    headers: dict[str, str] = {}
    for line in lines:
        name, colon, value = line.partition(":")
        if not colon or not name or name != name.strip():
            raise _Refused(400, "a malformed header line")
        name, value = name.lower(), value.strip(" \t")
        if name not in headers:
            headers[name] = value
        elif name in _LIST_HEADERS:
            headers[name] += "," + value
        elif name in _SINGLE_HEADERS:
            raise _Refused(400, f"the header '{name}' is repeated")

    host = headers.get("host")
    if host is None:
        raise _Refused(400, "the Host header is missing")
    if _LOOPBACK_AUTHORITY.fullmatch(host) is None:
        raise _Refused(403, f"the Host '{_quoted(host)}' is not the loopback")
    origin = headers.get("origin")
    if not admits_origin(origin):
        raise _Refused(403, "an Origin header is required" if origin is None else f"the Origin '{_quoted(origin)}' is not allowed")

    for name, token in (("upgrade", "websocket"), ("connection", "upgrade")):
        if token not in (item.strip().lower() for item in headers.get(name, "").split(",")):
            raise _Refused(400, f"expected the header '{name}' with '{token}'")
    if headers.get("sec-websocket-version") != "13":
        raise _Refused(426, "this server speaks WebSocket version 13")
    key = headers.get("sec-websocket-key", "")
    if _CLIENT_KEY.fullmatch(key) is None:
        raise _Refused(400, "'Sec-WebSocket-Key' is missing or is not 16 bytes in base64")
    return key


async def _discard(reader: asyncio.StreamReader) -> None:
    while await reader.read(_RECV_CHUNK):
        pass


async def _refuse(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, refusal: _Refused) -> None:
    body = refusal.message.encode("utf-8")
    versions = "Sec-WebSocket-Version: 13\r\n" if refusal.status == 426 else ""
    head = (
        f"HTTP/1.1 {refusal.status} {_HTTP_REASONS[refusal.status]}\r\nConnection: close\r\n{versions}"
        f"Content-Type: text/plain\r\nContent-Length: {len(body)}\r\n\r\n"
    )
    writer.write(head.encode("ascii") + body)
    # Closing over unread bytes is a reset, which would take the answer with it
    with contextlib.suppress(OSError, asyncio.TimeoutError):
        writer.write_eof()
        await asyncio.wait_for(_discard(reader), CLOSE_LINGER_SECONDS)


async def _upgrade(
    reader: asyncio.StreamReader, writer: asyncio.StreamWriter, admits_origin: Callable[[str | None], bool], limit: int, log: Log
) -> _Connection | None:
    """The opening handshake; None when the peer was refused or left."""
    try:
        head, rest = await asyncio.wait_for(_read_head(reader), HANDSHAKE_TIMEOUT_SECONDS)
        key = _check_request(head, admits_origin)
    except _Refused as refusal:
        log(f"refused a connection with HTTP {refusal.status}: {refusal.message}")
        await _refuse(reader, writer, refusal)
        return None
    except (OSError, asyncio.TimeoutError):
        return None
    response = f"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept_key(key)}\r\n\r\n"
    writer.write(response.encode("ascii"))
    connection = _Connection(reader, writer, limit, log)
    connection.frames.feed(rest)
    return connection


class _Link:
    """The linked page and the control side, which exists only while the page is linked."""

    def __init__(self, page: _Connection):
        self.page = page
        self.server: asyncio.AbstractServer | None = None
        self.writers: set[asyncio.StreamWriter] = set()  # every control connection, the handshakes included
        self.clients: dict[int, _Connection] = {}  # by channel, the upgraded ones
        self._page_drain = asyncio.Lock()  # a StreamWriter takes one drain() at a time before Python 3.12

    async def forward(self, message: bytes) -> None:
        """Sends `message` to the page and waits while the page is behind, which holds the sending client back."""
        async with self._page_drain:
            if self.page.write(encode_frame(OPCODE_TEXT, message)):
                with contextlib.suppress(OSError):
                    await self.page.writer.drain()

    def deliver(self, channel: int, text: bytes) -> None:
        """A message of the page: an answer for one channel, or with channel 0 a push for every client."""
        frame = encode_frame(OPCODE_TEXT, text)
        if channel == 0:
            for client in list(self.clients.values()):
                client.send(frame)
        elif channel in self.clients:
            self.clients[channel].send(frame)

    def close_control(self) -> None:
        if self.server is not None:
            self.server.close()
        upgraded = {client.writer for client in self.clients.values()}
        for client in list(self.clients.values()):
            client.close(CLOSE_GOING_AWAY, "the page left")
        for writer in self.writers - upgraded:
            writer.transport.abort()


class Bridge:
    """The bridge in a thread of its own. `token` is made up when none is given; port 0 takes a free port (for the
    control port a new one at every link). `origins` are the pages admitted besides those of the loopback. `log` gets a
    line per event, from the bridge's thread; no line holds the token."""

    def __init__(
        self,
        token: str | None = None,
        control_port: int = DEFAULT_CONTROL_PORT,
        page_port: int = DEFAULT_PAGE_PORT,
        origins: Iterable[str] = (),
        log: Log | None = None,
    ):
        if token is not None and not is_token(token):
            raise ValueError("a token is 1 to 128 of A-Z a-z 0-9 _ -")
        parsed = [parse_origin(origin) for origin in origins]
        if None in parsed:
            raise ValueError("an origin is http(s)://host[:port]")
        self.token = token if token is not None else secrets.token_urlsafe(GENERATED_TOKEN_BYTES)
        self.page_port = page_port
        self._control_port = control_port
        self._origins = set(parsed)
        self._log: Log = log or (lambda line: None)
        self._channels = itertools.count(1)
        self._link: _Link | None = None
        self._linked_port: int | None = None
        self._changed = threading.Condition()
        self._thread: threading.Thread | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._stopping: asyncio.Event | None = None
        self._tasks: set[asyncio.Task] = set()
        self._page_writers: set[asyncio.StreamWriter] = set()

    @property
    def page_query(self) -> str:
        """What to append to the page's address; after start(), which knows the page port."""
        return f"?controlToken={self.token}&control={self.page_port}"

    def start(self) -> int:
        """Listens for the page and gives the port. Raises OSError when the page port or the control port is taken."""
        ready: concurrent.futures.Future = concurrent.futures.Future()
        self._thread = threading.Thread(target=lambda: asyncio.run(self._run(ready)), name="bagi-bridge", daemon=True)
        self._thread.start()
        self.page_port = ready.result(START_TIMEOUT_SECONDS)
        return self.page_port

    def wait_linked(self, timeout: float) -> int | None:
        """The control port once a page is linked and the port listens; None after `timeout` seconds."""
        with self._changed:
            self._changed.wait_for(lambda: self._linked_port is not None, timeout)
            return self._linked_port

    def wait_unlinked(self, timeout: float) -> bool:
        """True once no page is linked, so that nothing listens on the control port; False after `timeout` seconds."""
        with self._changed:
            return self._changed.wait_for(lambda: self._linked_port is None, timeout)

    def stop(self) -> None:
        """Closes every connection with 1001 and ends the thread. Safe to call twice and when never started."""
        if self._thread is None or not self._thread.is_alive():
            return
        assert self._loop is not None and self._stopping is not None
        with contextlib.suppress(RuntimeError):  # the loop has just closed
            self._loop.call_soon_threadsafe(self._stopping.set)
        self._thread.join(STOP_TIMEOUT_SECONDS)

    async def _run(self, ready: concurrent.futures.Future) -> None:
        self._loop = asyncio.get_running_loop()
        self._stopping = asyncio.Event()
        try:
            # Bound and let go at once: the port listens only while a page is linked, but a taken one is said at the start
            (await self._listen(self._control_port, self._serve_nobody, start_serving=False)).close()
            page_server = await self._listen(self.page_port, self._serve_page)
        except Exception as error:  # whatever it is, start() has to raise it instead of waiting
            ready.set_exception(error)
            return
        ready.set_result(page_server.sockets[0].getsockname()[1])
        await self._stopping.wait()
        page_server.close()
        closed_politely = set()
        if self._link is not None:
            page = self._link.page
            closed_politely.add(page.writer)
            page.close(CLOSE_GOING_AWAY, "the bridge is stopping")  # which unlinks it
        for writer in self._page_writers - closed_politely:
            writer.transport.abort()
        if self._tasks:
            await asyncio.wait(self._tasks, timeout=STOP_TIMEOUT_SECONDS / 2)

    @staticmethod
    async def _listen(port: int, serve: Callable, start_serving: bool = True) -> asyncio.AbstractServer:
        try:
            return await asyncio.start_server(serve, HOST, port, start_serving=start_serving)
        except OSError as error:
            raise OSError(f"cannot listen on {HOST}:{port}: {error.strerror or error}") from error

    @staticmethod
    async def _serve_nobody(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        writer.transport.abort()

    @contextlib.contextmanager
    def _connection_task(self, writer: asyncio.StreamWriter):
        """Lets stop() wait for the handler of a connection, and ends the connection with it."""
        task = asyncio.current_task()
        self._tasks.add(task)
        try:
            yield
        finally:
            writer.transport.abort()
            self._tasks.discard(task)

    def _admits_page(self, origin: str | None) -> bool:
        return origin is not None and (_is_loopback_origin(origin) or parse_origin(origin) in self._origins)

    @staticmethod
    def _admits_client(origin: str | None) -> bool:
        return origin is None or _is_loopback_origin(origin)

    def _hello_refusal(self, opcode: int, payload: bytes) -> str | None:
        """Why the first message of a page does not link it; None when it does."""
        if opcode != OPCODE_TEXT:
            return REASON_BAD_HELLO
        try:
            hello = json.loads(payload.decode("utf-8"))
        except (ValueError, RecursionError):
            return REASON_BAD_HELLO
        if not isinstance(hello, dict) or set(hello) != {"bagiControlLink", "token"}:
            return REASON_BAD_HELLO
        version, token = hello["bagiControlLink"], hello["token"]
        if type(version) is not int or version != LINK_VERSION or not isinstance(token, str):
            return REASON_BAD_HELLO
        if not is_token(token) or not hmac.compare_digest(token, self.token):
            return REASON_BAD_TOKEN
        return None

    async def _serve_page(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        with self._connection_task(writer):
            self._page_writers.add(writer)
            try:
                page = await _upgrade(reader, writer, self._admits_page, MAX_HELLO_BYTES, self._log)
                if page is not None:
                    await self._run_page(page)
            finally:
                self._page_writers.discard(writer)

    async def _run_page(self, page: _Connection) -> None:
        try:
            hello = await asyncio.wait_for(page.receive(), HELLO_TIMEOUT_SECONDS)
        except asyncio.TimeoutError:
            refusal = REASON_BAD_HELLO
        else:
            if hello is None:
                return
            refusal = self._hello_refusal(*hello)
        if refusal is None and self._link is not None:
            refusal = REASON_BUSY
        if refusal is not None:
            self._log(f"refused a page: {refusal}")
            page.close(CLOSE_POLICY_VIOLATION, refusal)
            await self._drain_closed(page)
            return

        link = _Link(page)
        self._link = link  # taken before the first await, so that a second page is busy
        page.on_closing = functools.partial(self._unlink, link)
        try:
            try:
                link.server = await self._listen(self._control_port, functools.partial(self._serve_client, link))
            except OSError as error:
                self._log(str(error))
                page.close(CLOSE_INTERNAL_ERROR, REASON_CONTROL_PORT)
                await self._drain_closed(page)
                return
            if self._link is not link:  # the bridge was stopped meanwhile
                link.server.close()
                return
            port = link.server.sockets[0].getsockname()[1]
            page.frames.limit = MAX_PAGE_MESSAGE_BYTES
            self._publish(port)
            self._log(f"page linked: the control API is on {HOST}:{port}")
            while (message := await page.receive()) is not None:
                opcode, payload = message
                channel, space, text = payload.partition(b" ")
                if opcode != OPCODE_TEXT or not space or _CHANNEL.fullmatch(channel) is None:
                    self._log("closing the page: a message that is not '<channel> <text>'")
                    page.close(CLOSE_PROTOCOL_ERROR, "bad_message")
                else:
                    link.deliver(int(channel), text)
        finally:
            self._unlink(link)

    @staticmethod
    async def _drain_closed(connection: _Connection) -> None:
        """Reads a connection that was closed to its end, so that the close frame is not lost to a reset."""
        while await connection.receive() is not None:
            pass

    def _unlink(self, link: _Link) -> None:
        if self._link is not link:
            return
        self._link = None
        link.close_control()
        self._publish(None)
        if link.server is not None:
            self._log("page unlinked: the control port is closed")

    def _publish(self, port: int | None) -> None:
        with self._changed:
            self._linked_port = port
            self._changed.notify_all()

    async def _serve_client(self, link: _Link, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        with self._connection_task(writer):
            if self._link is not link:
                return
            if len(link.writers) >= MAX_CLIENTS:
                self._log("refused a control client: too many connections")
                with contextlib.suppress(OSError, asyncio.TimeoutError, _Refused):
                    await asyncio.wait_for(_read_head(reader), BUSY_HEAD_SECONDS)
                await _refuse(reader, writer, _Refused(503, "too many control connections"))
                return
            link.writers.add(writer)
            try:
                client = await _upgrade(reader, writer, self._admits_client, MAX_REQUEST_BYTES, self._log)
                if client is not None and self._link is link:
                    await self._run_client(link, client)
            finally:
                link.writers.discard(writer)

    async def _run_client(self, link: _Link, client: _Connection) -> None:
        channel = next(self._channels)
        link.clients[channel] = client  # no await since the 101: a push cannot miss a client that has seen it
        try:
            while (message := await client.receive()) is not None:
                opcode, payload = message
                if opcode != OPCODE_TEXT:
                    client.close(CLOSE_UNSUPPORTED_DATA, "binary messages are not supported")
                    continue
                try:
                    payload.decode("utf-8")
                except UnicodeDecodeError:
                    # A browser ends a connection that brings it such text: it must not reach the page
                    client.close(CLOSE_INVALID_PAYLOAD, "the text is not UTF-8")
                    continue
                await link.forward(b"%d %s" % (channel, payload))
        finally:
            del link.clients[channel]
            if self._link is link:
                await link.forward(b"%d" % channel)


def _port_argument(text: str) -> int:
    if not text.isdecimal() or not 0 < int(text) <= MAX_PORT:
        raise argparse.ArgumentTypeError(f"a port is 1 to {MAX_PORT}")
    return int(text)


def _token_argument(text: str) -> str:
    if not is_token(text):
        raise argparse.ArgumentTypeError("a token is 1 to 128 of A-Z a-z 0-9 _ -")  # argparse does not repeat the value
    return text


def _origin_argument(text: str) -> str:
    if parse_origin(text) is None:
        raise argparse.ArgumentTypeError(f"'{text}' is not http(s)://host[:port]")
    return text


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Offers the control API of a game that runs in a browser page.")
    parser.add_argument("--control-port", type=_port_argument, default=DEFAULT_CONTROL_PORT, help="where control clients connect (default %(default)s)")
    parser.add_argument("--page-port", type=_port_argument, default=DEFAULT_PAGE_PORT, help="where the page dials (default %(default)s)")
    parser.add_argument("--token", type=_token_argument, help="the secret the page has to show (default: a random one)")
    parser.add_argument(
        "--origin", type=_origin_argument, action="extend", nargs="+", default=[], metavar="URL", help="admit pages of these origins besides those of the loopback"
    )
    args = parser.parse_args(argv)
    if args.control_port == args.page_port:
        parser.error("--control-port and --page-port have to differ")

    bridge = Bridge(args.token, args.control_port, args.page_port, args.origin, log=lambda line: print(f"bagi_bridge: {line}", file=sys.stderr, flush=True))
    try:
        bridge.start()
    except OSError as error:
        parser.exit(2, f"bagi_bridge: {error}\n")
    print(f"open the page with the query {bridge.page_query}", flush=True)
    print(f"the control API listens on {HOST}:{args.control_port} while a page is linked; Ctrl+C stops the bridge", flush=True)
    try:
        while True:
            time.sleep(IDLE_SECONDS)
    except KeyboardInterrupt:
        bridge.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
