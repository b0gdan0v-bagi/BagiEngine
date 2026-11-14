"""Starts a game executable with the control API and connects to it. Standard library only.

    from game_process import GameProcess, build_game_command, free_port, preset_executable

    exe = preset_executable("macos-debug", "BagiStress")  # build/macos-debug/bin/BagiStress, or any path of a built game
    port = free_port()
    with GameProcess(build_game_command(exe, port), cwd=exe.parent) as game:
        client = game.connect()
        ...
        client.call("quit")

The game writes its output to a temporary file; log_tail() gives the end of it for an error message.
"""

import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Sequence

from bagi_control_client import BagiControlClient

DEFAULT_STARTUP_TIMEOUT = 30.0
DEFAULT_SHUTDOWN_TIMEOUT = 10.0
CONNECT_RETRY_SECONDS = 0.1
LOG_TAIL_LINES = 25
PORT_TAKEN_MARKER = "cannot listen on 127.0.0.1:"  # what the game logs when it cannot bind the control port (Application::StartControlServer)
# A log line is "[Warning] [12:00:00.000] text", with the level tag wrapped in an ANSI colour when the console sink colours (ConsoleSink.cpp)
ANSI_COLOR = re.compile(r"\x1b\[[0-9;]*m")
LOG_PROBLEM_PATTERN = re.compile(r"^\[(?:Warning|Error|Fatal)\]")
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]  # tools/control/game_process.py


class GameLaunchError(Exception):
    """The game did not start, exited early or did not bring the control API up in time; the message has the end of its log."""


def free_port() -> int:
    """A TCP port on 127.0.0.1 that nobody listens on right now."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def preset_executable(preset: str, game: str, root: Path | None = None) -> Path:
    """Where the build of `preset` puts the executable of `game`: <root>/build/<preset>/bin/<game>[.exe] (cmake/CLAUDE.md, "Output").

    A multi-config preset (Visual Studio) puts it in bin/<Config>/; then the newest one that exists is given. `root` is the
    repository whose build/ folder is meant (default: the one holding these tools). The file may not exist: the caller reports it.
    """
    name = f"{game}.exe" if sys.platform == "win32" else game
    bin_dir = (root or REPOSITORY_ROOT) / "build" / preset / "bin"
    single_config = bin_dir / name
    if single_config.is_file() or not bin_dir.is_dir():
        return single_config
    per_config = [path for path in (config / name for config in bin_dir.iterdir() if config.is_dir()) if path.is_file()]
    return max(per_config, key=lambda path: path.stat().st_mtime) if per_config else single_config


def log_tail(log_path: Path, lines: int = LOG_TAIL_LINES) -> str:
    try:
        text = log_path.read_text(errors="replace").splitlines()
    except OSError:
        return "(no log)"
    return "\n".join(text[-lines:])


def log_problems(log_path: Path) -> list[str]:
    """The lines of the game's log whose level is Warning, Error or Fatal (the colour codes are removed)."""
    try:
        lines = log_path.read_text(errors="replace").splitlines()
    except OSError:
        return []
    plain = (ANSI_COLOR.sub("", line) for line in lines)
    return [line for line in plain if LOG_PROBLEM_PATTERN.match(line)]


def build_game_command(exe: Path, port: int, headless: bool = True, manual_clock: bool = True, extra: Sequence[str] = ()) -> list[str]:
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

    def log_problems(self) -> list[str]:
        return log_problems(self.log_path)

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
