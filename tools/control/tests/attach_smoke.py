#!/usr/bin/env python3
"""Smoke test of the editor's Attach over two processes: a headless BagiStress with --control, and a headless editor of it started with --attach.

    python tools/control/tests/attach_smoke.py --exe build/macos-debug/bin/BagiStress

In the editor's UI (imgui_find / imgui_input) a node of the hierarchy is picked and its layer stepped up. The game's layer is set apart from the editor's
own first, so the step shows that the editor read the game's value and wrote to the game: the game's scene_tree has the next layer, the editor's own scene is
as it was and has no unsaved change. Then the game quits: the hierarchy takes no input any more but still shows what was read, and the editor stays alive.
Neither log may hold a warning or an error.

The editor keeps its dock layout in a file of the checkout that no flag redirects: the test takes it away for the run, so that the editor starts on the
default layout, and puts it back. Exit code: 0 passed, 1 failed, 77 skipped (no executable, or a build without the editor).
"""

import argparse
import contextlib
import os
import sys
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Callable

ENGINE_DIR = Path(os.environ["BAGI_ENGINE_DIR"]) if "BAGI_ENGINE_DIR" in os.environ else Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "qa"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
import bagi_qa_editor as editor  # noqa: E402
from bagi_session import GameSession, SessionError  # noqa: E402
from game_process import PORT_TAKEN_MARKER, GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from smoke import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, SmokeFailure, check  # noqa: E402

SCENE_FLAGS = ["--stress-scene=entities", "--stress-count=1"]  # a small tree, the same in both programs
NODE = "Run"
EDITOR_WINDOW = "--window=1280x720"  # the size the splitter of `widen_left_dock` is measured on
GAME_LAYER = 7  # unlike the layer the scene file gives the node, so that the two programs differ
LAYER_STEP_BUTTON = "+"  # of the inspector's layer stepper, the one such button while every component section is closed
LAYOUT_FILE = ENGINE_DIR / "tools" / "stress" / "config" / "imgui.ini"
WAIT_SECONDS = 30.0
RETRY_SECONDS = 0.1
SHUTDOWN_TIMEOUT = 15.0
EDITOR_FRAMES = 5  # ImGui lays its docks out over more than one frame
ATTACHED_LINE = "Remote editor: attached to"
LOST_LINE = "Remote editor: no connection to"


class Skipped(Exception):
    pass


def wait_until(attempt: Callable[[], bool], what: str) -> None:
    deadline = time.monotonic() + WAIT_SECONDS
    while not attempt():
        check(time.monotonic() < deadline, f"{what}: not in {WAIT_SECONDS} s")
        time.sleep(RETRY_SECONDS)


def layer_of(program: BagiControlClient | GameSession) -> int:
    """The layer of NODE in the scene of the program itself (`scene_tree`)."""
    nodes = program.call("scene_tree")["nodes"]
    found = [node for node in nodes if node["name"] == NODE]
    check(len(found) == 1, f"{len(found)} nodes are called '{NODE}' in {[node['path'] for node in nodes]}")
    return found[0]["layer"]


def node_path(program: BagiControlClient) -> str:
    return next(node["path"] for node in program.call("scene_tree")["nodes"] if node["name"] == NODE)


def picked(session: GameSession) -> bool:
    """A click on the node's row; False while the hierarchy takes no input yet."""
    try:
        editor.select_node(session, NODE)
    except SessionError:
        if not session.is_connected:
            raise
        return False
    return True


def takes_input(session: GameSession) -> bool:
    """Whether a click on the hierarchy's search field gives it the keyboard; the field is left again."""
    field = editor.find_item(session, editor.CHILD_WINDOWS, editor.HIERARCHY_CHILD, label_contains=editor.SEARCH_FIELD)
    taken = editor.click_item(session, field)["wantTextInput"]
    if taken:
        session.imgui_key("Escape")
    return taken


def drive(game: GameProcess, game_client: BagiControlClient, session: GameSession) -> None:
    session.step(EDITOR_FRAMES)
    editor.widen_left_dock(session)
    editor.open_panel(session, editor.SCENE_EDITOR)
    own_layer = layer_of(session)
    check(own_layer != GAME_LAYER, f"the scene already has '{NODE}' on layer {GAME_LAYER}")

    wait_until(lambda: picked(session), f"picking '{NODE}' in the attached editor")
    steps = [item for item in editor.find_items(session, editor.CHILD_WINDOWS, editor.INSPECTOR_CHILD, label=LAYER_STEP_BUTTON) if item["visible"]]
    check(len(steps) == 1, f"{len(steps)} layer step buttons in the inspector: {editor.inspector_labels(session)}")
    editor.click_item(session, steps[0])
    wait_until(lambda: layer_of(game_client) == GAME_LAYER + 1, f"the game's '{NODE}' on layer {GAME_LAYER + 1}")
    check(layer_of(session) == own_layer, f"the editor's own '{NODE}' moved to layer {layer_of(session)}")
    check(not editor.dirty(session), "the editor's own scene has unsaved changes")
    print(f"attach: a layer step in the editor put the game's '{NODE}' on layer {GAME_LAYER + 1}, the editor's own stays on {own_layer}", flush=True)

    check(takes_input(session), "the hierarchy of an attached editor takes no input")
    game_client.call("quit")
    check(game.stop(SHUTDOWN_TIMEOUT) == 0, f"the game exited with code {game.exit_code} after quit")
    wait_until(lambda: not takes_input(session), "a read-only hierarchy after the game quit")
    check(NODE in editor.hierarchy_names(session), f"the rows that were read left the screen: {editor.hierarchy_names(session)}")
    check(bool(editor.find_items(session, editor.CHILD_WINDOWS, editor.INSPECTOR_CHILD, label_contains=editor.NAME_FIELD)), "the inspected node left the screen")
    check(session.call("status")["runMode"] == "Editor", "the editor does not answer after the game quit")
    print("attach: the game quit, the editor shows what was read, read-only, and runs on", flush=True)


def check_log(program: GameProcess, name: str, lines: tuple[str, ...] = ()) -> None:
    """For a program that has exited: its output is complete only then."""
    problems = program.log_problems()
    check(not problems, f"the {name}'s log has problems:\n" + "\n".join(problems))
    text = program.log_path.read_text(errors="replace")
    for line in lines:
        check(line in text, f"the {name}'s log has no line '{line}'\n--- log (tail) ---\n{program.log_tail()}")


def play(exe: Path, overlay: Path) -> None:
    game_port = free_port()
    game = GameProcess(build_game_command(exe, game_port, extra=SCENE_FLAGS), cwd=exe.parent)
    editor_port = free_port()
    editor_flags = ["--editor", EDITOR_WINDOW, f"--attach=ws://127.0.0.1:{game_port}", f"--content-overlay={overlay}", *SCENE_FLAGS]
    editor_process = GameProcess(build_game_command(exe, editor_port, extra=editor_flags), cwd=exe.parent)
    session = GameSession()
    try:
        game.start()
        game_client = game.connect(game_port)
        game_client.call("scene_set_node", {"node": node_path(game_client), "layer": GAME_LAYER})
        editor_process.start()
        first = editor_process.connect(editor_port)
        run_mode = first.call("status")["runMode"]
        if run_mode == "Game":
            raise Skipped("status.runMode is Game (the build has no editor)")
        check(run_mode == "Editor", f"status.runMode is '{run_mode}' with --editor")
        first.close()
        session.connect(port=editor_port)

        drive(game, game_client, session)

        session.call("quit")
        check(editor_process.stop(SHUTDOWN_TIMEOUT) == 0, f"the editor exited with code {editor_process.exit_code} after quit")
        check_log(game, "game")
        check_log(editor_process, "editor", (ATTACHED_LINE, LOST_LINE))
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
def temporary_editor_folders() -> Iterator[Path]:
    """$BAGI_EDITOR_DIR (preferences, layouts) for the editor started inside, and the folder its writes of content go into."""
    previous = os.environ.get("BAGI_EDITOR_DIR")
    with tempfile.TemporaryDirectory() as user, tempfile.TemporaryDirectory() as overlay:
        os.environ["BAGI_EDITOR_DIR"] = user
        try:
            yield Path(overlay)
        finally:
            if previous is None:
                del os.environ["BAGI_EDITOR_DIR"]
            else:
                os.environ["BAGI_EDITOR_DIR"] = previous


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True, help="the built BagiStress")
    exe = parser.parse_args(argv).exe.resolve()
    if not exe.is_file():
        print(f"attach: SKIPPED, there is no executable at {exe}")
        return EXIT_SKIPPED
    try:
        with default_layout(), temporary_editor_folders() as overlay:
            try:
                play(exe, overlay)
            except SmokeFailure as error:
                # free_port gives the port back before the program binds it
                if PORT_TAKEN_MARKER not in str(error):
                    raise
                print("attach: a port was taken by another process; retrying once", flush=True)
                play(exe, overlay)
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
