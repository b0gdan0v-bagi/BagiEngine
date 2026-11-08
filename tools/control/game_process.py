"""Starts a game executable with the control API and connects to it. Standard library only.

    from game_process import GameProcess, build_game_command, free_port

    port = free_port()
    with GameProcess(build_game_command(Path("build/ClangmacOS/BagiMatch3"), port), cwd=Path("build/ClangmacOS")) as game:
        client = game.connect()
        ...
        client.call("quit")

The game writes its output to a temporary file; log_tail() gives the end of it for an error message.
"""

import socket
import subprocess
import tempfile
import time
from pathlib import Path

from bagi_control_client import BagiControlClient

DEFAULT_STARTUP_TIMEOUT = 30.0
DEFAULT_SHUTDOWN_TIMEOUT = 10.0
CONNECT_RETRY_SECONDS = 0.1
LOG_TAIL_LINES = 25


class GameLaunchError(Exception):
    """The game did not start, exited early or did not bring the control API up in time; the message has the end of its log."""


def free_port() -> int:
    """A TCP port on 127.0.0.1 that nobody listens on right now."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def log_tail(log_path: Path, lines: int = LOG_TAIL_LINES) -> str:
    try:
        text = log_path.read_text(errors="replace").splitlines()
    except OSError:
        return "(no log)"
    return "\n".join(text[-lines:])


def build_game_command(exe: Path, port: int, headless: bool = True, manual_clock: bool = True, extra: tuple[str, ...] = ()) -> list[str]:
    """The command line that runs `exe` with the control API on `port` (flags: src/Modules/RemoteControl/CLAUDE.md)."""
    command = [str(exe), "--control", f"--control-port={port}"]
    if headless:
        command.append("--headless")
    if manual_clock:
        command.append("--manual-clock")
    command.extend(extra)
    return command


class GameProcess:
    """One running game. Use it as a context manager or call stop(); the process is killed when it does not quit in time."""

    def __init__(self, command: list[str], cwd: Path | None = None):
        self.command = command
        self.cwd = cwd
        self._process: subprocess.Popen | None = None
        self._exit_code: int | None = None
        self._log_dir = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)  # Windows: a killed .bat leaves its child holding the log
        self.log_path = Path(self._log_dir.name) / "game.log"

    def __enter__(self) -> "GameProcess":
        self.start()
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.stop()

    @property
    def exit_code(self) -> int | None:
        """None while the game runs (or was never started)."""
        return self._exit_code if self._process is None else self._process.poll()

    def start(self) -> None:
        try:
            with open(self.log_path, "wb") as log:
                self._process = subprocess.Popen(self.command, cwd=self.cwd, stdout=log, stderr=subprocess.STDOUT)
        except OSError as error:
            raise GameLaunchError(f"cannot start {self.command[0]}: {error}") from error

    def connect(self, port: int, timeout: float = DEFAULT_STARTUP_TIMEOUT) -> BagiControlClient:
        """Retries until the game listens on `port`; fails at once when the game exits instead."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self._process is None or self._process.poll() is not None:
                raise GameLaunchError(f"the game exited with code {self.exit_code} before the control API was up\n--- game log (tail) ---\n{self.log_tail()}")
            client = BagiControlClient(port=port, timeout=timeout)
            try:
                client.connect()
                return client
            except (ConnectionRefusedError, ConnectionResetError):
                time.sleep(CONNECT_RETRY_SECONDS)
        raise GameLaunchError(f"the control API did not come up in {timeout} s\n--- game log (tail) ---\n{self.log_tail()}")

    def log_tail(self, lines: int = LOG_TAIL_LINES) -> str:
        return log_tail(self.log_path, lines)

    def stop(self, timeout: float = DEFAULT_SHUTDOWN_TIMEOUT) -> int | None:
        """Waits `timeout` seconds for the game to exit (after a `quit` call), then kills it; 0 kills at once. Gives the exit code.

        The log stays readable afterwards; it goes away with this object.
        """
        process = self._process
        if process is None:
            return None
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        self._process = None
        self._exit_code = process.returncode
        return self._exit_code
