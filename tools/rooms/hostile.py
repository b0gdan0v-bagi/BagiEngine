#!/usr/bin/env python3
"""A hostile client of the room server: starts BagiRoomServer and attacks it over real sockets with frames built by hand.

    python tools/rooms/hostile.py --exe <path of the built BagiRoomServer>
    python tools/rooms/hostile.py --preset macos-debug [--only frames] [--server-arg=--max-per-address=0]

This file is the one place of the tools with hand-built WebSocket frames: `BagiControlClient` (what smoke.py plays with) refuses to send
what an attacker sends (an unmasked frame, an oversized one, fragments, one byte at a time, a handshake that never ends). The honest players,
the judge and the server process are the ones of smoke.py.

Every scenario starts a fresh server on --port 0 and a good table (the `cards` game, two humans and two computers, judged on every text) that
plays while the attack goes on. At the end the table is finished with no error and no hidden card on any wire, the server answers a new
client, and it exits 0 on SIGTERM. The scenarios are in SCENARIOS; the limit ones start the server with the flag of their limit
(`--only <name> --server-arg=<flag>` switches a limit off or moves it by hand: the scenario then has to fail).
Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
"""

import argparse
import base64
import json
import os
import select
import socket
import struct
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

sys.path.insert(0, str(Path(__file__).resolve().parent))

import smoke  # noqa: E402
from smoke import (  # noqa: E402
    EXIT_FAILED,
    EXIT_PASSED,
    EXIT_SKIPPED,
    IO_TIMEOUT_SECONDS,
    PROTOCOL_VERSION,
    SERVER_NAME,
    Human,
    RoomServer,
    SmokeFailure,
    check,
    open_room,
    play_to_the_end,
    run_until,
)
from bagi_control_client import FrameReader, HandshakeError, ConnectionClosed, accept_key  # noqa: E402
from game_process import preset_executable  # noqa: E402

OPCODE_CONTINUATION = 0x0
OPCODE_TEXT = 0x1
OPCODE_BINARY = 0x2
OPCODE_CLOSE = 0x8
OPCODE_PING = 0x9
OPCODE_PONG = 0xA

CLOSE_PROTOCOL_ERROR = 1002
CLOSE_UNSUPPORTED_DATA = 1003
CLOSE_POLICY_VIOLATION = 1008
CLOSE_TOO_BIG = 1009

HTTP_SWITCHING = 101
HTTP_HEADER_TOO_LARGE = 431
HTTP_BUSY = 503

MAX_MESSAGE_BYTES = 16 * 1024  # RoomLimits::kMaxMessageBytes
HANDSHAKE_DEADLINE_SECONDS = 5.0  # WebSocketServer::kHandshakeTimeout
HANDSHAKE_CUT_LIMIT_SECONDS = 9.0  # the deadline and the time to notice it, with room for a loaded machine
FRAME_MASK = b"\x37\xfa\x21\x3d"
SHORT_WAIT_SECONDS = 0.02
REPLY_LIMIT_SECONDS = 5.0
TRICKLE_PAUSE_SECONDS = 0.002
COMPUTER_DELAY_MS = 100  # slow enough that the good table is still playing when the first attacks run
PER_ADDRESS_CAP = 32  # the default of --max-per-address
FLOOD_MESSAGES = 2000
FLOOD_BATCH = 25
SILENT_SOCKETS = 40
DROPPED_SOCKETS = 100
OVERSIZED_HEAD_BYTES = 100 * 1024
HUGE_DECLARED_LENGTH = 1 << 40
ROOMS_PER_ADDRESS_FLAG = 2
LOBBY_TIMEOUT_MS = 500
LOBBY_TIMEOUT_LIMIT_SECONDS = 8.0
LOBBY_TIMEOUT_REASON = "lobby_timeout"
FAILED_JOINS_IN_A_ROW = 5  # RoomLimits::kMaxFailedJoinsInRow
ADDRESS_JOIN_BURST = 10  # RoomLimits::kAddressJoinBurst
PROXY_PER_ADDRESS_FLAG = 3
PROXY_FORWARDED_NET = "203.0.113"  # TEST-NET-3, RFC 5737
GUESS_LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ"
ROOM_CODE_LENGTH = 5


def mask_payload(payload: bytes, mask: bytes) -> bytes:
    return bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))


def build_frame(opcode: int, payload: bytes = b"", *, fin: bool = True, masked: bool = True, mask: bytes = FRAME_MASK, declared_length: int | None = None) -> bytes:
    """One WebSocket frame written by hand. `declared_length` puts another length in the header than the payload has (a frame whose body never comes)."""
    length = len(payload) if declared_length is None else declared_length
    first = (0x80 if fin else 0) | opcode
    mask_bit = 0x80 if masked else 0
    if length < 126:
        header = struct.pack("!BB", first, mask_bit | length)
    elif length <= 0xFFFF:
        header = struct.pack("!BBH", first, mask_bit | 126, length)
    else:
        header = struct.pack("!BBQ", first, mask_bit | 127, length)
    if not masked:
        return header + payload
    return header + mask + mask_payload(payload, mask)


def message_bytes(message: dict, padding_to: int = 0) -> bytes:
    """A JSON message of the protocol as bytes; trailing spaces bring it to `padding_to` bytes (JSON allows them)."""
    text = json.dumps({"v": PROTOCOL_VERSION, **message}).encode()
    return text + b" " * max(0, padding_to - len(text))


def split_fragments(payload: bytes, sizes: list[int], opcode: int = OPCODE_TEXT) -> list[bytes]:
    """`payload` as masked frames of the given sizes: the first has `opcode`, the others are continuations, only the last has FIN."""
    check(sum(sizes) == len(payload), "the fragment sizes do not add up to the payload")
    frames = []
    start = 0
    for index, size in enumerate(sizes):
        frames.append(build_frame(opcode if index == 0 else OPCODE_CONTINUATION, payload[start : start + size], fin=index == len(sizes) - 1))
        start += size
    return frames


def write_one_byte_at_a_time(connection: "socket.socket", data: bytes, pause: float = TRICKLE_PAUSE_SECONDS) -> None:
    """Every byte is a send of its own (TCP_NODELAY is set by the caller), with a pause between them."""
    for index in range(len(data)):
        connection.sendall(data[index : index + 1])
        time.sleep(pause)


@dataclass
class Closing:
    """How a connection ended: the code and reason of its close frame, or None when the stream just ended."""

    code: int | None
    reason: str


class RawClient:
    """A WebSocket client that writes its handshake and its frames itself and reads what the server sends frame by frame."""

    def __init__(self, port: int):
        self.port = port
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=IO_TIMEOUT_SECONDS)
        self.socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.reader = FrameReader()
        self.texts: list[dict] = []
        self.pongs = 0
        self.closing: Closing | None = None
        self.ended = False
        self._cursor = 0

    def handshake(self, headers: list[str] | None = None) -> int:
        """The upgrade request with the extra header lines; returns the HTTP status. On 101 the client is ready for frames."""
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        lines = ["GET /control HTTP/1.1", f"Host: 127.0.0.1:{self.port}", "Upgrade: websocket", "Connection: Upgrade", f"Sec-WebSocket-Key: {key}", "Sec-WebSocket-Version: 13", *(headers or [])]
        self.socket.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("ascii"))
        data = b""
        deadline = time.monotonic() + IO_TIMEOUT_SECONDS
        while b"\r\n\r\n" not in data:
            check(time.monotonic() < deadline, "the answer to a handshake never ended")
            try:
                chunk = self.socket.recv(4096)
            except OSError:
                chunk = b""
            if not chunk:
                self.ended = True
                return 0  # the stream ended without an answer
            data += chunk
        head, _, rest = data.partition(b"\r\n\r\n")
        status = int(head.split(b" ", 2)[1])
        if status == HTTP_SWITCHING:
            check(accept_key(key).encode() in head, "the Sec-WebSocket-Accept value is wrong")
            self.reader.feed(rest)
        else:
            self.ended = True  # a refusal is followed by the end of the stream
        return status

    def send(self, data: bytes) -> bool:
        """False when the server has already closed the connection (the write failed)."""
        try:
            self.socket.sendall(data)
        except OSError:
            self.ended = True
            return False
        return True

    def send_message(self, message: dict) -> bool:
        return self.send(build_frame(OPCODE_TEXT, message_bytes(message)))

    def fill(self, timeout: float = SHORT_WAIT_SECONDS) -> None:
        """Reads what has arrived (waits `timeout` for the first byte) and sorts the complete frames."""
        if not self.ended:
            self.socket.settimeout(timeout)
            try:
                data = self.socket.recv(64 * 1024)
            except (socket.timeout, BlockingIOError):
                data = None
            except OSError:
                data = b""
            if data == b"":
                self.ended = True
            elif data:
                self.reader.feed(data)
        while (frame := self.reader.pop()) is not None:
            _fin, opcode, payload = frame
            if opcode == OPCODE_TEXT:
                self.texts.append(json.loads(payload))
            elif opcode == OPCODE_PONG:
                self.pongs += 1
            elif opcode == OPCODE_CLOSE and self.closing is None:
                code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else None
                self.closing = Closing(code, payload[2:].decode(errors="replace"))

    def is_closed(self) -> bool:
        return self.ended or self.closing is not None

    def wait_closed(self, limit: float = REPLY_LIMIT_SECONDS, pump: Callable[[], None] | None = None) -> Closing:
        """The close frame the server sent (a stream that ends first is a Closing with no code)."""
        deadline = time.monotonic() + limit
        while self.closing is None and not self.ended:
            check(time.monotonic() < deadline, f"the server did not close the connection in {limit} s; it sent {self.texts}")
            if pump:
                pump()
            self.fill()
        self.fill(0.0)
        return self.closing or Closing(None, "")

    def wait_text(self, kind: str, limit: float = REPLY_LIMIT_SECONDS, pump: Callable[[], None] | None = None) -> dict:
        """The next text of `kind` after the ones an earlier wait_text returned."""
        deadline = time.monotonic() + limit
        while True:
            while self._cursor < len(self.texts):
                message = self.texts[self._cursor]
                self._cursor += 1
                if message.get("type") == kind:
                    return message
            check(not self.is_closed(), f"the connection ended before a '{kind}' arrived (closing {self.closing}); got {[m.get('type') for m in self.texts]}")
            check(time.monotonic() < deadline, f"no '{kind}' arrived in {limit} s; got {[m.get('type') for m in self.texts]}")
            if pump:
                pump()
            self.fill()

    def wait_pong_frame(self, limit: float = REPLY_LIMIT_SECONDS) -> None:
        deadline = time.monotonic() + limit
        while self.pongs == 0:
            check(time.monotonic() < deadline and not self.is_closed(), "no pong frame arrived")
            self.fill()

    def close(self) -> None:
        try:
            self.socket.close()
        except OSError:
            pass


class GoodTable:
    """The honest table that plays while the attack goes on: two humans of smoke.py on the `cards` game and two computers."""

    def __init__(self, port: int):
        self.humans = list(open_room(port, "cards"))
        self.room = self.humans[0].room
        self.humans[0].send({"type": "start"})

    def pump(self) -> None:
        for human in self.humans:
            human.poll()
            human.act_if_its_turn()

    def finish(self) -> None:
        play_to_the_end(self.humans)
        smoke.check_no_error(self.humans)
        for human in self.humans:
            check(not human.leaks, f"seat {human.seat} of the good table was sent hidden cards: {human.leaks}")
            human.close()


def expect_error(human: Human, codes: set[str], what: str) -> dict:
    error = human.wait_for("error")
    check(error.get("code") in codes, f"{what}: the answer was {error}, not one of {sorted(codes)}")
    return error


def expect_raw_error(client: RawClient, codes: set[str], what: str, pump: Callable[[], None] | None = None) -> dict:
    error = client.wait_text("error", pump=pump)
    check(error.get("code") in codes, f"{what}: the answer was {error}, not one of {sorted(codes)}")
    return error


def expect_pong(client: RawClient, what: str) -> None:
    client.wait_text("pong")
    check(client.closing is None, f"{what}: the connection was closed ({client.closing})")


def expect_closing(client: RawClient, code: int, what: str, reason: str | None = None) -> None:
    closing = client.wait_closed()
    check(closing.code == code, f"{what}: the connection ended with {closing}, not close {code}")
    if reason is not None:
        check(closing.reason == reason, f"{what}: the close reason is '{closing.reason}', not '{reason}'")


def open_client(port: int, headers: list[str] | None = None) -> RawClient:
    client = RawClient(port)
    status = client.handshake(headers)
    check(status == HTTP_SWITCHING, f"the handshake got HTTP {status}, not {HTTP_SWITCHING}")
    return client


def create_room_as(port: int, name: str) -> Human:
    human = Human(port)
    human.send({"type": "create_room", "game": "cards", "name": name, "options": {}})
    human.wait_for("joined")
    human.wait_for("lobby")
    return human


def guess(port: int, code: str, name: str = "Guesser") -> tuple[Human, dict]:
    """A new socket that tries to join `code`; the connection and the answer (an error, or the `joined` that must not come)."""
    human = Human(port)
    human.send({"type": "join_room", "code": code, "name": name})
    return human, human.wait_for("error")


def wrong_codes(count: int, avoid: str) -> list[str]:
    """`count` well-formed codes (5 letters of the alphabet) that are not `avoid`."""
    codes = [letter * ROOM_CODE_LENGTH for letter in GUESS_LETTERS]
    return [code for code in codes if code != avoid][:count]


def scenario_seats(server: RoomServer, table: GoodTable) -> None:
    """choose_seat with a number no seat has, in the lobby of a room of its own."""
    attacker = create_room_as(server.port, "Evil")
    bad_numbers = [-1, 4, 2**63, -(2**63) - 1, 1.5, "1", None, True]
    for number in bad_numbers:
        attacker.send({"type": "choose_seat", "seat": number})
        expect_error(attacker, {"invalid_seat", "bad_message"}, f"choose_seat {number!r}")
        table.pump()
    attacker.send({"type": "ping"})
    attacker.wait_for("pong")
    check(not any(m.get("type") == "joined" for m in attacker.messages[1:]), "a bad seat number moved the player")
    attacker.close()


def scenario_codes(server: RoomServer, table: GoodTable) -> None:
    """Wrong and malformed room codes, each from a socket of its own (a socket is closed after five failed joins in a row)."""
    attempts = [(code, {"unknown_room"}) for code in wrong_codes(5, table.room)]
    attempts += [(wrong_codes(1, table.room)[0].lower(), {"unknown_room"}), ("A" * 1000, {"unknown_room", "bad_message"}), ("AA\u0000AA", {"unknown_room", "bad_message"})]
    for code, allowed in attempts:
        human, answer = guess(server.port, code)
        check(answer.get("code") in allowed, f"join_room with the code {code[:12]!r} ({len(code)} characters) got {answer}")
        check(not any(m.get("type") == "joined" for m in human.messages), f"join_room with the code {code[:12]!r} was let in")
        human.close()
        table.pump()


def scenario_tokens(server: RoomServer, table: GoodTable) -> None:
    """The token of a member used from a third socket takes the seat over; with the code of another room it is refused."""
    host_a, guest_a = open_room(server.port, "cards")
    host_b = create_room_as(server.port, "OtherRoom")
    elsewhere = Human(server.port)
    elsewhere.send({"type": "join_room", "code": host_b.room, "token": guest_a.token})
    expect_error(elsewhere, {"bad_token"}, "the token of room A with the code of room B")
    elsewhere.close()
    table.pump()

    third = Human(server.port)
    third.send({"type": "join_room", "code": host_a.room, "token": guest_a.token})
    third.wait_for("joined")
    third.wait_for("lobby")
    check(third.seat == guest_a.seat, f"the socket that took over sits at seat {third.seat}, not {guest_a.seat}")
    left = guest_a.wait_for("left")
    check(left.get("reason") == "replaced", f"the old socket was told {left}, not left/replaced")
    check(guest_a.token not in json.dumps(host_a.messages), "the token of a member was sent to another seat")
    for human in (host_a, host_b, third):
        human.close()


SENT_CARD_IDS = {"c1_0", "c9_9", "c0_0"}  # the ids scenario_actions names in its bad actions


def scenario_actions(server: RoomServer, table: GoodTable) -> None:
    """Actions out of turn, stale, replayed, of another seat's card or of a card that never existed, in a room of its own that then plays to the end."""
    host, guest = open_room(server.port, "cards", {"firstSeat": 0})
    host.send({"type": "start"})
    host.wait_for("view")
    guest.wait_for("view")
    check(host.has_actions() and not guest.has_actions(), "the room did not start with the host on turn")
    guest_view = guest.view
    version = guest_view["version"]
    views_before = len([m for m in host.messages if m.get("type") == "view"])
    out_of_turn = {"type": "play", "card": "c1_0"}

    for _ in range(12):
        guest.send({"type": "action", "version": version, "action": out_of_turn})
        expect_error(guest, {"not_your_turn"}, "an action out of turn")
    for stale in (version + 7, max(0, version - 1), 2**31):
        guest.send({"type": "action", "version": stale, "action": out_of_turn})
        expect_error(guest, {"stale_action"}, f"an action with the version {stale}")
    table.pump()

    host_view = host.view
    for action in ({"type": "play", "card": "c1_0"}, {"type": "play", "card": "c9_9"}, {"type": "play"}, {"type": "discard", "card": "c0_0"}, {"card": "c0_0"}):
        host.send({"type": "action", "version": host_view["version"], "action": action})
        expect_error(host, {"illegal_action", "invalid_params", "bad_message"}, f"the action {action}")
    host.send({"type": "action", "version": host_view["version"], "action": host_view["actions"][0]})
    host.send({"type": "action", "version": host_view["version"], "action": host_view["actions"][0]})  # the same version again
    expect_error(host, {"stale_action"}, "the same action sent twice")

    play_to_the_end([host, guest])
    # The demo game's error text repeats the card id the player named ("seat 0 does not hold 'c1_0'"): what a player sent is no leak
    host.leaks = [card for card in host.leaks if card not in SENT_CARD_IDS]
    check(len([m for m in host.messages if m.get("type") == "view"]) > views_before, "the host never saw the room move")
    for human in (host, guest):
        check(not human.leaks, f"seat {human.seat} was sent hidden cards: {human.leaks}")
        human.close()


def scenario_frames(server: RoomServer, table: GoodTable) -> None:
    port = server.port
    ping = message_bytes({"type": "ping"})

    # Over the limit in one frame: closed at the frame header, before the body is read
    client = open_client(port)
    client.send(build_frame(OPCODE_TEXT, b"a" * 100, declared_length=MAX_MESSAGE_BYTES + 1))
    expect_closing(client, CLOSE_TOO_BIG, "a frame of 16 KiB + 1")
    client.close()
    table.pump()

    client = open_client(port)
    client.send(build_frame(OPCODE_TEXT, b"a" * 100, declared_length=HUGE_DECLARED_LENGTH))
    expect_closing(client, CLOSE_TOO_BIG, "a frame that declares 1 TiB")
    client.close()

    # 16 KiB + 1 in 3 fragments: closed at the header of the third, which is never sent in full
    client = open_client(port)
    first, second, third_size = 6000, 6000, MAX_MESSAGE_BYTES + 1 - 12000
    fragments = split_fragments(b"a" * (MAX_MESSAGE_BYTES + 1), [first, second, third_size])
    client.send(fragments[0] + fragments[1] + fragments[2][:20])
    expect_closing(client, CLOSE_TOO_BIG, "three fragments over the limit")
    client.close()
    table.pump()

    # Exactly at the limit, in 3 fragments, with a ping between the first two: one message, and the ping is answered
    client = open_client(port)
    padded = message_bytes({"type": "ping"}, MAX_MESSAGE_BYTES)
    check(len(padded) == MAX_MESSAGE_BYTES, "the padded message has the wrong size")
    pieces = split_fragments(padded, [5000, 5000, MAX_MESSAGE_BYTES - 10000])
    client.send(pieces[0] + build_frame(OPCODE_PING, b"between") + pieces[1] + pieces[2])
    client.wait_pong_frame()
    expect_pong(client, "a message at the limit in three fragments")
    client.close()

    # One byte per write
    client = open_client(port)
    write_one_byte_at_a_time(client.socket, build_frame(OPCODE_TEXT, ping))
    expect_pong(client, "a message one byte at a time")
    client.close()
    table.pump()

    # A client frame with no mask, a binary message, a stray continuation, a text that is not UTF-8
    for what, data, code in (
        ("an unmasked frame", build_frame(OPCODE_TEXT, ping, masked=False), CLOSE_PROTOCOL_ERROR),
        ("a binary message", build_frame(OPCODE_BINARY, ping), CLOSE_UNSUPPORTED_DATA),
        ("a continuation with nothing to continue", build_frame(OPCODE_CONTINUATION, ping), CLOSE_PROTOCOL_ERROR),
    ):
        client = open_client(port)
        client.send(data)
        expect_closing(client, code, what)
        client.close()

    client = open_client(port)
    client.send(build_frame(OPCODE_TEXT, b'{"v":1,"type":"ping","name":"\xff\xfe"}'))
    client.fill(REPLY_LIMIT_SECONDS / 5)
    refused = any(m.get("type") == "error" for m in client.texts) or client.is_closed()
    check(refused and not any(m.get("type") == "pong" for m in client.texts), f"a text that is not UTF-8 was taken: {client.texts}, {client.closing}")
    client.close()
    table.pump()


def scenario_stalls(server: RoomServer, table: GoodTable) -> None:
    """Sockets that say nothing or never finish their request are cut by the handshake deadline; a head over 8 KiB is refused."""
    port = server.port
    silent = [socket.create_connection(("127.0.0.1", port), timeout=IO_TIMEOUT_SECONDS) for _ in range(SILENT_SOCKETS)]
    stalled = socket.create_connection(("127.0.0.1", port), timeout=IO_TIMEOUT_SECONDS)
    stalled.sendall(b"GET /control HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n")  # the empty line that ends the head never comes
    started = time.monotonic()
    waiting = silent + [stalled]
    while waiting and time.monotonic() - started < HANDSHAKE_CUT_LIMIT_SECONDS + 1:
        table.pump()
        readable, _, _ = select.select(waiting, [], [], SHORT_WAIT_SECONDS)
        for connection in readable:
            try:
                data = connection.recv(4096)
            except OSError:
                data = b""
            if not data:  # the stream ended: the server cut it
                waiting.remove(connection)
    elapsed = time.monotonic() - started
    check(not waiting, f"{len(waiting)} of {len(silent) + 1} sockets that never finished a handshake were still open after {elapsed:.1f} s")
    check(elapsed < HANDSHAKE_CUT_LIMIT_SECONDS, f"the handshake deadline took {elapsed:.1f} s, not about {HANDSHAKE_DEADLINE_SECONDS} s")
    for connection in silent + [stalled]:
        connection.close()

    head = socket.create_connection(("127.0.0.1", port), timeout=IO_TIMEOUT_SECONDS)
    request = b"GET /control HTTP/1.1\r\nHost: 127.0.0.1\r\nX-Padding: " + b"a" * OVERSIZED_HEAD_BYTES + b"\r\n\r\n"
    try:
        head.sendall(request)
    except OSError:
        pass  # the server hung up before the end of the head: that is the answer too
    try:
        answer = head.recv(4096)
    except OSError:
        answer = b""
    check(answer == b"" or answer.startswith(f"HTTP/1.1 {HTTP_HEADER_TOO_LARGE}".encode()), f"a request head of 100 KiB got {answer[:60]!r}, not {HTTP_HEADER_TOO_LARGE} or a close")
    head.close()


def scenario_floods(server: RoomServer, table: GoodTable) -> None:
    port = server.port
    for what, message in (("ping", {"type": "ping"}), ("create_room", {"type": "create_room", "game": "cards", "name": "Flood", "options": {}})):
        client = open_client(port)
        sent = 0
        batch = build_frame(OPCODE_TEXT, message_bytes(message)) * FLOOD_BATCH
        while sent < FLOOD_MESSAGES and not client.is_closed():
            client.send(batch)
            sent += FLOOD_BATCH
            client.fill()
        closing = client.wait_closed()
        check(sent < FLOOD_MESSAGES, f"{FLOOD_MESSAGES} {what} messages were all taken, the connection was never closed")
        check(closing.code == CLOSE_POLICY_VIOLATION and closing.reason == "rate_limited", f"the {what} flood ended with {closing}, not close {CLOSE_POLICY_VIOLATION} rate_limited")
        client.close()
        table.pump()

    for index in range(DROPPED_SOCKETS):
        client = open_client(port)
        if index % 2 == 0:  # half of them leave at once, the others after one answer; none says goodbye
            client.send_message({"type": "ping"})
            expect_pong(client, "a socket that is dropped")
        client.close()
        if index % 10 == 0:
            table.pump()


def scenario_address_cap(server: RoomServer, table: GoodTable) -> None:
    """The default cap of open sockets per address; a forged X-Forwarded-For changes nothing without --trusted-proxy-hops."""
    admitted = []
    open_now = len(table.humans)
    for index in range(PER_ADDRESS_CAP - open_now):
        admitted.append(open_client(server.port, [f"X-Forwarded-For: {PROXY_FORWARDED_NET}.{index + 1}"]))
    refused = RawClient(server.port)
    status = refused.handshake([f"X-Forwarded-For: {PROXY_FORWARDED_NET}.200"])
    check(status == HTTP_BUSY, f"socket number {PER_ADDRESS_CAP + 1} of the address got HTTP {status}, not {HTTP_BUSY}")
    refused.close()
    table.pump()

    admitted.pop().close()
    deadline = time.monotonic() + REPLY_LIMIT_SECONDS
    status = 0
    while status != HTTP_SWITCHING and time.monotonic() < deadline:
        again = RawClient(server.port)
        status = again.handshake()
        if status == HTTP_SWITCHING:
            admitted.append(again)
        else:
            again.close()
            time.sleep(0.05)
    check(status == HTTP_SWITCHING, "a place was not free again after a socket of the address was closed")
    for client in admitted:
        client.close()


def scenario_rooms_per_address(server: RoomServer, table: GoodTable) -> None:
    mine = create_room_as(server.port, "Second")  # the table is the first room of this address
    greedy = Human(server.port)
    greedy.send({"type": "create_room", "game": "cards", "name": "Third", "options": {}})
    error = expect_error(greedy, {"too_many_rooms"}, f"a room over --max-rooms-per-address {ROOMS_PER_ADDRESS_FLAG}")
    check("address" in error.get("message", ""), f"the message does not say that it is the address: {error}")
    greedy.send({"type": "ping"})
    greedy.wait_for("pong")
    greedy.close()
    mine.close()


def scenario_lobby_timeout(server: RoomServer, table: GoodTable) -> None:
    """A lobby that only pings is closed with `left {lobby_timeout}`; the socket stays open."""
    silent = create_room_as(server.port, "Silent")

    def step() -> None:
        silent.send({"type": "ping"})  # a ping is no activity of the room
        silent.poll(0.1)
        table.pump()

    run_until(lambda: any(m.get("type") == "left" for m in silent.messages), step, "the silent lobby was never closed", LOBBY_TIMEOUT_LIMIT_SECONDS)
    left = silent.wait_for("left")
    check(left.get("reason") == LOBBY_TIMEOUT_REASON, f"the members of the silent lobby were told {left}, not left/{LOBBY_TIMEOUT_REASON}")
    check(silent.closed is None, "the connection of the members was closed with the room")
    silent.send({"type": "ping"})
    silent.wait_for("pong")
    silent.close()


def scenario_guessing(server: RoomServer, table: GoodTable) -> None:
    """Codes guessed across reconnects: the failed joins of one address are limited as a whole, and an empty budget says nothing about which code exists."""
    host_a, guest_a = open_room(server.port, "cards")  # a real member who comes back later with its token
    codes = wrong_codes(2 * FAILED_JOINS_IN_A_ROW + 2, table.room)
    taken = 0
    for _socket in range(ADDRESS_JOIN_BURST // FAILED_JOINS_IN_A_ROW):
        human = Human(server.port)
        for _ in range(FAILED_JOINS_IN_A_ROW):
            human.send({"type": "join_room", "code": codes[taken], "name": "Guesser"})
            taken += 1
            expect_error(human, {"unknown_room"}, "a wrong code with the budget not yet spent")
        human.close()
        table.pump()

    for code, what in ((codes[taken], "a wrong code"), (table.room, "the code of a room that exists")):
        human, answer = guess(server.port, code, "Late")
        check(answer.get("code") == "rate_limited", f"{what} after {ADDRESS_JOIN_BURST} wrong guesses got {answer}, not rate_limited")
        human.close()

    back = Human(server.port)  # a token is the member's own: it works with the budget empty
    back.send({"type": "join_room", "code": host_a.room, "token": guest_a.token})
    back.wait_for("joined")
    check(back.seat == guest_a.seat, "the returning member did not get its seat")
    for human in (host_a, guest_a, back):
        human.close()


def scenario_proxy_addresses(server: RoomServer, table: GoodTable) -> None:
    """With --trusted-proxy-hops 1 the client is the last entry of X-Forwarded-For; what a client wrote to its left counts for nothing."""
    kept = []
    for index in range(2 * PROXY_PER_ADDRESS_FLAG):  # more sockets than the cap, each from another forwarded address
        kept.append(open_client(server.port, [f"X-Forwarded-For: {PROXY_FORWARDED_NET}.{index + 1}"]))
    table.pump()
    forged = f"{PROXY_FORWARDED_NET}.50"
    for index in range(PROXY_PER_ADDRESS_FLAG):
        kept.append(open_client(server.port, [f"X-Forwarded-For: 10.9.9.{index + 1}, {forged}"]))  # a different left entry each time
    refused = RawClient(server.port)
    status = refused.handshake([f"X-Forwarded-For: 10.9.9.99, {forged}"])
    check(status == HTTP_BUSY, f"a socket of the forwarded address {forged} over the cap got HTTP {status}, not {HTTP_BUSY}: the forged left entry was believed")
    refused.close()
    kept.append(open_client(server.port, [f"X-Forwarded-For: {forged}, {PROXY_FORWARDED_NET}.51"]))  # the forged entry on the right is another address
    for client in kept:
        client.close()


@dataclass
class Scenario:
    name: str
    flags: list[str]
    run: Callable[[RoomServer, GoodTable], None]


SCENARIOS: list[Scenario] = [
    Scenario("seats", [], scenario_seats),
    Scenario("codes", [], scenario_codes),
    Scenario("tokens", [], scenario_tokens),
    Scenario("actions", [], scenario_actions),
    Scenario("frames", [], scenario_frames),
    Scenario("stalls", ["--max-per-address", "100"], scenario_stalls),
    Scenario("floods", [], scenario_floods),
    Scenario("address-cap", [], scenario_address_cap),
    Scenario("rooms-per-address", ["--max-rooms-per-address", str(ROOMS_PER_ADDRESS_FLAG)], scenario_rooms_per_address),
    Scenario("lobby-timeout", ["--lobby-timeout-ms", str(LOBBY_TIMEOUT_MS)], scenario_lobby_timeout),
    Scenario("guessing", [], scenario_guessing),
    Scenario("proxy-addresses", ["--trusted-proxy-hops", "1", "--max-per-address", str(PROXY_PER_ADDRESS_FLAG)], scenario_proxy_addresses),
]


def run_scenario(exe: Path, scenario: Scenario, extra_flags: list[str]) -> None:
    flags = ["--computer-delay-ms", str(COMPUTER_DELAY_MS), *scenario.flags, *extra_flags]
    with RoomServer(exe, flags) as server:
        table = GoodTable(server.port)
        scenario.run(server, table)
        table.finish()
        alive = Human(server.port)  # the server still answers a new client
        alive.send({"type": "ping"})
        alive.wait_for("pong")
        alive.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, help="the BagiRoomServer executable (or --preset)")
    parser.add_argument("--preset", help="the CMake preset that built it: the executable is build/<preset>/bin/BagiRoomServer")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--only", action="append", default=[], metavar="NAME", help="run only this scenario (repeatable)")
    parser.add_argument("--server-arg", action="append", default=[], metavar="ARG", help="one more argument of the server, after the scenario's own (repeatable; write --server-arg=--flag)")
    parser.add_argument("--list", action="store_true", help="print the names of the scenarios and exit")
    args = parser.parse_args()

    if args.list:
        print("\n".join(scenario.name for scenario in SCENARIOS))
        return EXIT_PASSED
    if (args.exe is None) == (args.preset is None):
        parser.error("give the executable either as --exe or as --preset")
    unknown = [name for name in args.only if name not in {scenario.name for scenario in SCENARIOS}]
    if unknown:
        parser.error(f"unknown scenario {unknown}; --list shows them")
    exe = args.exe or preset_executable(args.preset, SERVER_NAME, args.root)
    if not exe.is_file():
        print(f"SKIPPED: {exe} does not exist")
        return EXIT_SKIPPED

    for scenario in SCENARIOS:
        if args.only and scenario.name not in args.only:
            continue
        print(f"{scenario.name}: running", flush=True)
        try:
            run_scenario(exe.resolve(), scenario, args.server_arg)
        except (SmokeFailure, OSError, ConnectionClosed, HandshakeError, ValueError, KeyError) as error:
            print(f"FAILED ({scenario.name}): {error!r}" if not str(error) else f"FAILED ({scenario.name}): {error}")
            return EXIT_FAILED
    print("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
