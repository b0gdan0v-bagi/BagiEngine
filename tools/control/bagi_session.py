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
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Sequence

import scenario
from bagi_control_client import DEFAULT_HOST, DEFAULT_PORT, BagiControlClient, ConnectionClosed, ControlError
from game_process import DEFAULT_SHUTDOWN_TIMEOUT, DEFAULT_STARTUP_TIMEOUT, GameLaunchError, GameProcess, build_game_command, free_port, preset_executable

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shots"))

import png_ops  # noqa: E402

CALL_TIMEOUT_SECONDS = 60.0
STEP_SECONDS_PER_FRAME = 0.05  # a generous bound for a frame: a step of N frames may take N times this before the call gives up
LOG_LINES = 40
# Every action of a game pushes a state_changed, and nobody may read them: the newest ones are kept, the oldest are dropped
EVENT_BUFFER_SIZE = 256
EVENT_POLL_SECONDS = 0.05  # the least a take_event waits for a push that is on its way, also with a timeout of 0
QUIT_METHOD = "quit"
STEP_METHOD = "step"
BATCH_METHOD = "batch"
RUN_UNTIL_METHOD = "run_until"
DEFAULT_RUN_UNTIL_FRAMES = 600  # the game's default for maxFrames, which the timeout of a run_until counts when none is given
LOG_LEVEL_WARNING = "warning"
SET_WINDOW_METHOD = "set_window"
IMGUI_INPUT_METHOD = "imgui_input"
IMGUI_FIND_METHOD = "imgui_find"
IMGUI_WAIT_FRAMES = 600  # kMaxImGuiWaitFrames: the frames the game's own wait may run before it answers imgui_timeout
WINDOW_FLAG = "--window="
DENSITY_FLAG = "--density="
EDITOR_FLAG = "--editor"
CONTENT_OVERLAY_FLAG = "--content-overlay="
EDITOR_STATE_METHOD = "editor_state"
NOT_EDITOR_CODE = "not_editor"
WINDOW_SIZE_PATTERN = re.compile(r"\d+x\d+")  # "WxH", the form of the engine's --window flag
SAFE_AREA_SIDES = ("top", "right", "bottom", "left")


@dataclass
class _Recording:
    path: Path
    mode: str
    header: scenario.Header
    versions: scenario.VersionTracker
    steps: list[scenario.Step]


def pointer_equals(pointer: str, equals: Any) -> dict[str, Any]:
    """A `run_until` condition: the JSON pointer into what get_state answers (`{game, version, state}`, e.g. "/state/score") equals `equals`."""
    return {"pointer": pointer, "equals": equals}


def version_moved() -> dict[str, Any]:
    """A `run_until` condition: the state version of the active game differs from the one when the call began."""
    return {"versionMoved": True}


def event_occurs(name: str, equal: dict[str, Any] | None = None) -> dict[str, Any]:
    """A `run_until` condition: a push called `name` whose data has the members of `equal` arrives after the call began."""
    event: dict[str, Any] = {"name": name}
    if equal:
        event["equal"] = equal
    return {"event": event}


class SessionError(Exception):
    """The session cannot do what was asked; the message says what to do instead."""


class GameSession:
    """At most one game at a time, either started by launch() or reached by connect().

    `client_name` is what the game's activity journal calls the connections of this session; none by default. It is no call of the session, so a recording never holds it.
    """

    def __init__(self, client_name: str | None = None) -> None:
        self._client_name = client_name
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
        window: str | None = None,
        density: float | None = None,
        editor: bool = False,
        content_overlay: str | None = None,
    ) -> dict[str, Any]:
        """Starts a game with the control API on a free port and connects to it. A game that this session started earlier is stopped.

        The game is given either as `exe` (a path) or as `preset` + `game` (build/<preset>/bin/<game>, see preset_executable); exactly one of the two.
        `window` ("WxH" in window units) and `density` are the start size and pixel density of a headless window (`--window=`, `--density=`), which
        set_window changes later. `editor` starts the game in the Editor run mode (`--editor`; a build without the editor runs Game: `status.runMode` says which). `content_overlay` is a folder (made when missing) that
        every write through the config save, the node-file save and the mounted paths goes into instead of the content files, in either run mode (`--content-overlay=`; the Generate panel's presets and sets and imgui.ini are not redirected); it has to lie outside every git work tree, so that a QA session never leaves a change
        in a repository, and it is not part of a recording's header (a path of this machine). `extra` are more start flags for the game. `record` is the path of a scenario file to record the session to (start_recording).
        """
        by_path = bool(exe)
        by_preset = bool(preset) or bool(game)
        if by_path == by_preset or (by_preset and not (preset and game)):
            raise SessionError("name the game either by `exe` (the path of the built executable) or by `preset` and `game` together (build/<preset>/bin/<game>), not both and not neither")
        window_flags = self._window_flags(window, density, headless) + [EDITOR_FLAG] * editor
        overlay_flags = [] if content_overlay is None else [CONTENT_OVERLAY_FLAG + str(self._check_overlay_target(Path(content_overlay)))]
        self.close()
        if record is not None:
            self._check_record_target(Path(record), record_expect)
        exe_path = Path(exe).expanduser().resolve() if exe else preset_executable(preset, game).resolve()
        if not exe_path.is_file():
            hint = f"; build the preset '{preset}' first" if preset else ""
            raise SessionError(f"there is no game executable at {exe_path}{hint}")

        port = free_port()
        process = GameProcess(build_game_command(exe_path, port, headless, manual_clock, (*window_flags, *overlay_flags, *extra)), cwd=exe_path.parent)
        try:
            process.start()
            client = process.connect(port, startup_timeout, self._client_name)
        except (GameLaunchError, ConnectionClosed, ControlError, OSError) as error:  # the last three: the hello of a named session
            process.stop(0)
            raise SessionError(str(error)) from error
        client.timeout = CALL_TIMEOUT_SECONDS
        self._game, self._client = process, client
        self._begin_session(["--headless"] * headless + ["--manual-clock"] * manual_clock + window_flags + list(extra))
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
        client = BagiControlClient(host=host, port=port, timeout=CALL_TIMEOUT_SECONDS, name=self._client_name)
        try:
            client.connect()
        except (ConnectionClosed, OSError) as error:
            raise SessionError(f"nothing accepts a control connection at {host}:{port} ({error}); start the game with --control or use launch_game") from error
        self._client = client
        self._begin_session(None)
        description = self._describe(port)
        if record is not None:
            self._start_recording_or_close(record, record_expect, description)
        return description

    def call(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> Any:
        """One control call. `timeout` (seconds) replaces the default for a call that is known to take long; a call that waits for frames
        (`step`, `run_until`, a `batch` holding them) gets one for the frames it may wait.

        While recording, a call the game answered (or refused with a ControlError) becomes a line of the scenario; a SessionError is not recorded.
        """
        sent = copy.deepcopy(params) if self._recording is not None else None  # the caller may reuse its dict
        if timeout is None:
            timeout = self._wait_timeout(method, params)
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

    def set_window(
        self,
        width: int,
        height: int,
        density: float | None = None,
        user_scale: float | None = None,
        safe_area: dict[str, float] | Sequence[float] | None = None,
    ) -> dict[str, Any]:
        """Changes the window of a headless game and answers its `window` object (as `status` has it), one frame later.

        `width` and `height` are window units; the picture is `round(size * density)` pixels. A value left out stays as it is. `safe_area` is a dict
        `{top, right, bottom, left}` or a tuple in that order, all four given (`{0,0,0,0}` clears it). A real window answers `not_headless`; a value
        outside the engine's limits is `invalid_params` and nothing changes (.claude/rules/control-window-api.md).
        """
        params: dict[str, Any] = {"width": width, "height": height}
        if density is not None:
            params["density"] = density
        if user_scale is not None:
            params["userScale"] = user_scale
        if safe_area is not None:
            params["safeArea"] = self._safe_area_object(safe_area)
        return self.call(SET_WINDOW_METHOD, params)

    def editor_state(self) -> dict[str, Any]:
        """The unsaved changes of the editor panels: `{anyDirty, documents: [{id, kind, name, dirty, path, file}]}` sorted by `id`.

        `file` is the real file a save writes: with `launch(content_overlay=...)` the copy in the overlay, never the checkout's file. A game that does not
        run in the editor is a SessionError: launch it with `editor=True` (a build without the editor runs Game).
        """
        try:
            return self.call(EDITOR_STATE_METHOD)
        except ControlError as error:
            if error.code == NOT_EDITOR_CODE:
                raise SessionError("the game does not run in the editor run mode: launch it with editor=True (a build without the editor cannot)") from error
            raise

    def imgui_find(
        self,
        label: str | None = None,
        *,
        label_contains: str | None = None,
        id: str | None = None,
        window: str | None = None,
        kind: str | None = None,
        visible_only: bool = False,
        limit: int | None = None,
    ) -> dict[str, Any]:
        """Windows and items the editor's ImGui drew, `{frame, display, wantCapture*, matches: [{kind, id, label, window?, x, y, width, height, visible, hovered}], ...}`.

        The rects are output pixels (the pixels of `screenshot` and `click_at`). With no filter only the windows are listed; an item exists only in a frame that
        drew it, so a menu's items are found while the menu is open (.claude/rules/control-imgui-api.md). `no_imgui` when no editor runs.
        """
        params: dict[str, Any] = {}
        for name, value in (("label", label), ("labelContains", label_contains), ("id", id), ("window", window), ("kind", kind), ("limit", limit)):
            if value is not None:
                params[name] = value
        if visible_only:
            params["visibleOnly"] = True
        return self.call(IMGUI_FIND_METHOD, params)

    def imgui_input(self, events: Sequence[dict[str, Any]], space: str | None = None) -> dict[str, Any]:
        """Sends `events` (`{type: move|click|down|up|wheel|key|text, ...}`) to the editor's ImGui and answers `{events, frames, wantCapture*, wantTextInput}`
        once ImGui has taken them and a frame has reacted; `space` is "output" (default, screenshot pixels) or "imgui" (ImGui's window units)."""
        params: dict[str, Any] = {"events": list(events)}
        if space is not None:
            params["space"] = space
        return self.call(IMGUI_INPUT_METHOD, params)

    def imgui_click(
        self,
        label: str | None = None,
        *,
        x: float | None = None,
        y: float | None = None,
        window: str | None = None,
        button: str = "left",
        count: int = 1,
        mods: Sequence[str] = (),
    ) -> dict[str, Any]:
        """Clicks the centre of the one visible item called `label` (in `window`, when given), or the point `x`, `y` in output pixels; `count` 2 is a double click.

        Zero matches or several are a SessionError listing what was found (name the `window`, or use `x` and `y`); nothing is clicked then. Answers what
        `imgui_input` answers.
        """
        if label is not None and (x is not None or y is not None):
            raise SessionError("imgui_click takes a label or a point (x and y), not both")
        event: dict[str, Any] = {"type": "click"}
        if label is not None:
            found = self.imgui_find(label, window=window, kind="item", visible_only=True)
            matches = found["matches"]
            where = f" in window '{window}'" if window is not None else ""
            if not matches:
                raise SessionError(f"no visible ImGui item '{label}'{where}: an item exists only in a frame that drew it (a menu item while its menu is open)")
            if len(matches) > 1 or found.get("truncated"):
                listed = "; ".join(f"'{match['label']}' in '{match.get('window', '')}' at ({match['x']:.0f}, {match['y']:.0f})" for match in matches)
                raise SessionError(f"{len(matches)} visible ImGui items called '{label}'{where}: {listed}; name the window or click a point")
            match = matches[0]
            event["x"] = match["x"] + match["width"] / 2
            event["y"] = match["y"] + match["height"] / 2
        elif x is not None and y is not None:
            event["x"] = x
            event["y"] = y
        elif x is not None or y is not None:
            raise SessionError("imgui_click needs both x and y")
        else:
            raise SessionError("imgui_click needs a label or a point (x and y)")
        if button != "left":
            event["button"] = button
        if count != 1:
            event["count"] = count
        if mods:
            event["mods"] = list(mods)
        return self.imgui_input([event])

    def imgui_type(self, text: str) -> dict[str, Any]:
        """Types `text` into the focused ImGui text field (UTF-8; ImGui's `wantTextInput` in the answer says whether one had the focus)."""
        return self.imgui_input([{"type": "text", "text": text}])

    def imgui_key(self, key: str, mods: Sequence[str] = ()) -> dict[str, Any]:
        """Presses and releases an ImGui key (`Enter`, `Escape`, `A`, `F6`, `LeftArrow`...); `mods` are "ctrl" (Cmd on macOS), "shift", "alt", "super"."""
        event: dict[str, Any] = {"type": "key", "key": key}
        if mods:
            event["mods"] = list(mods)
        return self.imgui_input([event])

    def imgui_wheel(self, x: float, y: float, dy: float, dx: float = 0.0) -> dict[str, Any]:
        """Moves the ImGui pointer to the output pixel `x`, `y` and turns the wheel there (`dy` positive scrolls up, as SDL)."""
        wheel: dict[str, Any] = {"type": "wheel", "dy": dy}
        if dx:
            wheel["dx"] = dx
        return self.imgui_input([{"type": "move", "x": x, "y": y}, wheel])

    def log_counts(self) -> dict[str, int]:
        """The warnings and errors the game has logged since its start: `{warnings, errors}` (`log` of `status`)."""
        return self.call("status")["log"]

    def log_mark(self) -> dict[str, Any]:
        """A mark of the game's log now, `{mark, warnings, errors}`: pass `mark` to log_since later to see only what was logged after it."""
        return self.call("log_mark")

    def log_since(self, mark: int, level: str = LOG_LEVEL_WARNING) -> dict[str, Any]:
        """The warnings and errors logged since `mark`: `{lines, warnings, errors, dropped, next, truncated?}`.

        `level` is "warning" (warnings and errors) or "error" (errors only). `dropped` lines fell out of the game's bounded history, so the counts are then lower bounds.
        """
        return self.call("log_since", {"mark": mark, "level": level})

    def log_subscribe(self, enabled: bool, level: str = LOG_LEVEL_WARNING) -> dict[str, Any]:
        """Turns the push of the event `log` on or off for the whole game (every client gets it); read it with take_event."""
        return self.call("log_subscribe", {"enabled": enabled, "level": level})

    def batch(self, calls: Sequence[tuple[str, dict[str, Any] | None] | dict[str, Any]], continue_on_error: bool = False) -> dict[str, Any]:
        """Runs `calls` in order in one request and answers `{results, stopped?}` as the game gives it.

        A call is a `(method, params)` pair or a `{"method", "params"}` dict. Each entry of `results` is `{"ok": true, "result"}` or
        `{"ok": false, "error": {code, message}}`; an error ends the batch (`stopped` is its index) unless `continue_on_error`. A refusal of the whole
        request (a bad shape) is a ControlError. A recording holds the batch as the one line it is.
        """
        entries: list[dict[str, Any]] = []
        for item in calls:
            method, params = (item.get("method"), item.get("params")) if isinstance(item, dict) else item
            entry: dict[str, Any] = {"method": method}
            if params:
                entry["params"] = params
            entries.append(entry)
        request: dict[str, Any] = {"calls": entries}
        if continue_on_error:
            request["continueOnError"] = True
        return self.call(BATCH_METHOD, request)

    def run_until(self, condition: dict[str, Any], max_frames: int = DEFAULT_RUN_UNTIL_FRAMES, dt: float | None = None) -> dict[str, Any]:
        """Runs frames until `condition` holds: `{met, frames, reason?, game?, version?, <clock>}`; reaching `max_frames` is `met: false`, not an error.

        `condition` comes from pointer_equals, version_moved or event_occurs. It is checked before the first frame and after every frame.
        """
        params: dict[str, Any] = {"condition": condition, "maxFrames": max_frames}
        if dt is not None:
            params["dt"] = dt
        return self.call(RUN_UNTIL_METHOD, params)

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

    @classmethod
    def _wait_timeout(cls, method: str, params: dict[str, Any] | None) -> float | None:
        """The timeout of a call that waits for frames, None for one that does not."""
        frames = cls._frames_waited(method, params)
        return None if frames is None else CALL_TIMEOUT_SECONDS + frames * STEP_SECONDS_PER_FRAME

    @classmethod
    def _frames_waited(cls, method: str, params: dict[str, Any] | None) -> int | None:
        """The most frames a call may wait for (a batch: the sum over its calls), None when it never waits."""
        if not isinstance(params, dict):
            params = {}
        if method == STEP_METHOD:
            return max(cls._count(params.get("frames"), 1), 0)
        if method == RUN_UNTIL_METHOD:
            return max(cls._count(params.get("maxFrames"), DEFAULT_RUN_UNTIL_FRAMES), 0)
        if method in (IMGUI_INPUT_METHOD, IMGUI_FIND_METHOD):
            return IMGUI_WAIT_FRAMES
        if method == BATCH_METHOD:
            calls = params.get("calls")
            if not isinstance(calls, list):
                return None
            waited = [cls._frames_waited(call.get("method"), call.get("params")) for call in calls if isinstance(call, dict)]
            frames = [count for count in waited if count is not None]
            return sum(frames) if frames else None
        return None

    @staticmethod
    def _window_flags(window: str | None, density: float | None, headless: bool) -> list[str]:
        """The start flags of a simulated window; both need a headless game, as the engine says at its start-up."""
        flags: list[str] = []
        if window is not None:
            if not WINDOW_SIZE_PATTERN.fullmatch(window):
                raise SessionError(f"window has to be 'WxH' in window units, e.g. '480x480', not '{window}'")
            flags.append(WINDOW_FLAG + window)
        if density is not None:
            flags.append(f"{DENSITY_FLAG}{density}")
        if flags and not headless:
            raise SessionError("window and density simulate a headless window; a real window's size and density belong to the system (leave headless on)")
        return flags

    @staticmethod
    def _safe_area_object(safe_area: dict[str, float] | Sequence[float]) -> dict[str, float]:
        if isinstance(safe_area, dict):
            return dict(safe_area)
        if len(safe_area) != len(SAFE_AREA_SIDES):
            raise SessionError(f"safe_area is a dict or {len(SAFE_AREA_SIDES)} numbers in the order {', '.join(SAFE_AREA_SIDES)}, not {len(safe_area)}")
        return dict(zip(SAFE_AREA_SIDES, safe_area))

    @staticmethod
    def _count(value: Any, default: int) -> int:
        return value if isinstance(value, int) and not isinstance(value, bool) else default

    @staticmethod
    def _check_record_target(path: Path, expect: str) -> None:
        if expect not in scenario.RECORD_MODES:
            raise SessionError(f"expect has to be one of {', '.join(scenario.RECORD_MODES)}, not '{expect}'")
        folder = path.expanduser().resolve().parent
        if not folder.is_dir():
            raise SessionError(f"cannot record to {path}: the folder {folder} does not exist")

    @staticmethod
    def _check_overlay_target(path: Path) -> Path:
        folder = path.expanduser().resolve()
        if folder.exists() and not folder.is_dir():
            raise SessionError(f"content_overlay {path} is not a folder")
        if png_ops.inside_git_work_tree(folder):
            raise SessionError(f"content_overlay {path} is inside a git work tree: the copy of the content must stay outside every repository")
        return folder

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
