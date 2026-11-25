#!/usr/bin/env python3
"""The engine's screenshot set (`.claude/rules/qa-project.md`, "Screenshots"): pictures of the editor's panels, taken in a headless editor on the manual clock.

    python3 tools/stress/qa/editor_shots.py --exe build/macos-debug/bin/BagiStress --label before --out DIR --scenes editor-hierarchy,editor-inspector --window 1920x1080

`tools/shots/shots.py capture` runs this as the set's `command`; run by hand from the root of the repository, with `--out` outside every git work tree.
`{out}/{label}/` gets `<scene>.png` for each requested scene and nothing else (the file name is what `shots.py diff` pairs by); the session's own folder is a
temporary one, removed at the end.

A scene is a panel, not the whole frame: a whole 1920x1080 editor at the tool's long side of about 640 is unreadable, and a one-pixel change of a panel hides
in it. The picture is a crop of a full-resolution screenshot to the rect `imgui_find` gives the panel's window, shrunk to `PANEL_MAX_SIDE`; `editor-frame` is the
whole window above the status bar at `FRAME_MAX_SIDE`, for the layout. The state is the one of the edit charter (`editor_charter.py`): the scene `entities`, the left dock widened,
the Scene Editor open, the node `Run` selected, `TransformComponent` open. `editor-viewport` is taken last, after a `reset` to the scene `sprites` at a small count.

What is kept out of the pictures, so that two runs of one build are the same pixels (the diff's tolerance is 0): the status bar (it draws the control port,
which is a free one each run, the frame rate and the GPU numbers), the pointer is parked outside the window (no
hover colour, no tooltip), the editor's own folder is a temporary one (no `imgui.ini` of an earlier run), the clock is the manual one (no caret blink, no fade),
and no panel that draws a clock or a counter (the Profiler, the Console, the Game panel) is a scene.

The editor's layout is built for one window size, `LAYOUT_WINDOW`: the dock constants of `bagi_qa_editor` are its output pixels. Any other `--window` is refused.
The command never clicks Save; the session still guards the tracked content file and `finish` makes a write of it a finding.

Exit code: 0 done, 1 a move that failed or a window that is not there (the text is on stdout, the scene has no picture), 2 a bad option, an unknown scene, a
`--window` other than `LAYOUT_WINDOW` or an `--out` inside a git work tree, 77 no executable or a build without the editor.
"""

import argparse
import contextlib
import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

TOOLS_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS_DIR / "qa"))
sys.path.insert(0, str(TOOLS_DIR / "control"))
sys.path.insert(0, str(TOOLS_DIR / "shots"))

import bagi_play  # noqa: E402
import bagi_qa_editor as editor  # noqa: E402
import png_ops  # noqa: E402
from bagi_qa import NoEditorError, QaSession  # noqa: E402
from bagi_session import SessionError  # noqa: E402
from png_reader import PngImage, read_png  # noqa: E402

EXIT_OK = bagi_play.EXIT_PASSED
EXIT_FAILED = bagi_play.EXIT_FAILED
EXIT_USAGE = bagi_play.EXIT_USAGE
EXIT_SKIPPED = bagi_play.EXIT_SKIPPED

INVARIANTS = Path(__file__).resolve().with_name("invariants.py")
CHARTER_TEXT = "The editor's panels for the screenshot set"
LAYOUT_WINDOW = "1920x1080"  # the window the editor's default layout, and so `bagi_qa_editor`'s dock constants, are made for
START_FLAGS = ["--stress-scene=entities", "--stress-count=1", f"--window={LAYOUT_WINDOW}"]
RESET_ENTITIES = {"scene": "entities", "count": 1}  # as the edit charter has them (`editor_charter.py`: RESET_ENTITIES, EDITED_NODE, EDITED_COMPONENT)
RESET_SPRITES = {"scene": "sprites", "count": 20}
SELECTED_NODE = "Run"
OPENED_COMPONENT = "TransformComponent"
PARKED_POINTER = (-10.0, -10.0)  # outside the window: over no item, so nothing is hovered and no tooltip waits
SETTLE_FRAMES = 3
FULL_SHOT_SIZE = 4096  # more than any window the layout allows, so the screenshot keeps its output pixels and the rects of `imgui_find` fit it
PANEL_MAX_SIDE = 640  # the tool's size (`tools/shots/CLAUDE.md`); a panel is mostly smaller, so it keeps its native pixels
FRAME_MAX_SIDE = 960  # the whole window at 640 loses the thin separators of the dock
STATUS_BAR = "##StatusBar"  # a child of `##EditorRoot`: its title is `##EditorRoot/##StatusBar_<hash>`
SHOT_LABEL = "frame"  # the label of the full screenshot in the session's scratch folder

SCENE_FRAME = "editor-frame"
SCENE_HIERARCHY = "editor-hierarchy"
SCENE_INSPECTOR = "editor-inspector"
SCENE_VIEWPORT = "editor-viewport"


@dataclass(frozen=True)
class Scene:
    """What a scene shows and where it is cut from: `window` is a title or a part of a child window's title (`None`: the whole frame above the status bar), `max_side` the long side."""

    about: str
    window: str | None
    max_side: int


# In the order they are taken: the viewport last, since its `reset` changes the scene the other panels show. The names are the set's, and the test of
# `test_stress_editor_shots.py` keeps this table and the JSON block of qa-project.md the same.
SCENES: Mapping[str, Scene] = {
    SCENE_FRAME: Scene("the editor window at the default layout: Scene Editor with the node Run selected, TransformComponent open", None, FRAME_MAX_SIDE),
    SCENE_HIERARCHY: Scene("the Scene Editor's hierarchy of StressEntities", editor.HIERARCHY_CHILD, PANEL_MAX_SIDE),
    SCENE_INSPECTOR: Scene("the Scene Editor's inspector of the node Run: TransformComponent open", editor.INSPECTOR_CHILD, PANEL_MAX_SIDE),
    SCENE_VIEWPORT: Scene("the Viewport panel showing the sprites scene at a small count", editor.PANELS[0].window, PANEL_MAX_SIDE),
}
EDITOR_STATE_SCENES = (SCENE_FRAME, SCENE_HIERARCHY, SCENE_INSPECTOR)  # the ones taken from the state of the edit charter


def parse_scenes(text: str) -> list[str]:
    """The scene names of `--scenes a,b`, each once, in the order asked. An unknown or empty list is a ValueError that names the known ones."""
    names: list[str] = []
    for part in text.split(","):
        name = part.strip()
        if not name:
            continue
        if name not in SCENES:
            raise ValueError(f"unknown scene '{name}'; known scenes: {', '.join(SCENES)}")
        if name not in names:
            names.append(name)
    if not names:
        raise ValueError("no scenes requested")
    return names


def order_scenes(names: Sequence[str]) -> list[str]:
    """The names in the order of `SCENES`: the order of taking, whatever the order of asking."""
    return [name for name in SCENES if name in names]


def panel_box(rect: Mapping[str, Any], picture_width: int, picture_height: int) -> list[int]:
    """The inclusive box `[x0, y0, x1, y1]` of `png_ops.crop` for an `imgui_find` rect `{x, y, width, height}`, rounded and clamped to the picture.

    A rect with no area inside the picture (a panel that is not on it) is a ValueError."""
    x0 = max(0, round(rect["x"]))
    y0 = max(0, round(rect["y"]))
    x1 = min(picture_width - 1, round(rect["x"] + rect["width"]) - 1)
    y1 = min(picture_height - 1, round(rect["y"] + rect["height"]) - 1)
    if x1 < x0 or y1 < y0:
        raise ValueError(f"the rect {dict(rect)} has no area inside the picture {picture_width}x{picture_height}")
    return [x0, y0, x1, y1]


def save_picture(image: PngImage, path: Path, max_side: int) -> None:
    """Writes `image` shrunk to the long side `max_side` (never enlarged) as a PNG, into `path`'s folder, which is made when missing."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png_ops.encode_png(png_ops.fit_within(image, max_side)))


def launch(session: QaSession, exe: Path) -> None:
    """Starts the editor; once more on another port when `free_port()` lost the race. NoEditorError for a build without the editor."""
    for attempt in range(2):
        try:
            session.launch(exe=str(exe), extra=START_FLAGS, editor=True)
            return
        except NoEditorError:
            raise
        except SessionError as error:
            if attempt == 0 and "cannot listen on" in str(error):
                continue
            raise


def abandon(session: QaSession) -> None:
    """Ends a session whose script died on an unexpected exception: the game is stopped and the content file put back, then the exception goes on."""
    with contextlib.suppress(SessionError):
        session.finish(content_writes_expected=False)


def park_pointer(session: QaSession) -> None:
    x, y = PARKED_POINTER
    session.imgui_input([{"type": "move", "x": x, "y": y}])
    session.step(SETTLE_FRAMES)


def take_screenshot(session: QaSession) -> tuple[PngImage, float]:
    """The full-resolution screenshot and the factor from the rects of `imgui_find` to its pixels (1 when the rects are in its output pixels, as they should be)."""
    picture = read_png(session.shot(SHOT_LABEL, FULL_SHOT_SIZE).read_bytes())
    display = session.imgui_find(kind="window", limit=1)["display"]
    return picture, picture.width / display["width"]


def panel_rect(session: QaSession, window: str) -> dict[str, Any]:
    """The rect of the window of a panel (`Viewport`), of a child window of the Scene Editor (its title holds `##hierarchy` and a hash) or of the status bar."""
    matches = [match for match in session.imgui_find(label_contains=window, kind="window")["matches"] if match["visible"]]
    if window.startswith("##"):
        matches = [match for match in matches if match["label"].rpartition("/")[2].startswith(window)]
    else:
        matches = [match for match in matches if match["label"] == window]
    if len(matches) != 1:
        raise SessionError(f"{len(matches)} visible windows match '{window}' in the editor; the windows are: {[m['label'] for m in session.imgui_find(kind='window')['matches']]}")
    return matches[0]


def crop_scene(session: QaSession, scene: Scene, picture: PngImage, scale: float) -> PngImage:
    if scene.window is None:
        status = panel_rect(session, STATUS_BAR)
        rect = {"x": 0, "y": 0, "width": picture.width / scale, "height": status["y"]}
    else:
        rect = panel_rect(session, scene.window)
    scaled = {key: rect[key] * scale for key in ("x", "y", "width", "height")}
    try:
        box = panel_box(scaled, picture.width, picture.height)
    except ValueError as error:
        raise SessionError(str(error)) from error
    return png_ops.crop(picture, *box)


def play(session: QaSession, names: Sequence[str], folder: Path) -> None:
    """Puts the editor in the state of the edit charter, takes the scenes in the order of `SCENES` and writes each picture as soon as it is cut."""
    if any(name in names for name in EDITOR_STATE_SCENES):
        session.reset(**RESET_ENTITIES)
        editor.widen_left_dock(session)
        editor.open_panel(session, editor.SCENE_EDITOR)
        editor.select_node(session, SELECTED_NODE)
        editor.open_component(session, OPENED_COMPONENT)
        park_pointer(session)
        picture, scale = take_screenshot(session)
        for name in names:
            if name in EDITOR_STATE_SCENES:
                scene = SCENES[name]
                save_picture(crop_scene(session, scene, picture, scale), folder / f"{name}.png", scene.max_side)
    if SCENE_VIEWPORT in names:
        session.reset(**RESET_SPRITES)
        park_pointer(session)
        picture, scale = take_screenshot(session)
        scene = SCENES[SCENE_VIEWPORT]
        save_picture(crop_scene(session, scene, picture, scale), folder / f"{SCENE_VIEWPORT}.png", scene.max_side)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True, help="the BagiStress executable of this checkout, built with the editor")
    parser.add_argument("--label", required=True, help="the folder of the pictures inside --out (before, after)")
    parser.add_argument("--out", type=Path, required=True, help="the folder of the labels, outside every git work tree")
    parser.add_argument("--scenes", required=True, help="comma-separated: " + ", ".join(SCENES))
    parser.add_argument("--window", default=LAYOUT_WINDOW, help="only %(default)s: the size the editor's layout is built for")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        names = order_scenes(parse_scenes(args.scenes))
    except ValueError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    if args.window != LAYOUT_WINDOW:
        print(f"ERROR the editor layout of the pictures is built for {LAYOUT_WINDOW}, not {args.window}")
        return EXIT_USAGE
    out = args.out.expanduser().resolve()
    if png_ops.inside_git_work_tree(out):
        print(f"ERROR {out} is inside a git work tree: a picture is never committed")
        return EXIT_USAGE
    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED
    scratch = Path(tempfile.mkdtemp(prefix="bagi-editor-shots-"))
    try:
        return run(args.exe.resolve(), scratch, out / args.label, names)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run(exe: Path, scratch: Path, folder: Path, names: Sequence[str]) -> int:
    try:
        session = QaSession(scratch, CHARTER_TEXT, INVARIANTS, ui_every=0)
        launch(session, exe)
    except NoEditorError as error:
        print(f"SKIPPED: {error}")
        return EXIT_SKIPPED
    except SessionError as error:
        print(f"ERROR {error}")
        return EXIT_FAILED
    try:
        play(session, names, folder)
    except SessionError as error:
        print(f"ERROR {error}")
        result = EXIT_FAILED
    except BaseException:
        abandon(session)
        raise
    else:
        result = EXIT_OK
    try:
        session.finish(content_writes_expected=False)
    except SessionError as error:
        print(f"ERROR {error}")
        return EXIT_FAILED
    for finding in session.bugs:
        print(f"FOUND {finding.name}: {finding.message}")
    return result


if __name__ == "__main__":
    sys.exit(main())
