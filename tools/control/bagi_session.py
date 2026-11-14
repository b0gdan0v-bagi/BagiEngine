"""The game session behind the MCP tools: starts or reaches one game and turns its control API into plain Python calls.

No MCP code in here (bagi_mcp.py maps these methods onto tools), so it runs and is tested without the `mcp` package.
Standard library only.

    session = GameSession()
    session.launch(preset="macos-debug", game="BagiStress")  # or launch("<path of a built game>")
    session.call("perform_action", {"type": "set_count", "count": 1000})  # the game's own action
    png, size = session.screenshot()
    session.quit()

An error that the game answers is raised as ControlError (its text is "code: message"); everything the session itself
refuses (no game, a lost connection, a missing executable) is a SessionError.

A session can record its calls as a scenario file (scenario.py; `launch(record=path)`, or `start_recording` / `stop_recording`):
the hook is GameSession.call, so the file holds what was called and how the game answered.
"""

import base64
import collections
import copy
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

import scenario
from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port, preset_executable

CALL_TIMEOUT_SECONDS = 60.0
STEP_SECONDS_PER_FRAME = 0.05  # a generous bound for a frame: a step of N frames may take N times this before the call gives up
LOG_LINES = 40
# Every action of a game pushes a state_changed, and nobody may read them: the newest ones are kept, the oldest are dropped
EVENT_BUFFER_SIZE = 256
EVENT_POLL_SECONDS = 0.05  # the least a take_event waits for a push that is on its way, also with a timeout of 0
QUIT_METHOD = "quit"
STEP_METHOD = "step"


@dataclass
class _Recording:
    path: Path
    mode: str
    header: scenario.Header
    versions: scenario.VersionTracker
    steps: list[scenario.Step]


class SessionError(Exception):
    """The session cannot do what was asked; the message says what to do instead."""


class GameSession:
    """At most one game at a time, either started by launch() or reached by connect()."""

    def __init__(self) -> None:
        self._client: BagiControlClient | None = None
        self._game: GameProcess | None = None
        self._finished: GameProcess | None = None  # the game this session stopped last: its log is still there to read
        self._flags: list[str] | None = None  # the start flags of a launched game; None for one only connected to
        self._calls_made = 0  # calls since the launch or connect, to tell a recording that started late
        self._events: collections.deque[dict[str, Any]] = collections.deque(maxlen=EVENT_BUFFER_SIZE)
        self._recording: _Recording | None = None

    @property
    def is_connected(self) -> bool:
        return self._client is not None

    @property
    def is_recording(self) -> bool:
        return self._recording is not None

    def launch(
        self,
        exe: str | None = None,
        headless: bool = True,
        manual_clock: bool = True,
        startup_timeout: float = DEFAULT_STARTUP_TIMEOUT,
        preset: str | None = None,
        game: str | None = None,
        extra: Sequence[str] = (),
        record: str | None = None,
        record_expect: str = scenario.RECORD_MINIMAL,
    ) -> dict[str, Any]:
        """Starts a game with the control API on a free port and connects to it. A game that this session started earlier is stopped.

        The game is given either as `exe` (a path) or as `preset` + `game` (build/<preset>/bin/<game>, see preset_executable); exactly one of the two.
        `extra` are more start flags for the game. `record` is the path of a scenario file to record the session to (start_recording).
        """
        by_path = bool(exe)
        by_preset = bool(preset) or bool(game)
        if by_path == by_preset or (by_preset and not (preset and game)):
            raise SessionError("name the game either by `exe` (the path of the built executable) or by `preset` and `game` together (build/<preset>/bin/<game>), not both and not neither")
        self.close()
        if record is not None:
            self._check_record_target(Path(record), record_expect)
        exe_path = Path(exe).expanduser().resolve() if exe else preset_executable(preset, game).resolve()
        if not exe_path.is_file():
            hint = f"; build the preset '{preset}' first" if preset else ""
            raise SessionError(f"there is no game executable at {exe_path}{hint}")

        port = free_port()
        process = GameProcess(build_game_command(exe_path, port, headless, manual_clock, tuple(extra)), cwd=exe_path.parent)
        try:
            process.start()
            client = process.connect(port, startup_timeout)
        except GameLaunchError as error:
            process.stop(0)
            raise SessionError(str(error)) from error
        client.timeout = CALL_TIMEOUT_SECONDS
        self._game, self._client = process, client
        self._begin_session(["--headless"] * headless + ["--manual-clock"] * manual_clock + list(extra))
        description = self._describe(port)
        if record is not None:
            self._start_recording_or_close(record, record_expect, description)
        return description

    def connect(
        self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, record: str | None = None, record_expect: str = scenario.RECORD_MINIMAL
    ) -> dict[str, Any]:
        """Connects to a game that is already running with --control. A game that this session started earlier is stopped.

        `record` is the path of a scenario file to record to; the file replays from a fresh start of the game, which this game may be past (start_recording).
        """
        self.close()
        if record is not None:
            self._check_record_target(Path(record), record_expect)
        client = BagiControlClient(host=host, port=port, timeout=CALL_TIMEOUT_SECONDS)
        try:
            client.connect()
        except OSError as error:
            raise SessionError(f"nothing accepts a control connection at {host}:{port} ({error}); start the game with --control or use launch_game") from error
        self._client = client
        self._begin_session(None)
        description = self._describe(port)
        if record is not None:
            self._start_recording_or_close(record, record_expect, description)
        return description

    def call(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> Any:
        """One control call. `timeout` (seconds) replaces the default for a call that is known to take long; a `step` gets one for its frames.

        While recording, a call the game answered (or refused with a ControlError) becomes a line of the scenario; a SessionError is not recorded.
        """
        sent = copy.deepcopy(params) if self._recording is not None else None  # the caller may reuse its dict
        if timeout is None and method == STEP_METHOD:
            timeout = self._step_timeout(params)
        try:
            result = self._call_raw(method, params, timeout)
        except ControlError as error:
            self._calls_made += 1
            self._record(method, sent, scenario.Outcome(error_code=error.code, error_message=error.message))
            raise
        self._calls_made += 1
        recording = self._recording
        if recording is not None:
            self._record(method, sent, scenario.Outcome(result=result, version_step=recording.versions.step_of(result)))
        return result

    def _call_raw(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> Any:
        client = self._client
        if client is None:
            raise SessionError("no game is connected: call launch_game or connect first")
        previous_timeout = client.timeout
        if timeout is not None:
            client.timeout = timeout
        try:
            return client.call(method, params)
        except (ConnectionClosed, ConnectionResetError, ConnectionAbortedError, BrokenPipeError) as error:
            # A game that is gone reads as a close frame or the end of the stream, and as a reset (Windows: WinError 10053 / 10054) when requests were in flight
            self._drop_client()
            raise SessionError(f"the game closed the connection ({error}); {self._exit_note()}") from error
        except TimeoutError as error:
            # The answer may still come and would be taken for the answer of the next call, so this connection is done
            self._drop_client()
            raise SessionError("the game did not answer in time; the connection was dropped (use connect to reach the game again)") from error
        except OSError as error:
            self._drop_client()
            raise SessionError(f"lost the connection to the game ({error})") from error
        finally:
            client.timeout = previous_timeout
            self._events.extend(client.events)
            client.events.clear()

    def step(self, frames: int = 1, dt: float | None = None) -> Any:
        """Runs `frames` frames of `dt` seconds on the manual clock (on the real clock it waits for that many frames)."""
        params: dict[str, Any] = {"frames": frames}
        if dt is not None:
            params["dt"] = dt
        return self.call(STEP_METHOD, params)

    def screenshot(self, max_size: int | None = None) -> tuple[bytes, dict[str, int]]:
        """The PNG of the next frame and its size. `max_size` shrinks the longer side of a bigger picture (the game's default is 1024)."""
        result = self.call("screenshot", None if max_size is None else {"maxSize": max_size})
        return base64.b64decode(result["png_base64"]), {"width": result["width"], "height": result["height"]}

    def game_log(self, lines: int = LOG_LINES) -> str:
        """The end of the output of the game this session started; a game it only connected to has none."""
        if self._game is None:
            raise SessionError("this session did not start the game, so it has no log; launch_game starts one")
        return self._game.log_tail(lines)

    def take_event(self, match: Callable[[dict[str, Any]], bool], timeout: float = 0.0) -> dict[str, Any] | None:
        """Takes the first push for which `match(event)` is true, from those kept or from the connection within `timeout` seconds; None if none came.

        The push is removed from the kept ones. Pushes that do not match are kept (up to EVENT_BUFFER_SIZE, the oldest go first).
        """
        client = self._client
        if client is None:
            raise SessionError("no game is connected: call launch_game or connect first")
        deadline = time.monotonic() + timeout
        polled = False
        while True:
            for event in self._events:
                if match(event):
                    self._events.remove(event)
                    return event
            remaining = deadline - time.monotonic()
            if polled and remaining <= 0:
                return None
            polled = True
            try:
                message = client.receive_message(max(remaining, EVENT_POLL_SECONDS))
            except TimeoutError:
                continue
            except (ConnectionClosed, OSError) as error:
                self._drop_client()
                raise SessionError(f"the game closed the connection ({error}); {self._exit_note()}") from error
            if "event" in message:
                self._events.append(message)

    def log_problems(self) -> list[str]:
        """The Warning, Error and Fatal lines the game this session started (the last one, also after quit) has logged so far.

        The game's stdout is block-buffered into the log file, so a Warning may show only after the game has exited; Error and Fatal go to stderr, unbuffered.
        """
        game = self._game or self._finished
        if game is None:
            raise SessionError("this session did not start the game, so it has no log; launch_game starts one")
        return game.log_problems()

    def start_recording(self, path: str, expect: str = scenario.RECORD_MINIMAL) -> dict[str, Any]:
        """Records the calls from now on into the scenario file `path` (written by stop_recording, quit and close).

        `expect` is "minimal" (error codes and version steps) or "full" (the answers too), see scenario.py. The file replays from a fresh
        start of the game: calls made before the recording are not in it, which the answer says when there were any.
        """
        target = Path(path)
        self._check_record_target(target, expect)
        if self._client is None:
            raise SessionError("no game is connected: call launch_game or connect first")
        if self._recording is not None:
            raise SessionError(f"already recording to {self._recording.path}; call record_stop first")
        active = self._call_raw("list_games").get("active")
        if not active:
            raise SessionError("the game reports no active game, so the scenario header cannot name it")
        flags = self._flags if self._flags is not None else (["--manual-clock"] if self._call_raw("status").get("manualClock") else [])
        header = scenario.Header(scenario.SCENARIO_VERSION, active, list(flags))
        self._recording = _Recording(target.expanduser().resolve(), expect, header, scenario.VersionTracker(), [])
        answer: dict[str, Any] = {"path": str(self._recording.path), "expect": expect, "game": active, "flags": header.flags}
        if self._game is None:
            answer["note"] = "this game was not started by the session, so it may be past its start: the file replays from a fresh start of the game"
        elif self._calls_made > 0:
            answer["note"] = f"{self._calls_made} call(s) were made before the recording began; they are not in the file, which replays from a fresh start"
        return answer

    def stop_recording(self) -> dict[str, Any]:
        """Writes the scenario file and ends the recording: gives its `path` and the number of `calls` in it."""
        if self._recording is None:
            raise SessionError("not recording: call record_start first")
        return self._finish_recording()

    def quit(self) -> dict[str, Any]:
        """Asks the game to quit, waits for the process (a launched one) and forgets it. A recording is written and reported as `recording`."""
        result: dict[str, Any] = {}
        if self._client is not None:
            try:
                result = self.call(QUIT_METHOD) or {}
            except (ControlError, SessionError):
                pass  # it is going away either way
        exit_code = self._stop_game(DEFAULT_SHUTDOWN_TIMEOUT)
        self._drop_client()
        if exit_code is not None:
            result["exitCode"] = exit_code
        if self._recording is not None:
            result["recording"] = self._finish_recording()
        return result

    def close(self) -> None:
        """Lets go of the game: a launched one is killed at once, one that was only connected to keeps running. A recording is written.

        A recording that cannot be written is dropped without an error: `launch` and `connect` call this first, and the old file must not stop a
        new game (`stop_recording` and `quit` report a write that fails).
        """
        self._drop_client()
        self._stop_game(0)
        if self._recording is not None:
            try:
                self._finish_recording()
            except SessionError:
                pass

    def _begin_session(self, flags: list[str] | None) -> None:
        self._flags = flags
        self._finished = None
        self._calls_made = 0
        self._events.clear()

    def _describe(self, port: int) -> dict[str, Any]:
        games = self._call_raw("list_games")
        return {"port": port, "status": self._call_raw("status"), "activeGame": games.get("active"), "games": games.get("games", [])}

    @staticmethod
    def _step_timeout(params: dict[str, Any] | None) -> float:
        frames = params.get("frames") if isinstance(params, dict) else None
        count = frames if isinstance(frames, int) and not isinstance(frames, bool) else 1
        return CALL_TIMEOUT_SECONDS + max(count, 0) * STEP_SECONDS_PER_FRAME

    @staticmethod
    def _check_record_target(path: Path, expect: str) -> None:
        if expect not in scenario.RECORD_MODES:
            raise SessionError(f"expect has to be one of {', '.join(scenario.RECORD_MODES)}, not '{expect}'")
        folder = path.expanduser().resolve().parent
        if not folder.is_dir():
            raise SessionError(f"cannot record to {path}: the folder {folder} does not exist")

    def _start_recording_or_close(self, path: str, expect: str, description: dict[str, Any]) -> None:
        try:
            description["recording"] = self.start_recording(path, expect)
        except SessionError:
            self.close()
            raise

    def _record(self, method: str, params: dict[str, Any] | None, outcome: scenario.Outcome) -> None:
        recording = self._recording
        if recording is not None:
            expect = scenario.full_expect(outcome) if recording.mode == scenario.RECORD_FULL else scenario.minimal_expect(outcome)
            recording.steps.append(scenario.Step(method, params, expect))

    def _finish_recording(self) -> dict[str, Any]:
        recording, self._recording = self._recording, None
        assert recording is not None
        steps = recording.steps
        if steps and steps[-1].method == QUIT_METHOD:
            steps.pop()  # the player stops the game itself
        try:
            scenario.dump(recording.path, recording.header, steps)
        except OSError as error:
            raise SessionError(f"cannot write the scenario to {recording.path}: {error}") from error
        return {"path": str(recording.path), "calls": len(steps)}

    def _drop_client(self) -> None:
        client, self._client = self._client, None
        if client is not None:
            try:
                client.close()
            except (OSError, ConnectionClosed):
                pass

    def _stop_game(self, timeout: float) -> int | None:
        game, self._game = self._game, None
        if game is None:
            return None
        self._finished = game
        return game.stop(timeout)

    def _exit_note(self) -> str:
        if self._game is None:
            return "start it again with launch_game or connect"
        code = self._game.exit_code
        return "the process is still running" if code is None else f"the process exited with code {code}, game_log has its last lines"
