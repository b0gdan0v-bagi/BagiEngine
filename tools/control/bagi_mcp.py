#!/usr/bin/env python3
"""MCP server (stdio) that lets a model start, play, look at and debug a BagiEngine game over the control API.

    pip install -r tools/control/requirements.txt
    claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py       # Windows: .venv\\Scripts\\python

The tools map onto the control API of the engine (src/Modules/RemoteControl/CLAUDE.md); the work is done by
bagi_session.py. The engine has no MCP dependency: this is a separate process that talks WebSocket to the game.
"""

import atexit
import functools
from typing import Any, Callable

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from bagi_control_client import ControlError
from bagi_session import GameSession, SessionError

INSTRUCTIONS = """\
Plays and tests a BagiEngine game (Match3, and any game that implements the control API) through its control API.

Start with launch_game (the path of a built game executable, or a preset and a game name: build/<preset>/bin/<game>); it runs the game headless,
with no window, on the manual clock. Then get_state, list_actions and perform_action. On the manual clock game time stands
still until step: an action changes the state at once (get_state shows it), but the scene, an animation and a screenshot
follow only after a step, so call step before screenshot when the picture should show the action.

Every action comes from list_actions: pass one of them to perform_action as it is. A refused action is an error with a
code (illegal_action, game_over, ...), the state stays as it was. Call quit when done.

To keep a bug as a test, call record_start right after launch_game (or pass record= to launch_game) and record_stop when done: the file is
a scenario that replays from a fresh start of the game, and its calls become the test.
"""

session = GameSession()
server = MCPServer("bagi", instructions=INSTRUCTIONS)


def reports_errors(tool: Callable[..., Any]) -> Callable[..., Any]:
    """Makes a refusal of the game or of the session reach the model as the text of the error ("illegal_action: ...").

    The server shows the model the message of a ToolError only; any other exception becomes "Error executing tool", which
    would hide the error code the model needs to correct itself.
    """

    @functools.wraps(tool)
    def wrapper(*args: Any, **kwargs: Any) -> Any:
        try:
            return tool(*args, **kwargs)
        except (ControlError, SessionError) as error:
            raise ToolError(str(error)) from error

    return wrapper


@server.tool()
@reports_errors
def launch_game(exe: str = "", headless: bool = True, manual_clock: bool = True, preset: str = "", game: str = "", record: str = "") -> dict[str, Any]:
    """Starts a game executable with the control API and connects to it (a game started earlier by this server is stopped).

    exe: path of the built game, relative to where this server runs (the repository root) or absolute.
    preset, game: instead of exe, the CMake preset that built it and the name of the game (build/<preset>/bin/<game>). Give exe or both of these.
    headless: no window, drawn in memory by the software renderer; screenshots work only like this. False opens a real window.
    manual_clock: game time moves only on step (deterministic). False runs it on the real clock.
    record: the path of a scenario file to record the session to, as record_start does (error codes and version steps).
    Answers the port, the clock status and the games the executable has.
    """
    return session.launch(exe, headless=headless, manual_clock=manual_clock, preset=preset, game=game, record=record or None)


@server.tool()
@reports_errors
def connect(port: int = 8765, host: str = "127.0.0.1", record: str = "") -> dict[str, Any]:
    """Connects to a game that is already running with --control (a person's window, for example).

    record: the path of a scenario file to record to; the game may be past its start, and the file replays from a fresh start (the answer says so).
    """
    return session.connect(host=host, port=port, record=record or None)


@server.tool()
@reports_errors
def status() -> dict[str, Any]:
    """The frame number, the game time and whether the manual clock is on."""
    return session.call("status")


@server.tool()
@reports_errors
def get_state() -> dict[str, Any]:
    """The state of the active game: {game, version, state}. `version` grows with every change of the state.

    Match3: state has the board, the score, the seed and the status. Every game documents its own state in its control adapter.
    """
    return session.call("get_state")


@server.tool()
@reports_errors
def list_actions() -> dict[str, Any]:
    """The actions the game takes right now: {game, version, actions}. Pass one of them to perform_action unchanged.

    Match3: {"type": "swap", "from": {"col", "row"}, "to": {"col", "row"}} for every swap that makes a match.
    Other games list their own action types the same way.
    """
    return session.call("list_actions")


@server.tool()
@reports_errors
def perform_action(action: dict[str, Any]) -> dict[str, Any]:
    """Plays one action from list_actions, e.g. a Match3 swap. Answers the outcome of the action.

    A refused action is an error and changes nothing: illegal_action (not legal now), game_over, invalid_params (malformed);
    a game may add its own codes.
    """
    return session.call("perform_action", action)


@server.tool()
@reports_errors
def set_state(state: dict[str, Any]) -> dict[str, Any]:
    """Loads a state into the game, in the form its own `get_state` gives; a game that cannot load one answers unsupported."""
    return session.call("set_state", state)


@server.tool()
@reports_errors
def reset(options: dict[str, Any] | None = None) -> dict[str, Any]:
    """Starts the game over. Match3 takes {"seed": N} for a repeatable board; options are the game's own."""
    return session.call("reset", options)


@server.tool()
@reports_errors
def step(frames: int = 1, dt: float | None = None) -> dict[str, Any]:
    """Runs frames (1 to 36000) of dt seconds (0 to 0.25, default 1/60) on the manual clock and answers the clock after them.

    The scene follows the state only through frames, so step after perform_action, reset or set_state before a screenshot.
    On the real clock it waits for that many frames instead and ignores dt.
    """
    return session.step(frames, dt)


@server.tool()
@reports_errors
def screenshot(max_size: int | None = None) -> Image:
    """A PNG of the game after one more frame (needs a game launched headless on the software renderer).

    max_size: the longer side is shrunk to at most this many pixels (16 to 4096, default 1024).
    """
    png, _size = session.screenshot(max_size)
    return Image(data=png, format="png")


@server.tool()
@reports_errors
def ui_tree() -> dict[str, Any]:
    """The in-game UI of the active scene: {scene, canvases}. Step a frame first, the rects are those of the last drawn frame.

    A canvas has node, space, visible, modal, inputEnabled, takesInput, laidOut, scale, bounds and its elements (a tree) and the `issues` that the
    layout validator found (TouchTargetTooSmall, OutsideCanvas, TextDoesNotFit...). An element has type, name, visible, interactive, rect (UI units of
    the canvas) and outputRect (the pixels of a screenshot), text for an element with a text component (a label or a button), and children. A button (an element with a clicker) also has action, disabled (its own or its
    group's: whether a click does anything), value (sent with the action; "" for a plain button) and selected (the segment of a choice group that the
    bound variable names). A choice group (type UISegmented) has bind (the variable), selectedValue and disabled; its segments are its children. A toggle
    (a toggle row, with its caption as text) has bind, onValue, offValue, checked, action and disabled; a progress bar (UIProgressBar) bind, minValue, maxValue and
    fraction (0..1); a slider bind, minValue, maxValue, step, value (a number), fraction (0..1), action and disabled. The selection, checked, value and
    fraction are those of the moment of the call, the rects those of the last drawn frame.
    Not shown: the documents of RmlUi screens.
    """
    return session.call("ui_tree")


@server.tool()
@reports_errors
def ui_click(name: str, canvas: str | None = None, at: float | None = None) -> dict[str, Any]:
    """Presses the left mouse button on the middle of the visible button, toggle or slider called `name`, like a user, and answers where it clicked.

    canvas: the scene node of the canvas, when several canvases have an element of that name.
    at: a number from 0 to 1, where along the element's width to press (default 0.5, the middle; anything else is invalid_params and nothing is pressed). This is
    how to set a slider: 0 asks for its minimum, 1 for its maximum, in between a value on its step grid; the answer's `value` is the text that came out.
    An element that cannot be pressed is an error:
    not_found (no such element), not_clickable (hidden, not a button, covered by another element, or its canvas takes no input; the message says
    which), no_scene. `consumed` in the answer says whether a canvas took the press; a click that nobody took reaches the game, as a user's would.
    `action` and `value` in the answer are what the element emitted (a toggle: the value it asks for; a slider: the value under the press). Clicking a
    segment of a choice group, a toggle or a slider changes nothing by itself: the game sets the variable, so `step` and read `ui_tree` again to see the new
    state. A progress bar cannot be clicked. The action runs at once but the picture follows only after a step.
    """
    params: dict[str, Any] = {"name": name}
    if canvas is not None:
        params["canvas"] = canvas
    if at is not None:
        params["at"] = at
    return session.call("ui_click", params)


@server.tool()
@reports_errors
def frame_stats(frames: int = 1) -> dict[str, Any]:
    """What the last frames cost, oldest first: {frames: [...], allocTracking}.

    frames: how many (1 to 240). Each has frame, frameMs (end to end), cpuMs, stagesMs (Update, SceneUpdate, Render, SceneDraw, Present;
    the scene stages are inside the other two), drawCalls, triangles, textureSwitches (the engine's draws; ImGui's are the counters
    "ImGui draw calls" / "ImGui triangles"), allocCount, allocBytes, liveBytes (0 unless the game was built with ENGINE_PROFILER_ALLOC,
    allocTracking says) and counters (named values set during the frame). Times are only meaningful in an optimized build (macos-profile).
    """
    return session.call("frame_stats", {"frames": frames})


@server.tool()
@reports_errors
def profiler_dump(path: str = "trace.json") -> dict[str, Any]:
    """Writes the Chrome trace recorded so far to path (relative to the game's working folder, no ".."), for ui.perfetto.dev.

    Needs a game built with ENGINE_PROFILER (preset macos-profile) and started with --trace=<file> or --profiler=chrome
    (launch_game does not pass them); otherwise the error not_recording.
    """
    return session.call("profiler_dump", {"path": path})


@server.tool()
@reports_errors
def zone_allocs(top: int = 20, reset: bool = False) -> dict[str, Any]:
    """The heap allocations per profiler zone, the most bytes first, and the totals of the process: {enabled, zones, totals}.

    A zone counts its own allocations and those of the zones inside it. enabled is false (and everything zero) unless the game was built
    with ENGINE_PROFILER_ALLOC. top: how many zones (1 to 1000). reset: zero the zone statistics after answering, to measure the next frames.
    """
    return session.call("zone_allocs", {"top": top, "reset": reset})


@server.tool()
@reports_errors
def game_log(lines: int = 40) -> str:
    """The last lines of the output of the game that launch_game started: use it when the game fails or an action is refused for no visible reason."""
    return session.game_log(lines)


@server.tool()
@reports_errors
def record_start(path: str, expect: str = "minimal") -> dict[str, Any]:
    """Starts recording the calls of this session into a scenario file (JSON lines), written by record_stop or quit.

    Call it right after launch_game: the file replays from a fresh start of the game, so calls made before it are not in it (the answer says
    when there were any). path: where to write it (the folder must exist). expect: "minimal" writes the error code of a refused call and the
    step of the state version, which is what the game's API promises; "full" adds every answer (a screenshot as its size), which breaks on any
    change of the game's data. python3 tools/control/bagi_play.py <exe> <file> plays the file back as a test.
    """
    return session.start_recording(path, expect)


@server.tool()
@reports_errors
def record_stop() -> dict[str, Any]:
    """Ends the recording and writes the scenario file: answers its path and the number of calls in it."""
    return session.stop_recording()


@server.tool(name="quit")
@reports_errors
def quit_game() -> dict[str, Any]:
    """Quits the game and waits for it to exit. Answers the exit code of a game that launch_game started."""
    return session.quit()


def main() -> None:
    atexit.register(session.close)  # a launched game must not outlive the server
    server.run()


if __name__ == "__main__":
    main()
