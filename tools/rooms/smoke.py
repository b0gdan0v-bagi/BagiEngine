#!/usr/bin/env python3
"""Smoke test of the room server: starts BagiRoomServer and plays hands against it from this process, over real sockets.

    python tools/rooms/smoke.py --exe <path of the built BagiRoomServer> [--probe <path of the built BagiRoomClientProbe>]
    python tools/rooms/smoke.py --preset macos-debug            # build/macos-debug/bin/BagiRoomServer and BagiRoomClientProbe

Scenarios, on a server with --test-games --computer-delay-ms 0 --reconnect-grace-ms 300:
  * a room of two humans and two computers plays a hand to the end;
  * a human drops mid-hand, a computer plays its seat once the grace has passed, the human returns with its token and plays on;
  * the same hand on `cards-leaky` must make the judge report a leak (the negative control: a judge that cannot fail proves nothing);
  * a server started with --origin https://good.example answers 403 to another Origin and takes the listed one;
  * a server started with PORT=0 in the environment and no --port listens on a port of its own and takes a client (the variable is read);
  * a plain GET /healthz gets 200 and `ok`, GET / gets 400.
With the probe (the C++ client of RoomClient, a process of its own; its scenarios are skipped with a line when there is none):
  * the probe hosts a room, plays it to the end and prints `PROBE OK`;
  * the same against a server whose --origin list names only a web page: the probe sends no Origin and gets in;
  * the server is killed after the probe's first view: the probe fails by itself, fast and non-zero, without waiting for its 30 s.
The process is ended with SIGTERM and has to exit 0 in time (on Windows it is terminated, and its exit code is not checked).

The judge is the rule of tests/BECore/Rooms/WireJudge.h, ported: change both together (tools/rooms/CLAUDE.md).
Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
"""

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable

TOOLS_CONTROL = Path(__file__).resolve().parents[1] / "control"
sys.path.insert(0, str(TOOLS_CONTROL))

from bagi_control_client import BagiControlClient, ConnectionClosed, HandshakeError  # noqa: E402
from game_process import log_tail, preset_executable  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

PROTOCOL_VERSION = 1
SERVER_NAME = "BagiRoomServer"
LISTENING_PATTERN = re.compile(rf"{SERVER_NAME} listening on ws://[0-9.]+:(\d+)")
STARTUP_SECONDS = 30.0
SHUTDOWN_SECONDS = 10.0
STARTUP_POLL_SECONDS = 0.05
IO_TIMEOUT_SECONDS = 10.0
POLL_SECONDS = 0.01
STEP_LIMIT_SECONDS = 20.0
RECONNECT_GRACE_MS = 300
GUEST_SEAT = 1
GUEST_DROP_TABLE_SIZE = 4  # guest, two computers, host have played: the guest's second turn
TOTAL_PLAYS = 12
GOOD_ORIGIN = "https://good.example"
EVIL_ORIGIN = "https://evil.example"
HTTP_FORBIDDEN = 403
HTTP_OK = 200
HTTP_BAD_REQUEST = 400
HEALTH_PATH = "/healthz"
DEFAULT_PORT = 8080  # what a server without --port and PORT would listen on
PROBE_EXE_NAME = "BagiRoomClientProbe"
PROBE_OK = "PROBE OK"
PROBE_FIRST_VIEW = "PROBE first view"
PROBE_FAILED = "PROBE FAILED"
PROBE_LIMIT_SECONDS = 40.0  # the probe stops itself after 30 s
PROBE_GONE_LIMIT_SECONDS = 10.0  # a probe whose server is gone has to notice at once
PROBE_GONE_COMPUTER_DELAY_MS = 1500  # slow computers keep the hand running while the server is taken away

# A card id is c<digit>_<digit>, a whole token (WireJudge.h, FindCardIds)
CARD_ID = re.compile(r"(?<![A-Za-z0-9_])c\d_\d(?![A-Za-z0-9_])")


class SmokeFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SmokeFailure(message)


def find_card_ids(text: str) -> list[str]:
    return CARD_ID.findall(text)


def find_leaked_card_ids(text: str, seat: int) -> list[str]:
    """The ids a message may not name for the client of `seat`: not its own (`c<seat>_*`) and not in the message's own `view.table`."""
    allowed_table: set[str] = set()
    try:
        message = json.loads(text)
        table = message["view"]["table"]
        allowed_table = set(find_card_ids(json.dumps(table)))
    except (ValueError, KeyError, TypeError):
        pass
    own_prefix = f"c{seat}_" if seat >= 0 else None
    return [card for card in find_card_ids(text) if not (own_prefix and card.startswith(own_prefix)) and card not in allowed_table]


class RoomServer:
    """One BagiRoomServer process on a port it picked itself (--port 0), its output in a temporary file."""

    def __init__(self, exe: Path, extra_args: list[str], environment: dict[str, str] | None = None):
        # A server given PORT in its environment gets no --port: the variable is what is tried
        port_flags = [] if environment and "PORT" in environment else ["--port", "0"]
        self.command = [str(exe), "--test-games", *port_flags, "--computer-delay-ms", "0", "--reconnect-grace-ms", str(RECONNECT_GRACE_MS), "--bind", "loopback", *extra_args]
        self.environment = {**os.environ, **(environment or {})}
        self.exe = exe
        self.port = 0
        self._log_dir = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.log_path = Path(self._log_dir.name) / "server.log"
        self._process: subprocess.Popen | None = None
        self._killed = False

    def __enter__(self) -> "RoomServer":
        with open(self.log_path, "wb") as log:
            self._process = subprocess.Popen(self.command, cwd=self.exe.parent, env=self.environment, stdout=log, stderr=subprocess.STDOUT)
        self.port = self._wait_for_listening()
        return self

    def __exit__(self, exc_type: object, *rest: object) -> None:
        # A failed scenario still ends the server; its exit code then only matters when nothing else failed
        code = self._stop(report=exc_type is None)
        self._log_dir.cleanup()
        if exc_type is None and code is not None:
            raise SmokeFailure(code)

    def kill(self) -> None:
        """Takes the server away without a word (no close frames); its exit code is then no longer checked."""
        assert self._process is not None
        self._killed = True
        self._process.kill()
        self._process.wait()

    def _wait_for_listening(self) -> int:
        assert self._process is not None
        deadline = time.monotonic() + STARTUP_SECONDS
        while time.monotonic() < deadline:
            if self._process.poll() is not None:
                raise SmokeFailure(f"the server exited with code {self._process.returncode} before it listened\n--- server log (tail) ---\n{log_tail(self.log_path)}")
            match = LISTENING_PATTERN.search(self.log_path.read_text(errors="replace"))
            if match:
                return int(match.group(1))
            time.sleep(STARTUP_POLL_SECONDS)
        self._process.kill()
        raise SmokeFailure(f"the server did not print its listening line in {STARTUP_SECONDS} s\n--- server log (tail) ---\n{log_tail(self.log_path)}")

    def _stop(self, report: bool) -> str | None:
        """Ends the process with SIGTERM; a description of what was wrong with its exit, or None."""
        process = self._process
        if process is None:
            return None
        self._process = None
        if self._killed:
            return None
        process.terminate()
        try:
            process.wait(timeout=SHUTDOWN_SECONDS)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            return f"the server did not end within {SHUTDOWN_SECONDS} s of SIGTERM" if report else None
        if report and not self._killed and sys.platform != "win32" and process.returncode != 0:
            return f"the server exited with code {process.returncode} after SIGTERM, not 0\n--- server log (tail) ---\n{log_tail(self.log_path)}"
        return None


class Probe:
    """One BagiRoomClientProbe process against a server, its output in a temporary file."""

    def __init__(self, exe: Path, port: int):
        self.command = [str(exe), "--url", f"ws://127.0.0.1:{port}", "--game", "cards"]
        self.exe = exe
        self._log_dir = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.log_path = Path(self._log_dir.name) / "probe.log"
        self._process: subprocess.Popen | None = None

    def __enter__(self) -> "Probe":
        with open(self.log_path, "wb") as log:
            self._process = subprocess.Popen(self.command, cwd=self.exe.parent, stdout=log, stderr=subprocess.STDOUT)
        return self

    def __exit__(self, *rest: object) -> None:
        if self._process is not None and self._process.poll() is None:
            self._process.kill()
            self._process.wait()
        self._log_dir.cleanup()

    def output(self) -> str:
        return self.log_path.read_text(errors="replace")

    def wait_for_line(self, text: str, limit: float = STEP_LIMIT_SECONDS) -> None:
        assert self._process is not None
        deadline = time.monotonic() + limit
        while text not in self.output():
            check(self._process.poll() is None, f"the probe ended before it printed '{text}'\n--- probe output ---\n{self.output()}")
            check(time.monotonic() < deadline, f"the probe did not print '{text}' in {limit} s\n--- probe output ---\n{self.output()}")
            time.sleep(STARTUP_POLL_SECONDS)

    def finish(self, limit: float) -> int:
        """Waits for the probe to end by itself and returns its exit code."""
        assert self._process is not None
        try:
            return self._process.wait(timeout=limit)
        except subprocess.TimeoutExpired:
            raise SmokeFailure(f"the probe did not end within {limit} s\n--- probe output ---\n{self.output()}") from None


class Human:
    """A player on its own socket: reads and judges every text the server sends, plays the first action of its view on its turn."""

    def __init__(self, port: int, origin: str | None = None):
        self.client = BagiControlClient(port=port, timeout=IO_TIMEOUT_SECONDS, origin=origin)
        self.client.connect()
        self.seat = -1
        self.room = ""
        self.token = ""
        self.messages: list[dict[str, Any]] = []
        self.leaks: list[str] = []
        self.closed: ConnectionClosed | None = None
        self.view: dict[str, Any] | None = None
        self._cursor = 0
        self._acted_version: int | None = None

    def send(self, message: dict[str, Any]) -> None:
        self.client.send_text(json.dumps({"v": PROTOCOL_VERSION, **message}))

    def poll(self, timeout: float = POLL_SECONDS) -> None:
        """Reads what has arrived (waits `timeout` for the first message)."""
        wait = timeout
        while self.closed is None:
            try:
                text = self.client.receive_text(wait)
            except TimeoutError:
                return
            except ConnectionClosed as closed:
                self.closed = closed
                return
            wait = POLL_SECONDS
            self._take(text)

    def _take(self, text: str) -> None:
        message = json.loads(text)
        kind = message.get("type")
        if kind == "joined":
            self.room, self.token, self.seat = message["room"], message["token"], message["seat"]
        elif kind == "view":
            self.view = message
            self.seat = message["seat"]
        self.leaks += find_leaked_card_ids(text, self.seat)
        self.messages.append(message)

    def wait_for(self, kind: str, limit: float = STEP_LIMIT_SECONDS) -> dict[str, Any]:
        """The next message of `kind` after the ones already taken by an earlier wait_for (the ones in between stay in `messages`)."""
        deadline = time.monotonic() + limit
        while True:
            while self._cursor < len(self.messages):
                message = self.messages[self._cursor]
                self._cursor += 1
                if message.get("type") == kind:
                    return message
            if self.closed is not None or time.monotonic() >= deadline:
                raise SmokeFailure(f"no '{kind}' arrived (closed: {self.closed}); got {[m.get('type') for m in self.messages]}")
            self.poll()

    def table_size(self) -> int:
        return len(self.view["view"]["table"]) if self.view else -1

    def has_actions(self) -> bool:
        return bool(self.view and self.view["actions"])

    def sees_the_end(self) -> bool:
        return bool(self.view and self.view["finished"])

    def act_if_its_turn(self) -> None:
        if not self.has_actions() or self.view["version"] == self._acted_version:
            return
        self._acted_version = self.view["version"]
        self.send({"type": "action", "version": self.view["version"], "action": self.view["actions"][0]})

    def close(self) -> None:
        self.client.close()


def run_until(done: Callable[[], bool], step: Callable[[], None], what: str, limit: float = STEP_LIMIT_SECONDS) -> None:
    deadline = time.monotonic() + limit
    while not done():
        check(time.monotonic() < deadline, f"timed out: {what}")
        step()


def play_to_the_end(humans: list[Human]) -> None:
    def step() -> None:
        for human in humans:
            human.poll()
            human.act_if_its_turn()

    run_until(lambda: all(human.sees_the_end() for human in humans), step, "the hand did not end")
    for human in humans:
        ids = find_card_ids(json.dumps(human.view["view"]["table"]))
        check(len(ids) == TOTAL_PLAYS and len(set(ids)) == TOTAL_PLAYS, f"the final table of seat {human.seat} shows {ids}, not the {TOTAL_PLAYS} cards")


def open_room(port: int, game: str, options: dict[str, Any] | None = None) -> tuple[Human, Human]:
    host = Human(port)
    host.send({"type": "create_room", "game": game, "name": "Host", "options": options or {}})
    host.wait_for("joined")
    host.wait_for("lobby")
    guest = Human(port)
    guest.send({"type": "join_room", "code": host.room, "name": "Guest"})
    guest.wait_for("joined")
    guest.wait_for("lobby")
    return host, guest


def check_no_error(humans: list[Human]) -> None:
    for human in humans:
        errors = [message for message in human.messages if message.get("type") == "error"]
        if errors:
            raise SmokeFailure(f"the server answered an error: {errors[0]}")


def shows_away_human(message: dict[str, Any]) -> bool:
    """A `lobby` that lists a human whose connection is down."""
    return message.get("type") == "lobby" and any(seat.get("kind") == "human" and seat.get("connected") is False for seat in message.get("seats", []))


def scenario_hand(port: int) -> None:
    host, guest = open_room(port, "cards")
    host.send({"type": "start"})
    play_to_the_end([host, guest])
    check_no_error([host, guest])
    for human in (host, guest):
        check(not human.leaks, f"seat {human.seat} was sent hidden cards: {human.leaks}")
        human.close()


def scenario_reconnect(port: int) -> None:
    host, guest = open_room(port, "cards", {"firstSeat": GUEST_SEAT})
    check(guest.seat == GUEST_SEAT, f"the guest sits at seat {guest.seat}")
    host.send({"type": "start"})

    def step() -> None:
        host.poll()
        host.act_if_its_turn()
        guest.poll()
        if guest.table_size() < GUEST_DROP_TABLE_SIZE:
            guest.act_if_its_turn()

    run_until(lambda: guest.table_size() == GUEST_DROP_TABLE_SIZE and guest.has_actions(), step, "the guest never got its second turn")

    guest.close()
    dropped_at = len(host.messages)
    run_until(lambda: any(shows_away_human(message) for message in host.messages[dropped_at:]), host.poll, "the host was never told that the guest is away")
    # Past the grace a computer plays the seat: the table grows although nobody acts for it
    time.sleep(RECONNECT_GRACE_MS / 1000.0)
    run_until(lambda: host.table_size() > GUEST_DROP_TABLE_SIZE, host.poll, "no computer took the seat after the grace")

    back = Human(port)
    back.send({"type": "join_room", "code": guest.room, "token": guest.token})
    back.wait_for("view")
    check([message["type"] for message in back.messages[:3]] == ["joined", "lobby", "view"], f"the returning human got {[m['type'] for m in back.messages]}")
    check(back.seat == GUEST_SEAT, f"the human came back to seat {back.seat}")
    play_to_the_end([host, back])
    check_no_error([host, back])
    for human in (host, guest, back):
        check(not human.leaks, f"seat {human.seat} was sent hidden cards: {human.leaks}")
        human.close()


def scenario_leaky(port: int) -> None:
    host, guest = open_room(port, "cards-leaky")
    host.send({"type": "start"})
    play_to_the_end([host, guest])
    check(host.leaks and guest.leaks, "the judge did not notice that every hand was on the wire: it cannot fail, so the other scenarios prove nothing")
    check(find_leaked_card_ids('{"type":"view","view":{"hand":["c0_1"],"table":[],"allHands":[["c0_1"],["c1_0"]]}}', 0) == ["c1_0"], "the judge missed a leak in a hand-made text")
    check(find_card_ids("c1_20 xc1_2 c1_2x") == [], "the judge took part of a longer word for a card")
    host.close()
    guest.close()


def scenario_origin(port: int) -> None:
    try:
        BagiControlClient(port=port, timeout=IO_TIMEOUT_SECONDS, origin=EVIL_ORIGIN).connect()
    except HandshakeError as refused:
        check(refused.status == HTTP_FORBIDDEN, f"a foreign Origin got HTTP {refused.status}, not {HTTP_FORBIDDEN}")
    else:
        raise SmokeFailure("a foreign Origin was let in")
    for origin in (GOOD_ORIGIN, None):  # a program sends no Origin and is let in
        human = Human(port, origin)
        human.send({"type": "ping"})
        human.wait_for("pong")
        human.close()


def scenario_environment_port(port: int) -> None:
    check(port != DEFAULT_PORT, f"the server listens on {port}, the default: PORT=0 in its environment was not read")
    human = Human(port)
    human.send({"type": "ping"})
    human.wait_for("pong")
    human.close()


def http_get(port: int, path: str) -> tuple[int, str]:
    """A plain request over a raw socket (no Upgrade): the status of the answer and its text, read to the end of the stream."""
    with socket.create_connection(("127.0.0.1", port), timeout=IO_TIMEOUT_SECONDS) as connection:
        connection.sendall(f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
        chunks = []
        while chunk := connection.recv(4096):
            chunks.append(chunk)
    text = b"".join(chunks).decode(errors="replace")
    status_line = text.split("\r\n", 1)[0].split(" ")
    check(len(status_line) >= 2 and status_line[1].isdigit(), f"GET {path} got no HTTP answer: {text!r}")
    return int(status_line[1]), text


def scenario_health(port: int) -> None:
    status, text = http_get(port, HEALTH_PATH)
    check(status == HTTP_OK, f"GET {HEALTH_PATH} got {status}, not {HTTP_OK}: {text!r}")
    check(text.endswith("\r\n\r\nok"), f"the health answer has no body `ok`: {text!r}")
    status, text = http_get(port, "/")
    check(status == HTTP_BAD_REQUEST, f"a plain GET / got {status}, not {HTTP_BAD_REQUEST}: {text!r}")


def scenario_probe(server: RoomServer, probe: Path | None) -> None:
    assert probe is not None
    with Probe(probe, server.port) as running:
        code = running.finish(PROBE_LIMIT_SECONDS)
        check(code == 0 and PROBE_OK in running.output(), f"the probe exited with {code}\n--- probe output ---\n{running.output()}")


def scenario_probe_gone(server: RoomServer, probe: Path | None) -> None:
    assert probe is not None
    with Probe(probe, server.port) as running:
        running.wait_for_line(PROBE_FIRST_VIEW)
        server.kill()
        code = running.finish(PROBE_GONE_LIMIT_SECONDS)
        check(code != 0 and PROBE_FAILED in running.output(), f"the probe exited with {code} after its server was killed\n--- probe output ---\n{running.output()}")
        check(PROBE_OK not in running.output(), "the probe reported a finished game although its server was killed")


def by_port(scenario: Callable[[int], None]) -> Callable[[RoomServer, Path | None], None]:
    return lambda server, _probe: scenario(server.port)


# (name, server arguments, scenario, whether it needs the probe, the server's extra environment)
SCENARIOS: list[tuple[str, list[str], Callable[[RoomServer, Path | None], None], bool, dict[str, str]]] = [
    ("hand", [], by_port(scenario_hand), False, {}),
    ("reconnect", [], by_port(scenario_reconnect), False, {}),
    ("leaky", [], by_port(scenario_leaky), False, {}),
    ("origin", ["--origin", GOOD_ORIGIN], by_port(scenario_origin), False, {}),
    ("environment-port", [], by_port(scenario_environment_port), False, {"PORT": "0"}),
    ("health", [], by_port(scenario_health), False, {}),
    ("probe", [], scenario_probe, True, {}),
    # The probe sends no Origin, and a server that lists only a web page lets a program in
    ("probe-no-origin", ["--origin", GOOD_ORIGIN], scenario_probe, True, {}),
    ("probe-server-gone", ["--computer-delay-ms", str(PROBE_GONE_COMPUTER_DELAY_MS)], scenario_probe_gone, True, {}),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, help="the BagiRoomServer executable (or --preset)")
    parser.add_argument("--preset", help="the CMake preset that built it: the executable is build/<preset>/bin/BagiRoomServer")
    parser.add_argument("--probe", type=Path, help="the BagiRoomClientProbe executable; without it (or with --preset, when it is not built) the probe scenarios are skipped")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    args = parser.parse_args()

    if (args.exe is None) == (args.preset is None):
        parser.error("give the executable either as --exe or as --preset")
    exe = args.exe or preset_executable(args.preset, SERVER_NAME, args.root)
    if not exe.is_file():
        print(f"SKIPPED: {exe} does not exist")
        return EXIT_SKIPPED

    probe = args.probe or (preset_executable(args.preset, PROBE_EXE_NAME, args.root) if args.preset else None)
    if probe is not None and not probe.is_file():
        print(f"the probe {probe} does not exist")
        probe = None
    if probe is None:
        print("no probe: the probe scenarios are skipped")

    for name, flags, scenario, needs_probe, environment in SCENARIOS:
        if needs_probe and probe is None:
            print(f"{name}: skipped (no probe)", flush=True)
            continue
        print(f"{name}: running", flush=True)
        try:
            with RoomServer(exe.resolve(), flags, environment) as server:
                scenario(server, probe.resolve() if probe else None)
        except (SmokeFailure, OSError, ConnectionClosed, HandshakeError, ValueError) as error:
            print(f"FAILED ({name}): {error}")
            return EXIT_FAILED

    print("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
