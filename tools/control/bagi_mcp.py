#!/usr/bin/env python3
"""MCP server (stdio) that lets a model start, play, look at and debug a BagiEngine game over the control API.

    pip install -r tools/control/requirements.txt
    claude mcp add bagi -- .venv/bin/python tools/control/bagi_mcp.py       # Windows: .venv\\Scripts\\python

The tools map onto the control API of the engine (src/Modules/RemoteControl/CLAUDE.md); the work is done by
bagi_session.py. The engine has no MCP dependency: this is a separate process that talks WebSocket to the game.
"""

import atexit
import functools
import json
from typing import Any, Callable

from mcp.server.mcpserver import Image, MCPServer
from mcp.server.mcpserver.exceptions import ToolError

from bagi_control_client import ControlError
from bagi_image import ImageError, run_image_diff
from bagi_session import GameSession, SessionError, event_occurs, pointer_equals
from bagi_session import version_moved as version_moved_condition

INSTRUCTIONS = """\
Plays and tests a BagiEngine game (any game that implements the control API, e.g. the engine's BagiStress) through its control API.

Start with launch_game (the path of a built game executable, or a preset and a game name: build/<preset>/bin/<game>); it runs the game headless,
with no window, on the manual clock. Then get_state, list_actions and perform_action. On the manual clock game time stands
still until step: an action changes the state at once (get_state shows it), but the scene, an animation and a screenshot
follow only after a step, so call step before screenshot when the picture should show the action.

Every action comes from list_actions: pass one of them to perform_action as it is. A refused action is an error with a
code (illegal_action, game_over, ...), the state stays as it was. Call quit when done.

A run of many actions is one batch call, not many calls; wait for something with run_until instead of polling get_state with step. To see whether a run logged
new warnings or errors, call log_mark before it and log_since with that mark after it.

To compare two pictures (a before and an after, a render and its mockup, two backends) call image_diff: a and b are PNG files or "screen" (the connected game's
picture now); it answers the numbers and a map of the difference, with no game needed for two files.

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
        except (ControlError, SessionError, ImageError) as error:
            raise ToolError(str(error)) from error

    return wrapper


@server.tool()
@reports_errors
def launch_game(
    exe: str = "",
    headless: bool = True,
    manual_clock: bool = True,
    preset: str = "",
    game: str = "",
    record: str = "",
    window: str = "",
    density: float | None = None,
    editor: bool = False,
) -> dict[str, Any]:
    """Starts a game executable with the control API and connects to it (a game started earlier by this server is stopped).

    exe: path of the built game, relative to where this server runs (the repository root) or absolute.
    preset, game: instead of exe, the CMake preset that built it and the name of the game (build/<preset>/bin/<game>). Give exe or both of these.
    headless: no window, drawn in memory by the software renderer; screenshots work only like this. False opens a real window.
    manual_clock: game time moves only on step (deterministic). False runs it on the real clock.
    record: the path of a scenario file to record the session to, as record_start does (error codes and version steps).
    window: "WxH" in window units (64 to 4096 each), the start size of the headless window instead of the one in the game's config, e.g. "390x844".
    density: the start pixel density (0.5 to 4, default 1): the pictures are round(size * density) pixels. Both need headless; set_window changes them later.
    editor: start in the editor run mode (--editor), headless too: a screenshot is the whole editor window; status.runMode says which mode runs (Game in a build without the editor).
    Answers the port, the clock status and the games the executable has.
    """
    return session.launch(exe, headless=headless, manual_clock=manual_clock, preset=preset, game=game, record=record or None, window=window or None, density=density, editor=editor)


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
    """The frame number, the game time, whether the manual clock is on and `log`: how many warnings and errors the game has logged since its start."""
    return session.call("status")


@server.tool()
@reports_errors
def get_state() -> dict[str, Any]:
    """The state of the active game: {game, version, state}. `version` grows with every change of the state.

    The shape of `state` is the game's own: every game documents it in its control adapter (BagiStress: scene, count, language).
    """
    return session.call("get_state")


@server.tool()
@reports_errors
def list_actions() -> dict[str, Any]:
    """The actions the game takes right now: {game, version, actions}. Pass one of them to perform_action unchanged.

    Each game lists its own action types (BagiStress: {"type": "set_count", "count": N}, {"type": "set_scene", "scene": "ui"}).
    """
    return session.call("list_actions")


@server.tool()
@reports_errors
def perform_action(action: dict[str, Any]) -> dict[str, Any]:
    """Plays one action from list_actions, e.g. BagiStress's {"type": "set_count", "count": 1000}. Answers the outcome of the action.

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
    """Starts the game over. A game may take options (a seed for a repeatable start, BagiStress: {"scene", "count"}); they are the game's own."""
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
def set_window(
    width: int,
    height: int,
    density: float | None = None,
    user_scale: float | None = None,
    safe_area: dict[str, float] | list[float] | None = None,
) -> dict[str, Any]:
    """Changes the window of a game launched headless (a phone, a Retina display, a notch) and answers its `window` object after one frame.

    width, height: window units, 64 to 4096 integers. density: the pixel density, 0.5 to 4; the picture and `ui_tree` `outputRect` are round(size * density)
    pixels, at most 8192 a side. user_scale: the UI scale of the player, 0.5 to 3 (out of range is an error, never clamped). safe_area: {top, right, bottom, left} in
    window units, or a list of four numbers in that order; all four are required, [0, 0, 0, 0] clears it; the insets are not negative and leave the window a
    non-empty area. A value left out keeps its current value. Everything is checked first: an error changes nothing.
    ui_tree and screenshot right after the answer are of the new size (the frame has laid the scene out). A real window (launch_game headless=False, connect)
    answers not_headless; a value outside the limits is invalid_params and the message names it. The answer, like `status`, has width, height, density,
    displayScale, safeArea, outputWidth, outputHeight, userScale and headless.
    """
    return session.set_window(width, height, density, user_scale, safe_area)


@server.tool()
@reports_errors
def log_mark() -> dict[str, Any]:
    """A mark of the game's log now: {mark, warnings, errors} (the totals since the start).

    Call it before a run and pass `mark` to log_since afterwards: that answers exactly what the run logged. Use it to check that nothing went wrong.
    """
    return session.log_mark()


@server.tool()
@reports_errors
def log_since(mark: int, level: str = "warning") -> dict[str, Any]:
    """The warnings and errors the game logged since a mark of log_mark (0 is the start of the game): {lines, warnings, errors, dropped, next, truncated?}.

    lines: {sequence, level, time, message, category?}, oldest first, at most the newest 200 (`truncated`). level: "warning" (warnings and errors, the default) or
    "error" (errors only); `warnings` and `errors` count every kept line since the mark whatever the level. `dropped` lines are lost already (a very noisy
    game), so the counts are then lower bounds. `next` is the mark to use for the next check. A clean run is `errors: 0, dropped: 0`.
    """
    return session.log_since(mark, level)


@server.tool()
@reports_errors
def log_subscribe(enabled: bool, level: str = "warning") -> dict[str, Any]:
    """Turns on or off the push of the event `log` (one per new warning or error line, at most 16 per frame) for the game; it is off by default.

    level: "warning" (default) or "error". The pushes come in with later calls; they are for a client that reads events, a model usually asks log_since instead.
    """
    return session.log_subscribe(enabled, level)


@server.tool()
@reports_errors
def batch(calls: list[dict[str, Any]], continue_on_error: bool = False) -> dict[str, Any]:
    """Runs many calls in order in one request: use it for a run of actions, steps and reads instead of one call each.

    calls: 1 to 1000 objects {"method": "perform_action", "params": {...}} (params may be left out; any method of list_methods works, except `batch` and
    `quit`, which that call's entry refuses). Answers {results, stopped?}: one entry per call run, {"ok": true, "result": ...} or {"ok": false, "error": {code,
    message}}. An error ends the batch (`stopped` is the index of that call; the calls after it did not run) unless continue_on_error. A step inside
    waits for its frames and the calls behind it run after them. The answer holds every answer, so a batch of screenshots is large.
    """
    return session.batch(calls, continue_on_error)


@server.tool()
@reports_errors
def run_until(
    pointer: str | None = None,
    equals: Any = None,
    version_moved: bool = False,
    event: str | None = None,
    event_equal: dict[str, Any] | None = None,
    max_frames: int = 600,
    dt: float | None = None,
) -> dict[str, Any]:
    """Runs frames until a condition holds, instead of polling get_state with step: {met, frames, reason?, game?, version?, frame, totalTime, ...}.

    Give exactly one condition. pointer (with equals): the JSON pointer (RFC 6901, "/state/score") into what get_state answers ({game, version, state}) equals
    `equals` (JSON equality; `equals` left out means null); a missing path is not met. version_moved: the state version differs from the one at the start of
    the call. event: the name of a push that arrives after the call began ("state_changed"), event_equal the members its data has to carry.
    The condition is checked before the first frame (`frames: 0`) and after every frame. max_frames (1 to 36000, default 600) and dt (seconds per frame on the
    manual clock, default 1/60) are as in step. Reaching the limit is not an error: `met: false`, `frames` = max_frames; look at `met`.
    """
    chosen = [name for name, given in (("pointer", pointer is not None), ("version_moved", version_moved), ("event", event is not None)) if given]
    if len(chosen) != 1:
        raise SessionError(f"give exactly one condition of pointer (with equals), version_moved and event, not {', '.join(chosen) if chosen else 'none'}")
    if event_equal is not None and event is None:
        raise SessionError("event_equal belongs to an event: give event too")
    if pointer is not None:
        condition = pointer_equals(pointer, equals)
    elif event is not None:
        condition = event_occurs(event, event_equal)
    else:
        condition = version_moved_condition()
    return session.run_until(condition, max_frames, dt)


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
def image_diff(a: str, b: str, out: str = "", threshold: int = 0, region: list[int] | None = None) -> list[str | Image]:
    """Compares picture b with picture a (the reference) and answers the numbers as JSON and a map of the difference as an image: a is dimmed to grey, red is changed.

    a, b: a PNG file (a path, `~` allowed, relative to where this server runs) or the word `screen`, the picture of the connected game now at full size
    (step first so it shows the last action). Two files need no game. Use it for a before and an after of a change, a render against its mockup (the
    mockup as b), or the same scene on two backends.
    threshold: a pixel is changed when any channel differs by more than this (0 to 255, default 0: any difference). Use a few units to ignore anti-aliasing.
    region: [x0, y0, x1, y1] in pixels of a, corners inclusive, to compare only that box; a box found in an earlier answer (`boundingBox`, `regions`) can be given back as it is.
    out: a file to write the full-size map to (the folder must exist, outside every git repository); the answer then has `out`. The image in the answer is the map
    shrunk to at most 1024 pixels.
    A b of another size is scaled to the size of a (`scaled`; `aspectDiffers` when its shape differs, so it was stretched); a 3 channel picture beside a 4 channel one is compared on RGB.
    Answers `equal`, `changed` and `compared` pixels, `share` (0 to 1), `boundingBox` of all changes (null when none), the biggest `regions` ({box, changed, shareOfPicture}),
    and what was done to the pictures. An unreadable file or a bad region is an error that says so.
    """
    stats, preview = run_image_diff(session, a, b, out, threshold, region)
    return [json.dumps(stats), Image(data=preview, format="png")]


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
    fraction (0..1); a slider bind, minValue, maxValue, step, sliderValue (a number; a slider has no value), fraction (0..1), action and disabled. The selection, checked,
    sliderValue and fraction are those of the moment of the call, the rects those of the last drawn frame. A canvas has `regions` too: [{name, rect}] sorted by
    name. An element deeper than 100 levels has `truncated` and no children.
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
    which), no_scene. `consumed` in the answer says whether a handler took the press and `takenBy` which one: "element" (the element's own canvas),
    "target" (something drawn above it: a scene target such as a ClickableComponent, or another canvas; `action` and `value` are then empty because the element
    emitted nothing, the target did) or "none" (the game got the click, as a user's would).
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
def click_at(
    x: float, y: float, space: str = "output", button: str = "left", which: int | None = None
) -> dict[str, Any]:
    """Moves the pointer to a point, presses the button and releases it, like a user, with no element name: the way to click a scene object (a tile, a board cell)
    that `ui_click` cannot name. Answers {x, y, outputX, outputY, consumed}.

    x, y: the point. space "output" (default): the pixels of a `screenshot`; "scene": scene units of the active scene (mapped by the fit of its last drawn frame,
    so `step` first; no_scene when there is none). button: "left" (default), "right" or "middle". which: the pointer id (default 0, the mouse); another id is
    another pointer, for a game that tells fingers apart.
    `consumed` says whether a scene target (ClickableComponent, a game's pointer target) or a canvas took the press, which a click on empty space does
    not; the game's own raw mouse handlers see an unconsumed click. A number that is not finite or is beyond 1e6, an unknown space or button, or a bad
    `which` is invalid_params and nothing is pressed. The action runs at once but the picture follows only after a step.
    """
    params: dict[str, Any] = {"x": x, "y": y}
    if space != "output":
        params["space"] = space
    if button != "left":
        params["button"] = button
    if which is not None:
        params["which"] = which
    return session.call("click_at", params)


@server.tool()
@reports_errors
def imgui_find(
    label: str | None = None,
    label_contains: str | None = None,
    id: str | None = None,
    window: str | None = None,
    kind: str | None = None,
    visible_only: bool = False,
    limit: int | None = None,
) -> dict[str, Any]:
    """Finds the windows and items of the editor's ImGui by label or id: {frame, display, wantCaptureMouse, wantCaptureKeyboard, wantTextInput, hoveredWindow?,
    focusedWindow?, matches: [{kind, id, label, window?, x, y, width, height, visible, hovered}], truncated?}. The rects are output pixels, the pixels of `screenshot`.

    Needs the editor: launch_game(editor=True), else the error no_imgui. With no filter only the windows are listed. label: the whole label or its part before `##`;
    label_contains: a part of it; id: ImGui's hex id (0x1A2B3C4D); window: only what belongs to that window; kind: "window" or "item"; visible_only: leave out what is
    clipped or scrolled out. An item exists only in a frame that drew it: a menu item is there only while its menu is open (click the menu, then find the item).
    """
    return session.imgui_find(label, label_contains=label_contains, id=id, window=window, kind=kind, visible_only=visible_only, limit=limit)


@server.tool()
@reports_errors
def imgui_click(
    label: str | None = None,
    x: float | None = None,
    y: float | None = None,
    window: str | None = None,
    button: str = "left",
    count: int = 1,
    mods: list[str] | None = None,
) -> dict[str, Any]:
    """Clicks an item of the editor's ImGui like a user: the centre of the one visible item called `label` (in `window`, to tell same names apart), or the output
    pixel x, y. count 2 is a double click, button "left", "right" or "middle", mods "ctrl" (Cmd on macOS), "shift", "alt", "super".

    Needs the editor (launch_game(editor=True), else no_imgui). Answers {events, frames, wantCaptureMouse, wantCaptureKeyboard, wantTextInput}: a click ImGui did
    not want (wantCaptureMouse false) went on to the game. No visible match or several is an error that lists them. A menu item exists only while its menu is open:
    click the menu, then the item. The call runs its own frames, no step is needed. A recording keeps it, and it replays in the editor only.
    """
    return session.imgui_click(label, x=x, y=y, window=window, button=button, count=count, mods=mods or ())


@server.tool()
@reports_errors
def imgui_type(text: str) -> dict[str, Any]:
    """Types text into the focused text field of the editor's ImGui (click the field first; wantTextInput in the answer says whether a field had the focus).

    Needs the editor (launch_game(editor=True), else no_imgui). A recording keeps it, and it replays in the editor only.
    """
    return session.imgui_type(text)


@server.tool()
@reports_errors
def imgui_key(key: str, mods: list[str] | None = None) -> dict[str, Any]:
    """Presses and releases a key in the editor's ImGui: key is ImGui's name (Enter, Escape, A, F6, LeftArrow, Backspace...), mods "ctrl" (Cmd on macOS), "shift",
    "alt", "super": imgui_key("P", ["ctrl", "shift"]) is the chord Ctrl+Shift+P. An unknown key is invalid_params.

    Needs the editor (launch_game(editor=True), else no_imgui). A recording keeps it, and it replays in the editor only.
    """
    return session.imgui_key(key, mods or ())


@server.tool()
@reports_errors
def frame_stats(frames: int = 1) -> dict[str, Any]:
    """What the last frames cost, oldest first: {frames: [...], allocTracking, arenas, pools}.

    frames: how many (1 to 240). Each has frame, frameMs (end to end), cpuMs, stagesMs (Update, SceneUpdate, Render, SceneDraw, Present, Wait;
    the scene stages are inside the other two, Wait is the display wait the Vulkan backend times, inside cpuMs), drawCalls, triangles, textureSwitches (the engine's draws; ImGui's are the counters
    "ImGui draw calls" / "ImGui triangles"), allocCount, allocBytes, liveBytes (0 unless the game was built with ENGINE_PROFILER_ALLOC,
    allocTracking says), arenaUsedBytes, arenaPeakBytes, arenaOverflows (the memory arenas, summed), poolLiveBlocks, poolPageBytes (the block
    pools, summed; a pool shared by threads lags by a few calls) and counters (named values set during the frame). arenas and pools list each arena (name, capacity, used, peak, overflows, chunks) and each block
    pool (name, blockSize, live, peak, pages, allocations). Times are only meaningful in an optimized build (macos-profile).
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
    """The heap allocations per profiler zone, the most bytes first, and the totals of the process: {enabled, zones, names, sizes, totals}.

    A zone counts its own allocations and those of the zones inside it. names: [{zone, name, allocs, bytes}], the allocations of each
    innermost zone by EASTL's allocation name ("EASTL vector", or the literal given to set_name; "" for a plain operator new; zone
    "(no zone)" outside every zone), so it says which container allocates. enabled is false (and everything zero, names empty) unless the
    game was built with ENGINE_PROFILER_ALLOC. sizes: [{zone, allocs, buckets: [{upTo, allocs}]}], the size histogram of each innermost zone in
    power-of-two buckets (only the non-empty ones; upTo is the largest size of the bucket in bytes, 0 for the open last bucket), which says
    whether a zone allocates many small blocks or a few large ones. top: how many zones, names and size histograms (1 to 1000). reset: zero
    the zone, name and size statistics after answering, to measure the next frames.
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
