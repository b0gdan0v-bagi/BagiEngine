#!/usr/bin/env python3
"""Checks the room server image: builds it, runs it as a platform would and plays against it. Manual (the build takes minutes), not a ctest entry.

    python3 tools/rooms/docker_check.py                      # from the engine root: builds tools/rooms/Dockerfile, runs, checks
    python3 tools/rooms/docker_check.py --image bagi-room-server   # an image that exists: no build

The container gets PORT, BAGI_ROOMS_TEST_GAMES (the `cards` game), BAGI_ROOMS_ORIGINS and BAGI_ROOMS_SHUTDOWN_GRACE_MS; its port is published on a free
port of 127.0.0.1. Checks, in order: the listening line for PORT and the settings line in `docker logs`; GET /healthz is 200 `ok`; a foreign Origin
gets 403, the listed one and a program without one get in; a `cards` room is created (`joined`); `docker stop` ends the container with exit code 0 inside
its time and the connected client is closed with 1001 `server_stopping`. The container is always removed; the image stays (the next run builds from cache).
Exit code: 0 passed, 1 failed (the message and the tail of the container log), 77 skipped (no docker, or no daemon).
Standard library only, Python 3.10+; the WebSocket client is tools/control/ (through smoke.py's Human).
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Callable

import smoke
from smoke import ConnectionClosed, HandshakeError, Human, SmokeFailure, check

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

ENGINE_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DOCKERFILE = Path("tools/rooms/Dockerfile")
DEFAULT_TAG = "bagi-room-server-check"
CONTAINER_PORT = 9090  # not the image's 8080: the check proves that PORT is read
GOOD_ORIGIN = "https://a.example"
EVIL_ORIGIN = "https://evil.example"
SHUTDOWN_GRACE_MS = 3000
STOP_TIMEOUT_SECONDS = 10  # docker kills the container after this: an exit code of 137 means the server did not end in time
STARTUP_SECONDS = 30.0
LOG_POLL_SECONDS = 0.2
LOG_TAIL_LINES = 40
STOPPED_LINE = "Room server: stopped"
GAME = "cards"
LOOPBACK = "127.0.0.1"

# (arguments of docker, whether its output is captured) -> something with returncode, stdout, stderr
Runner = Callable[..., "subprocess.CompletedProcess[str]"]


def run_docker(arguments: list[str], capture: bool = True) -> "subprocess.CompletedProcess[str]":
    """The one place that calls docker (a test passes a fake). Not captured, the output goes to this console (a build runs for minutes)."""
    return subprocess.run(["docker", *arguments], capture_output=capture, text=True, check=False)


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind((LOOPBACK, 0))
        return probe.getsockname()[1]


def container_name() -> str:
    return f"bagi-room-check-{os.getpid()}-{time.time_ns() % 1_000_000}"


def combined_output(result: "subprocess.CompletedProcess[str]") -> str:
    return (result.stdout or "") + (result.stderr or "")


def listening_port(log: str) -> int | None:
    """The port of the server's `BagiRoomServer listening on ws://...` line, or None."""
    match = smoke.LISTENING_PATTERN.search(log)
    return int(match.group(1)) if match else None


class DockerCheck:
    def __init__(self, arguments: argparse.Namespace, runner: Runner = run_docker):
        self.arguments = arguments
        self.docker = runner
        self.name = container_name()
        self.host_port = 0

    def log_tail(self) -> str:
        lines = combined_output(self.docker(["logs", "--tail", str(LOG_TAIL_LINES), self.name])).splitlines()
        return "\n".join(lines[-LOG_TAIL_LINES:])

    def fail(self, message: str) -> SmokeFailure:
        return SmokeFailure(f"{message}\n--- container log (tail) ---\n{self.log_tail()}")

    def build(self) -> str:
        if self.arguments.image:
            return self.arguments.image
        print(f"building {DEFAULT_TAG} from {self.arguments.dockerfile} (context {self.arguments.context})", flush=True)
        built = self.docker(["build", "-f", str(self.arguments.dockerfile), "-t", DEFAULT_TAG, str(self.arguments.context)], capture=False)
        check(built.returncode == 0, f"docker build failed with exit code {built.returncode}")
        return DEFAULT_TAG

    def start(self, image: str) -> None:
        self.host_port = pick_free_port()
        environment = {
            "PORT": str(CONTAINER_PORT),
            "BAGI_ROOMS_TEST_GAMES": "1",
            "BAGI_ROOMS_ORIGINS": GOOD_ORIGIN,
            "BAGI_ROOMS_SHUTDOWN_GRACE_MS": str(SHUTDOWN_GRACE_MS),
        }
        flags = [item for key, value in environment.items() for item in ("-e", f"{key}={value}")]
        started = self.docker(["run", "-d", "--name", self.name, "-p", f"{LOOPBACK}:{self.host_port}:{CONTAINER_PORT}", *flags, image])
        check(started.returncode == 0, f"docker run failed: {combined_output(started).strip()}")

    def is_running(self) -> bool:
        return self.docker(["inspect", "-f", "{{.State.Running}}", self.name]).stdout.strip() == "true"

    def wait_for_logs(self) -> None:
        deadline = time.monotonic() + STARTUP_SECONDS
        while True:
            log = combined_output(self.docker(["logs", self.name]))
            if listening_port(log) is not None:
                break
            if not self.is_running():
                raise self.fail("the container ended before it printed its listening line")
            if time.monotonic() >= deadline:
                raise self.fail(f"no listening line in {STARTUP_SECONDS} s")
            time.sleep(LOG_POLL_SECONDS)
        check(listening_port(log) == CONTAINER_PORT, f"the server listens on {listening_port(log)}, not on PORT={CONTAINER_PORT}\n{log}")
        check(smoke.SETTINGS_LINE in log, f"the settings line is not in the log\n{log}")
        check(GOOD_ORIGIN in log, f"the settings line does not show BAGI_ROOMS_ORIGINS\n{log}")

    def check_health(self) -> None:
        status, text = smoke.http_get(self.host_port, smoke.HEALTH_PATH)
        check(status == smoke.HTTP_OK, f"GET {smoke.HEALTH_PATH} got {status}, not {smoke.HTTP_OK}: {text!r}")
        check(text.endswith("\r\n\r\nok"), f"the health answer has no body `ok`: {text!r}")

    def check_origins(self) -> None:
        try:
            smoke.BagiControlClient(port=self.host_port, timeout=smoke.IO_TIMEOUT_SECONDS, origin=EVIL_ORIGIN, host=LOOPBACK).connect()
        except HandshakeError as refused:
            check(refused.status == smoke.HTTP_FORBIDDEN, f"a foreign Origin got HTTP {refused.status}, not {smoke.HTTP_FORBIDDEN}")
        else:
            raise SmokeFailure("a foreign Origin was let in")
        for origin in (GOOD_ORIGIN, None):
            human = Human(self.host_port, origin)
            human.send({"type": "ping"})
            human.wait_for("pong")
            human.close()

    def open_room(self) -> Human:
        host = Human(self.host_port)
        host.send({"type": "create_room", "game": GAME, "name": "Host", "options": {}})
        joined = host.wait_for("joined")
        check(bool(joined.get("room")), f"`joined` names no room: {joined}")
        return host

    def stop_and_check(self, host: Human) -> None:
        """docker stop (SIGTERM) in a thread while this one watches the client: it has to be closed with 1001 before the container ends."""
        result = SimpleNamespace(stopped=None)
        started = time.monotonic()
        stopper = threading.Thread(target=lambda: setattr(result, "stopped", self.docker(["stop", "-t", str(STOP_TIMEOUT_SECONDS), self.name])))
        stopper.start()
        while host.closed is None and time.monotonic() - started < STOP_TIMEOUT_SECONDS:
            host.poll(0.05)
        stopper.join()
        elapsed = time.monotonic() - started
        check(host.closed is not None, f"the client was not closed by `docker stop`\n{self.log_tail()}")
        check(host.closed.code == smoke.CLOSE_GOING_AWAY and host.closed.reason == smoke.STOPPING_REASON, f"the client was closed with {host.closed}, not {smoke.CLOSE_GOING_AWAY} {smoke.STOPPING_REASON}")
        host.close()
        check(result.stopped is not None and result.stopped.returncode == 0, f"docker stop failed: {combined_output(result.stopped) if result.stopped else 'did not return'}")
        code = self.docker(["inspect", "-f", "{{.State.ExitCode}}", self.name]).stdout.strip()
        check(code == "0", f"the container ended with exit code {code}, not 0 (137 is a kill after {STOP_TIMEOUT_SECONDS} s)\n{self.log_tail()}")
        check(elapsed < STOP_TIMEOUT_SECONDS, f"the container took {elapsed:.1f} s to stop")
        check(STOPPED_LINE in combined_output(self.docker(["logs", self.name])), f"no `{STOPPED_LINE}` line: the server was not ended by its own shutdown\n{self.log_tail()}")

    def run(self) -> None:
        image = self.build()
        try:
            self.start(image)
            self.wait_for_logs()
            print("listening and settings lines: ok", flush=True)
            self.check_health()
            print("health: ok", flush=True)
            self.check_origins()
            print("origins: ok", flush=True)
            host = self.open_room()
            print("room: ok", flush=True)
            self.stop_and_check(host)
            print("stop: ok", flush=True)
        finally:
            self.docker(["rm", "-f", self.name])


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image", help="an image that exists (no build); default: build tools/rooms/Dockerfile")
    parser.add_argument("--context", type=Path, default=ENGINE_ROOT, help="the build context (default: the engine root; a game's root with --dockerfile)")
    parser.add_argument("--dockerfile", type=Path, default=None, help="default: tools/rooms/Dockerfile of the context")
    arguments = parser.parse_args(argv)
    if arguments.dockerfile is None:
        arguments.dockerfile = arguments.context / DEFAULT_DOCKERFILE
    return arguments


def run(argv: list[str], runner: Runner = run_docker, which: Callable[[str], str | None] = shutil.which) -> int:
    arguments = parse_arguments(argv)
    if which("docker") is None:
        print("SKIPPED: docker is not installed")
        return EXIT_SKIPPED
    try:
        daemon = runner(["info"])
    except OSError as error:
        print(f"SKIPPED: docker cannot be started: {error}")
        return EXIT_SKIPPED
    if daemon.returncode != 0:
        print("SKIPPED: the docker daemon does not answer")
        return EXIT_SKIPPED
    try:
        DockerCheck(arguments, runner).run()
    except (SmokeFailure, OSError, ConnectionClosed, HandshakeError, ValueError) as error:
        print(f"FAILED: {error}")
        return EXIT_FAILED
    print("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
