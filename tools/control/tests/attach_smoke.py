#!/usr/bin/env python3
"""Smoke test of the editor's Attach over two processes: a headless game with --control, and a headless editor of BagiStress started with --attach.

    python tools/control/tests/attach_smoke.py --exe build/macos-debug/bin/BagiStress
    BAGI_WEB_DIST=<dist> python tools/control/tests/attach_smoke.py --exe build/macos-debug/bin/BagiStress --game-exe tools/web/web_game.py

In the editor's UI (imgui_find / imgui_input) the game's picture in the Viewport is clicked where the game's scene_pick finds a node, the layer of the node
that this selects is stepped up, and Save is pressed. The game's layer is raised before the editor starts, so the step shows that the editor read the game's
value and wrote to the game: the game's scene_tree has the next layer and the editor's own scene is as it was, with no unsaved change. The save lands in the
game's content overlay and nothing in the editor's; a game in a browser page (--game-exe with tools/web/web_game.py, which takes no scene and no overlay
flag) says that it saved into its memory, which the editor's status line passes on. Then the game quits: the hierarchy takes no input any more but still
shows what was read, and the editor stays alive. Neither log may hold a warning or an error.

The editor keeps its dock layout in a file of the checkout that no flag redirects: the test takes it away for the run, so that the editor starts on the
default layout, and puts it back. Exit code: 0 passed, 1 failed, 77 skipped (no executable, a build without the editor, or a browser game that
cannot start).
"""

import argparse
import contextlib
import os
import re
import sys
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any, Callable, NamedTuple

ENGINE_DIR = Path(os.environ["BAGI_ENGINE_DIR"]) if "BAGI_ENGINE_DIR" in os.environ else Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "qa"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
import bagi_qa_editor as editor  # noqa: E402
from bagi_session import GameSession, SessionError  # noqa: E402
from game_process import DEFAULT_STARTUP_TIMEOUT, PORT_TAKEN_MARKER, GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from smoke import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, SmokeFailure, check  # noqa: E402

SCENE_FLAGS = ["--stress-scene=ui", "--stress-count=6"]  # a canvas of labels to click on, the same in the editor and a desktop game
WEB_GAME = "web_game.py"
EDITOR_WINDOW = "--window=1280x720"  # the size the splitter of `widen_left_dock` is measured on
LAYER_RAISE = 7  # added to the picked node's layer in the game: unlike the editor's own, and still on top at the picked point
PICK_STEPS = 20  # the points tried for a hit: a grid of this many parts a side
PICK_MARGIN = 0.005  # of the screen: the node is hit this far around the point too, so that rounding a click to the picture keeps it
VIEWPORT = "Viewport"
PICTURE_ITEM = "##RemoteImage"
SCENE_FILE = Path("config") / "SceneConfig.xml"  # in a content overlay
LAYER_STEP_BUTTON = "+"  # of the inspector's layer stepper, the one such button while every component section is closed
LAYOUT_FILE = ENGINE_DIR / "tools" / "stress" / "config" / "imgui.ini"
WAIT_SECONDS = 30.0
WEB_STARTUP_TIMEOUT = 120.0  # a browser starts, loads the page and compiles the game
RETRY_SECONDS = 0.1
SAVE_SETTLE_SECONDS = 3.0  # for the answer of a save to reach the editor: nothing the control API tells shows that it has
SHUTDOWN_TIMEOUT = 15.0
EDITOR_FRAMES = 5  # ImGui lays its docks out over more than one frame
ATTACHED_LINE = "Remote editor: attached to"
LOST_LINE = "Remote editor: no connection to"
SAVED_LINE = "Remote editor: The program saved the scene to "
NOT_PERSISTENT_TEXT = "kept only in the page's memory, lost on reload"


class Skipped(Exception):
    pass


class Overlays(NamedTuple):
    editor: Path
    game: Path


def wait_until(attempt: Callable[[], bool], what: str) -> None:
    deadline = time.monotonic() + WAIT_SECONDS
    while not attempt():
        check(time.monotonic() < deadline, f"{what}: not in {WAIT_SECONDS} s")
        time.sleep(RETRY_SECONDS)


def layers(program: BagiControlClient | GameSession) -> dict[str, int]:
    """The layer of each node of the program's own scene by its path (`scene_tree`)."""
    return {node["path"]: node["layer"] for node in program.call("scene_tree")["nodes"]}


def pickable_point(game: BagiControlClient) -> tuple[float, float, str]:
    """A point of the game's screen, as fractions, and the path of the node a click there picks."""
    game.call("step", {"frames": 1})  # nothing is picked in a scene that never drew
    for row in range(1, PICK_STEPS):
        for column in range(1, PICK_STEPS):
            x, y = column / PICK_STEPS, row / PICK_STEPS
            answer = game.call("scene_pick", {"x": x, "y": y})
            around = ((x + dx, y + dy) for dx in (-PICK_MARGIN, PICK_MARGIN) for dy in (-PICK_MARGIN, PICK_MARGIN))
            if answer["hit"] and all(game.call("scene_pick", {"x": ax, "y": ay}) == answer for ax, ay in around):
                return x, y, answer["node"]
    raise SmokeFailure(f"no point of a grid of {PICK_STEPS} a side picks a node of the game")


def picture_of(session: GameSession) -> dict[str, Any] | None:
    """The game's picture in the editor's Viewport; None until a frame has arrived."""
    items = [item for item in editor.find_items(session, VIEWPORT, label=PICTURE_ITEM) if item["visible"]]
    return items[0] if items else None


def layer_buttons(session: GameSession) -> list[dict[str, Any]]:
    return [item for item in editor.find_items(session, editor.CHILD_WINDOWS, editor.INSPECTOR_CHILD, label=LAYER_STEP_BUTTON) if item["visible"]]


def click_picture(session: GameSession, x: float, y: float) -> None:
    """Clicks the point (`x`, `y`, fractions) of the game's screen in the editor's picture of it."""
    wait_until(lambda: picture_of(session) is not None, "the game's picture in the editor's Viewport")
    picture = picture_of(session)
    point = {"x": picture["x"] + x * picture["width"], "y": picture["y"] + y * picture["height"]}
    session.imgui_input([{"type": "move", **point}])
    session.imgui_click(**point)


def saved_layer(overlay: Path, scene: str, name: str) -> int | None:
    """The layer the overlay's SceneConfig.xml gives the node called `name` in `scene`; None while there is no such file or node.

    Read as text, so that a Python whose XML parser does not load can run the test."""
    try:
        text = (overlay / SCENE_FILE).read_text(encoding="utf-8")
    except OSError:
        return None
    block = re.search(rf'<scene\b[^>]*\bname="{re.escape(scene)}"[^>]*>.*?</scene>', text, flags=re.DOTALL)
    node = None if block is None else re.search(rf'<node\b[^>]*\bname="{re.escape(name)}"[^>]*>', block.group())
    if node is None:
        return None
    layer = re.search(r'\blayer="(-?\d+)"', node.group())
    return 0 if layer is None else int(layer.group(1))


def takes_input(session: GameSession) -> bool:
    """Whether a click on the hierarchy's search field gives it the keyboard; the field is left again."""
    field = editor.find_item(session, editor.CHILD_WINDOWS, editor.HIERARCHY_CHILD, label_contains=editor.SEARCH_FIELD)
    taken = editor.click_item(session, field)["wantTextInput"]
    if taken:
        session.imgui_key("Escape")
    return taken


def settle(session: GameSession, seconds: float) -> None:
    """Runs frames of the editor for a while, so that it reads what the game answers."""
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        session.step(1)
        time.sleep(RETRY_SECONDS)


def drive(game: GameProcess, game_client: BagiControlClient, session: GameSession, point: tuple[float, float, str], overlays: Overlays | None) -> None:
    """`overlays` is None for a browser game, which has no overlay to look into."""
    x, y, path = point
    session.step(EDITOR_FRAMES)
    editor.widen_left_dock(session)
    editor.open_panel(session, editor.SCENE_EDITOR)
    own_layers = layers(session)
    layer = layers(game_client)[path]
    check(own_layers.get(path) != layer, f"the editor's own scene already has '{path}' on layer {layer}")

    check(not layer_buttons(session), "the inspector shows a node before any click")
    click_picture(session, x, y)
    wait_until(lambda: bool(layer_buttons(session)), f"the inspector of '{path}' after a click on the game's picture")
    buttons = layer_buttons(session)
    check(len(buttons) == 1, f"{len(buttons)} layer step buttons in the inspector: {editor.inspector_labels(session)}")
    editor.click_item(session, buttons[0])
    wait_until(lambda: layers(game_client)[path] == layer + 1, f"the game's '{path}' on layer {layer + 1}")
    check(layers(session) == own_layers, f"the editor's own scene moved: {layers(session)}")
    check(not editor.dirty(session), "the editor's own scene has unsaved changes")
    print(f"attach: a click on the picture selected '{path}', and a layer step in the editor put it on layer {layer + 1} in the game", flush=True)

    scene = game_client.call("scene_tree")["scene"]
    editor.click_item(session, editor.save_button(session))
    if overlays is not None:

        def saved() -> bool:
            session.step(1)  # the editor sends the save in a frame of its own
            return saved_layer(overlays.game, scene, path.rsplit("/", 1)[-1]) == layer + 1

        wait_until(saved, f"layer {layer + 1} of '{path}' in the scene file of the game's overlay")
    settle(session, SAVE_SETTLE_SECONDS)
    check(not editor.dirty(session), "the editor's own scene has unsaved changes after the save")

    check(takes_input(session), "the hierarchy of an attached editor takes no input")
    game_client.call("quit")
    check(game.stop(SHUTDOWN_TIMEOUT) == 0, f"the game exited with code {game.exit_code} after quit")
    wait_until(lambda: not takes_input(session), "a read-only hierarchy after the game quit")
    name = path.rsplit("/", 1)[-1]
    check(name in editor.hierarchy_names(session), f"the rows that were read left the screen: {editor.hierarchy_names(session)}")
    check(bool(editor.find_items(session, editor.CHILD_WINDOWS, editor.INSPECTOR_CHILD, label_contains=editor.NAME_FIELD)), "the inspected node left the screen")
    check(session.call("status")["runMode"] == "Editor", "the editor does not answer after the game quit")
    print("attach: the game quit, the editor shows what was read, read-only, and runs on", flush=True)


def check_log(program: GameProcess, name: str, lines: tuple[str, ...] = ()) -> str:
    """For a program that has exited: its output is complete only then. Gives the log."""
    problems = program.log_problems()
    check(not problems, f"the {name}'s log has problems:\n" + "\n".join(problems))
    text = program.log_path.read_text(errors="replace")
    for line in lines:
        check(line in text, f"the {name}'s log has no line '{line}'\n--- log (tail) ---\n{program.log_tail()}")
    return text


def check_saved_line(editor_log: str, overlays: Overlays | None) -> None:
    """The status line the editor showed for the save, and what neither overlay may hold."""
    line = next(line for line in editor_log.splitlines() if SAVED_LINE in line)
    if overlays is None:
        check(NOT_PERSISTENT_TEXT in line, f"the editor does not say that a page keeps the save in memory only: {line}")
    else:
        check(NOT_PERSISTENT_TEXT not in line and overlays.game.name in line, f"the editor does not name the file in the game's overlay: {line}")
    print(f"attach: {line[line.index(SAVED_LINE):]}", flush=True)


def play(exe: Path, game_exe: Path, folders: Overlays) -> None:
    web = game_exe.name == WEB_GAME
    overlays = None if web else folders
    game_port = free_port()
    game_flags = [] if web else [*SCENE_FLAGS, f"--content-overlay={folders.game}"]
    game = GameProcess(build_game_command(game_exe, game_port, extra=game_flags), cwd=game_exe.parent)
    editor_port = free_port()
    editor_flags = ["--editor", EDITOR_WINDOW, f"--attach=ws://127.0.0.1:{game_port}", f"--content-overlay={folders.editor}", *SCENE_FLAGS]
    editor_process = GameProcess(build_game_command(exe, editor_port, extra=editor_flags), cwd=exe.parent)
    session = GameSession()
    try:
        game.start()
        try:
            game_client = game.connect(game_port, WEB_STARTUP_TIMEOUT if web else DEFAULT_STARTUP_TIMEOUT)
        except GameLaunchError:
            if web and game.exit_code == EXIT_SKIPPED:
                raise Skipped(f"the browser game cannot start\n{game.log_tail()}")
            raise
        x, y, path = pickable_point(game_client)
        game_client.call("scene_set_node", {"node": path, "layer": layers(game_client)[path] + LAYER_RAISE})
        check(game_client.call("scene_pick", {"x": x, "y": y})["node"] == path, f"raising '{path}' moved it from the point it was picked at")
        editor_process.start()
        first = editor_process.connect(editor_port)
        run_mode = first.call("status")["runMode"]
        if run_mode == "Game":
            raise Skipped("status.runMode is Game (the build has no editor)")
        check(run_mode == "Editor", f"status.runMode is '{run_mode}' with --editor")
        first.close()
        session.connect(port=editor_port)

        drive(game, game_client, session, (x, y, path), overlays)

        session.call("quit")
        check(editor_process.stop(SHUTDOWN_TIMEOUT) == 0, f"the editor exited with code {editor_process.exit_code} after quit")
        check_log(game, "game")
        check_saved_line(check_log(editor_process, "editor", (ATTACHED_LINE, SAVED_LINE, LOST_LINE)), overlays)
        written = sorted(str(file) for file in folders.editor.rglob(SCENE_FILE.name))
        check(not written, f"the editor wrote a scene file of its own: {written}")
    except (SmokeFailure, SessionError, ControlError, ConnectionClosed, GameLaunchError, OSError, KeyError) as error:
        raise SmokeFailure(f"{error!r}\n--- game log (tail) ---\n{game.log_tail()}\n--- editor log (tail) ---\n{editor_process.log_tail()}") from error
    finally:
        session.close()
        editor_process.stop(0)
        game.stop(0)


@contextlib.contextmanager
def default_layout() -> Iterator[None]:
    saved = LAYOUT_FILE.read_bytes() if LAYOUT_FILE.is_file() else None
    LAYOUT_FILE.unlink(missing_ok=True)
    try:
        yield
    finally:
        if saved is None:
            LAYOUT_FILE.unlink(missing_ok=True)
        else:
            LAYOUT_FILE.write_bytes(saved)


@contextlib.contextmanager
def temporary_folders() -> Iterator[Overlays]:
    """$BAGI_EDITOR_DIR (preferences, layouts) for the editor started inside, and the folders the two programs' writes of content go into."""
    previous = os.environ.get("BAGI_EDITOR_DIR")
    with tempfile.TemporaryDirectory() as user, tempfile.TemporaryDirectory() as editor_overlay, tempfile.TemporaryDirectory() as game_overlay:
        os.environ["BAGI_EDITOR_DIR"] = user
        try:
            yield Overlays(Path(editor_overlay), Path(game_overlay))
        finally:
            if previous is None:
                del os.environ["BAGI_EDITOR_DIR"]
            else:
                os.environ["BAGI_EDITOR_DIR"] = previous


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True, help="the built BagiStress, which runs the editor")
    parser.add_argument("--game-exe", type=Path, help=f"the game to attach to (default: --exe); tools/web/{WEB_GAME} for a game in a browser page")
    arguments = parser.parse_args(argv)
    exe = arguments.exe.resolve()
    game_exe = arguments.game_exe.resolve() if arguments.game_exe else exe
    for path in (exe, game_exe):
        if not path.is_file():
            print(f"attach: SKIPPED, there is no executable at {path}")
            return EXIT_SKIPPED
    try:
        with default_layout(), temporary_folders() as overlays:
            try:
                play(exe, game_exe, overlays)
            except SmokeFailure as error:
                # free_port gives the port back before the program binds it
                if PORT_TAKEN_MARKER not in str(error):
                    raise
                print("attach: a port was taken by another process; retrying once", flush=True)
                play(exe, game_exe, overlays)
    except Skipped as reason:
        print(f"attach: SKIPPED, {reason}")
        return EXIT_SKIPPED
    except SmokeFailure as error:
        print(f"attach: FAILED: {error}")
        return EXIT_FAILED
    print("attach: PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
