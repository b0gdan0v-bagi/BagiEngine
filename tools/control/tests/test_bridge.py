"""bagi_bridge.py between a page (fake_page.py) and real control clients, on free ports of this machine."""

import json
import queue
import re
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

import bagi_bridge
from bagi_bridge import MAX_CLIENTS, MAX_HELLO_BYTES, MAX_OUTBOX_BYTES, MAX_PAGE_MESSAGE_BYTES, MAX_REQUEST_BYTES, Bridge
from bagi_control_client import (
    OPCODE_BINARY,
    OPCODE_CLOSE,
    OPCODE_CONTINUATION,
    OPCODE_PING,
    OPCODE_PONG,
    OPCODE_TEXT,
    BagiControlClient,
    ConnectionClosed,
    HandshakeError,
    encode_frame,
)
from bagi_session import GameSession
from fake_page import PAGE_ORIGIN, WAIT_SECONDS, FakePage, RawWebSocket, json_answers
from game_process import free_port

TOKEN = "test-token_1"
BRIDGE_SCRIPT = Path(bagi_bridge.__file__)
MASK = b"\x01\x02\x03\x04"


@pytest.fixture
def start_bridge():
    """Starts bridges on free ports and stops them, which also ends the pages and clients of a test."""
    started: list[Bridge] = []

    def start(control_port: int = 0, **settings) -> Bridge:
        bridge = Bridge(token=TOKEN, control_port=control_port, page_port=0, **settings)
        bridge.start()
        started.append(bridge)
        return bridge

    yield start
    for bridge in started:
        bridge.stop()


@pytest.fixture
def bridge(start_bridge):
    return start_bridge()


def link(bridge: Bridge, handler=None) -> tuple[FakePage, int]:
    page = FakePage(bridge.page_port, TOKEN, handler)
    port = bridge.wait_linked(WAIT_SECONDS)
    assert port is not None, f"the page was not linked: {page.close_status}"
    return page, port


def client_of(port: int) -> BagiControlClient:
    """A client that connects when it is entered as a context."""
    return BagiControlClient(port=port, timeout=WAIT_SECONDS)


def connected(port: int) -> BagiControlClient:
    client = client_of(port)
    client.connect()
    return client


def retried(connect, refusals):
    """What `connect()` gives once it stops raising one of `refusals`."""
    deadline = time.monotonic() + WAIT_SECONDS
    while True:
        try:
            return connect()
        except refusals:
            assert time.monotonic() < deadline, "still refused"
            time.sleep(0.02)


def announced(opcode: int, length: int, fin: bool = True) -> bytes:
    """The header of a masked frame of `length` bytes, without any of them."""
    return struct.pack("!BBQ", (0x80 if fin else 0) | opcode, 0x80 | 127, length) + MASK


def game(method: str, params: dict | None):
    if method == "list_games":
        return {"active": "Fake", "games": [{"name": "Fake", "controllable": True}]}
    if method == "status":
        return {"frame": 7, "manualClock": True}
    return {"method": method, "params": params}


def test_requests_and_answers_pass_byte_for_byte(bridge):
    page, port = link(bridge)
    request = '  {"id":1,  "method":"ping",\n "note":"€ ☃ 𝄞 \x00"}  not even JSON '
    answer = ' {"id": 1,"ok":true}  € \n'
    with client_of(port) as client:
        client.send_text(request)
        assert page.next_message() == f"1 {request}"
        page.send(f"1 {answer}")
        assert client.receive_text() == answer


def test_the_client_and_a_game_session_work_through_the_bridge(bridge):
    _, port = link(bridge, json_answers(game))
    with client_of(port) as client:
        assert client.call("perform_action", {"type": "swap"}) == {"method": "perform_action", "params": {"type": "swap"}}
    session = GameSession()
    try:
        assert session.connect(port=port)["activeGame"] == "Fake"
        assert session.call("get_state") == {"method": "get_state", "params": None}
    finally:
        session.close()


def test_clients_that_use_the_same_ids_get_their_own_answers(bridge):
    page, port = link(bridge)
    with client_of(port) as first, client_of(port) as second:
        first.send_text('{"id": 1, "method": "first"}')
        second.send_text('{"id": 1, "method": "second"}')
        channels = {}
        for _ in range(2):
            channel, _, request = page.next_message().partition(" ")
            channels[json.loads(request)["method"]] = channel
        assert channels["first"] != channels["second"]
        page.send(f"{channels['second']} for the second")
        page.send(f"{channels['first']} for the first")
        assert first.receive_text() == "for the first"
        assert second.receive_text() == "for the second"


def test_a_push_reaches_every_client(bridge):
    page, port = link(bridge)
    with client_of(port) as first, client_of(port) as second:
        page.push('{"event": "state_changed"}')
        assert first.receive_text() == '{"event": "state_changed"}'
        assert second.receive_text() == '{"event": "state_changed"}'


def test_an_ended_client_is_announced_and_its_late_answer_is_dropped(bridge):
    page, port = link(bridge)
    leaving = connected(port)
    leaving.send_text("a request")
    leaving.close()
    assert page.next_message() == "1 a request"
    assert page.next_message() == "1"
    page.send("1 too late")
    with client_of(port) as staying:
        staying.send_text("another")
        assert page.next_message() == "2 another"
        page.send("2 its answer")
        assert staying.receive_text() == "its answer"


def test_the_control_port_listens_only_while_a_page_is_linked(start_bridge):
    control_port = free_port()
    bridge = start_bridge(control_port=control_port)
    with pytest.raises(ConnectionRefusedError):
        connected(control_port)

    page, port = link(bridge)
    assert port == control_port
    client = connected(port)
    page.close()
    assert bridge.wait_unlinked(WAIT_SECONDS)
    with pytest.raises(ConnectionClosed) as closed:
        client.receive_text()
    assert closed.value.code == 1001
    with pytest.raises(ConnectionRefusedError):
        connected(port)

    next_page, _ = link(bridge)
    with client_of(control_port) as next_client:
        next_client.send_text("hello again")
        assert next_page.next_message() == "2 hello again"  # a channel number is not used twice


def test_a_control_port_taken_after_the_start_closes_the_page_so_that_it_retries(start_bridge):
    control_port = free_port()
    bridge = start_bridge(control_port=control_port)
    with socket.socket() as other_program:
        other_program.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        other_program.bind(("127.0.0.1", control_port))
        other_program.listen()
        page = FakePage(bridge.page_port, TOKEN)
        assert page.wait_closed() == (1011, "control_port")
        assert bridge.wait_linked(0) is None
    link(bridge)


def test_stop_closes_the_page_and_the_clients(bridge):
    page, port = link(bridge)
    client = connected(port)
    bridge.stop()
    assert page.wait_closed()[0] == 1001
    with pytest.raises(ConnectionClosed) as closed:
        client.receive_text()
    assert closed.value.code == 1001
    with pytest.raises(ConnectionRefusedError):
        RawWebSocket(bridge.page_port, origin=PAGE_ORIGIN)


@pytest.mark.parametrize(
    "opcode, hello, reason",
    [
        (OPCODE_TEXT, json.dumps({"bagiControlLink": 1, "token": TOKEN + "x"}), "bad_token"),
        (OPCODE_TEXT, json.dumps({"bagiControlLink": 1, "token": "тайна"}), "bad_token"),
        (OPCODE_TEXT, "not JSON", "bad_hello"),
        (OPCODE_TEXT, "[" * (MAX_HELLO_BYTES - 1), "bad_hello"),
        (OPCODE_TEXT, json.dumps([TOKEN]), "bad_hello"),
        (OPCODE_TEXT, json.dumps({"bagiControlLink": 2, "token": TOKEN}), "bad_hello"),
        (OPCODE_TEXT, json.dumps({"bagiControlLink": True, "token": TOKEN}), "bad_hello"),
        (OPCODE_TEXT, json.dumps({"bagiControlLink": 1, "token": TOKEN, "more": 0}), "bad_hello"),
        (OPCODE_BINARY, json.dumps({"bagiControlLink": 1, "token": TOKEN}), "bad_hello"),
    ],
)
def test_a_wrong_hello_is_refused_and_links_nothing(bridge, opcode, hello, reason):
    page = RawWebSocket(bridge.page_port, origin=PAGE_ORIGIN)
    page.send(opcode, hello.encode("utf-8"))
    assert page.receive_close() == (1008, reason)
    assert bridge.wait_linked(0) is None


def test_a_silent_page_is_refused(bridge, monkeypatch):
    monkeypatch.setattr(bagi_bridge, "HELLO_TIMEOUT_SECONDS", 0.1)
    page = RawWebSocket(bridge.page_port, origin=PAGE_ORIGIN)
    assert page.receive_close() == (1008, "bad_hello")


def test_a_second_page_is_busy_and_the_first_stays_linked(bridge):
    page, port = link(bridge)
    second = FakePage(bridge.page_port, TOKEN)
    assert second.wait_closed() == (1008, "busy")
    stranger = FakePage(bridge.page_port, "wrong")
    assert stranger.wait_closed() == (1008, "bad_token")  # without the token nothing is said about the link
    with client_of(port) as client:
        client.send_text("still there")
        assert page.next_message() == "1 still there"


@pytest.mark.parametrize(
    "origin, host",
    [
        (None, None),
        ("https://evil.example", None),
        ("null", None),
        ("http://localhost.evil.example", None),
        (PAGE_ORIGIN, "evil.example:8766"),
    ],
)
def test_the_page_port_refuses_a_foreign_peer(bridge, origin, host):
    with pytest.raises(HandshakeError) as refused:
        RawWebSocket(bridge.page_port, origin=origin, host=host)
    assert refused.value.status == 403


def test_a_listed_origin_is_admitted_by_its_scheme_host_and_port(start_bridge):
    bridge = start_bridge(origins=["https://Pages.example"])
    for origin in ("http://pages.example", "https://pages.example:8443", "https://pages.example.evil.example"):
        with pytest.raises(HandshakeError) as refused:
            RawWebSocket(bridge.page_port, origin=origin)
        assert refused.value.status == 403, origin
    for origin in ("https://pages.example", "https://pages.example:443", "http://[::1]:3000"):
        RawWebSocket(bridge.page_port, origin=origin).close()


@pytest.mark.parametrize("origin, host", [("https://evil.example", None), ("null", None), (None, "evil.example")])
def test_the_control_port_refuses_a_foreign_page(bridge, origin, host):
    _, port = link(bridge)
    with pytest.raises(HandshakeError) as refused:
        RawWebSocket(port, origin=origin, host=host)
    assert refused.value.status == 403
    RawWebSocket(port, origin="http://127.0.0.1:3000", host="localhost").close()


def test_one_client_over_the_limit_gets_503_until_another_leaves(bridge):
    _, port = link(bridge)
    clients = [connected(port) for _ in range(MAX_CLIENTS)]
    with pytest.raises(HandshakeError) as refused:
        connected(port)
    assert refused.value.status == 503
    clients.pop().close()
    retried(lambda: connected(port), HandshakeError).close()


@pytest.mark.parametrize(
    "data, code",
    [
        (encode_frame(OPCODE_TEXT, b"unmasked"), 1002),
        (encode_frame(OPCODE_CONTINUATION, b"no start", MASK), 1002),
        (bytes([0xC0 | OPCODE_TEXT, 0x80]) + MASK, 1002),
        (encode_frame(OPCODE_PING, b"fragmented", MASK, fin=False), 1002),
        (encode_frame(OPCODE_CLOSE, b"x", MASK), 1002),
        (encode_frame(OPCODE_BINARY, b"binary", MASK), 1003),
        (encode_frame(OPCODE_TEXT, b"\xff\xfe", MASK), 1007),
    ],
    ids=["unmasked", "continuation", "reserved_bit", "fragmented_ping", "short_close", "binary", "not_utf8"],
)
def test_a_client_that_breaks_the_protocol_is_closed_and_nothing_reaches_the_page(bridge, data, code):
    page, port = link(bridge)
    client = RawWebSocket(port)
    client.send_bytes(data)
    assert client.receive_close()[0] == code
    assert page.next_message() == "1"


def test_fragments_are_one_message_and_pings_and_closes_are_answered(bridge):
    page, port = link(bridge)
    client = RawWebSocket(port)
    client.send(OPCODE_TEXT, b"one ", fin=False)
    client.send(OPCODE_PING, b"between")
    assert client.receive() == (OPCODE_PONG, b"between")
    client.send(OPCODE_CONTINUATION, b"two")
    assert page.next_message() == "1 one two"
    client.send(OPCODE_CLOSE, struct.pack("!H", 4000))
    assert client.receive_close()[0] == 4000
    assert client.receive() is None


def test_a_request_over_the_limit_is_refused_from_its_announced_length(bridge):
    page, port = link(bridge)
    largest = "a" * MAX_REQUEST_BYTES
    with client_of(port) as client:
        client.send_text(largest)
        assert page.next_message() == f"1 {largest}"

    announcing = RawWebSocket(port)
    announcing.send_bytes(announced(OPCODE_TEXT, MAX_REQUEST_BYTES + 1))
    assert announcing.receive_close()[0] == 1009

    fragmenting = RawWebSocket(port)
    fragmenting.send(OPCODE_TEXT, largest.encode("ascii"), fin=False)
    fragmenting.send_bytes(announced(OPCODE_CONTINUATION, 1))
    assert fragmenting.receive_close()[0] == 1009


def test_a_page_may_buffer_nothing_large_before_its_hello(bridge):
    page = RawWebSocket(bridge.page_port, origin=PAGE_ORIGIN)
    page.send_bytes(announced(OPCODE_TEXT, MAX_HELLO_BYTES + 1))
    assert page.receive_close()[0] == 1009


def test_a_linked_page_sends_more_than_a_request_may_hold_up_to_its_own_limit(bridge):
    page, port = link(bridge)
    answer = "b" * (4 * MAX_REQUEST_BYTES)
    with client_of(port) as client:
        client.send_text("a big one")
        page.next_message()
        page.send(f"1 {answer}")
        assert client.receive_text() == answer
        page.socket.send_bytes(announced(OPCODE_TEXT, MAX_PAGE_MESSAGE_BYTES + 1))
        assert page.wait_closed()[0] == 1009
        assert bridge.wait_unlinked(WAIT_SECONDS)


@pytest.mark.parametrize(
    "opcode, message",
    [
        (OPCODE_TEXT, b"x answer"),
        (OPCODE_TEXT, b"1"),
        (OPCODE_TEXT, b"01 answer"),
        (OPCODE_TEXT, b" 1 answer"),
        (OPCODE_BINARY, b"1 answer"),
    ],
)
def test_a_page_message_without_a_channel_ends_the_link(bridge, opcode, message):
    page, port = link(bridge)
    client = connected(port)
    page.socket.send(opcode, message)
    assert page.wait_closed()[0] == 1002
    with pytest.raises(ConnectionClosed) as closed:
        client.receive_text()
    assert closed.value.code == 1001


def test_a_client_that_does_not_read_is_dropped_and_holds_nobody_up(bridge):
    page, port = link(bridge)
    slow = RawWebSocket(port)
    slow.send_text("slow")
    assert page.next_message() == "1 slow"
    with client_of(port) as other:
        chunk = "x" * (MAX_OUTBOX_BYTES // 4)
        for _ in range(8):  # twice what may be queued: the socket buffers of the system take a part
            page.send(f"1 {chunk}")
        other.send_text("quick")
        page.send("2 answered")
        assert other.receive_text() == "answered"
        assert sorted([page.next_message(), page.next_message()]) == ["1", "2 quick"]


@pytest.mark.parametrize("taken", ["control_port", "page_port"])
def test_a_taken_port_stops_the_start(taken):
    with socket.socket() as other_program:
        other_program.bind(("127.0.0.1", 0))
        other_program.listen()
        port = other_program.getsockname()[1]
        bridge = Bridge(**{"control_port": 0, "page_port": 0, taken: port})
        with pytest.raises(OSError, match=f"cannot listen on 127.0.0.1:{port}"):
            bridge.start()
        bridge.stop()


def run_bridge(*arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(BRIDGE_SCRIPT), *arguments], capture_output=True, text=True, timeout=WAIT_SECONDS)


@pytest.mark.parametrize(
    "arguments",
    [
        ["--control-port", "0"],
        ["--page-port", "70000"],
        ["--token", "a secret with spaces"],
        ["--origin", "pages.example"],
        ["--control-port", "5000", "--page-port", "5000"],
    ],
)
def test_the_command_line_refuses_a_bad_argument(arguments):
    result = run_bridge(*arguments)
    assert result.returncode == 2
    assert "a secret" not in result.stderr


def test_the_command_line_stops_when_the_control_port_is_taken():
    with socket.socket() as other_program:
        other_program.bind(("127.0.0.1", 0))
        other_program.listen()
        port = other_program.getsockname()[1]
        result = run_bridge("--control-port", str(port), "--page-port", str(free_port()))
    assert result.returncode == 2
    assert f"cannot listen on 127.0.0.1:{port}" in result.stderr


def test_the_command_line_prints_the_query_that_links_a_page_and_logs_no_token():
    control_port, page_port = free_port(), free_port()
    command = [sys.executable, str(BRIDGE_SCRIPT), "--control-port", str(control_port), "--page-port", str(page_port)]
    command += ["--origin", "https://first.example", "https://pages.example"]
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        first_line: queue.Queue[str] = queue.Queue()
        threading.Thread(target=lambda: first_line.put(process.stdout.readline()), daemon=True).start()
        query = re.search(r"\?controlToken=([A-Za-z0-9_-]+)&control=(\d+)", first_line.get(timeout=WAIT_SECONDS))
        assert query is not None and int(query.group(2)) == page_port
        page = FakePage(page_port, query.group(1), json_answers(game), origin="https://pages.example")
        client = retried(lambda: connected(control_port), ConnectionRefusedError)
        assert client.call("status")["frame"] == 7
        client.close()
        page.close()
    finally:
        process.terminate()
        _, log = process.communicate(timeout=WAIT_SECONDS)
    assert "page linked" in log
    assert query.group(1) not in log
