#!/usr/bin/env python3
"""The editor charters of BagiStress: a person's use of the Scene Editor and the panels, played over the control API in a headless editor (no window).

    python3 tools/stress/qa/editor_charter.py --exe build/macos-debug/bin/BagiStress --out DIR [--charter edit|panels|all]

Run from the root of the repository, with `--out` outside every git work tree. The charters (the moves are `tools/qa/bagi_qa_editor.py`, the checks the
session's and `tools/qa/bagi_qa_scene_file.py`'s):

- **edit**: start with `--stress-scene=entities --stress-count=1`, widen the left dock, select the node `Run`, change a number (`TransformComponent` x), the
  layer and the name through the inspector, see the unsaved-changes marker, Save, see it go, and compare the saved `SceneConfig.xml` with the one before
  (the edits are there, nothing else of the scene changed: the findings `editor.*`). Then **reload**: the game is started again on the saved file and the
  hierarchy must show the renamed node. Sessions `<out>/edit` and `<out>/reload`.
- **panels**: open every panel of the View menu, a rebindable chord closes and opens the Game panel, the command palette opens, filters and closes, the
  `ui` scene is shown in the editor; a picture of each. Nothing is saved, so a write of the content file is the finding `editor.content_file_written`.
  The `ui` scene's `ui_tree` is not checked: the canvas of the `Screen` space takes the shape of the viewport panel (a 1436 x 1444 canvas in the default
  layout), the stress grid fills a 1920 x 1080 view, and the labels of the second column are then `OutsideCanvas`: a property of the scene, not of the editor.
  Session `<out>/panels`.

Save writes the tracked `tools/stress/config/SceneConfig.xml` (the content roots are compiled into the executable, which therefore has to be this
checkout's). The sessions put it back after each run (`tools/qa/CLAUDE.md`, "The session"); the reload needs the saved bytes on disk, so the script writes
them there itself and a guard of its own puts the original back at the very end, also when the script fails. The original is kept in `<out>/original/`.
A script killed by a signal cannot put it back: `git status` then shows the file, and the copy in `<out>/original/` is its content.

Exit code: 0 no bug (known deviations do not count), 1 a bug (the session folders hold `bugs/`, `report.md`, `shots/`), 2 a bad option or an `--out` inside a
git work tree, 77 no executable or a build without the editor (`status.runMode` Game).
"""

import argparse
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

TOOLS_DIR = Path(__file__).resolve().parents[2]
ENGINE_ROOT = TOOLS_DIR.parent  # the folder that holds .claude/rules/qa-project.md: `EDITOR_CONTENT_FILES` is relative to it
sys.path.insert(0, str(TOOLS_DIR / "qa"))
sys.path.insert(0, str(TOOLS_DIR / "control"))
sys.path.insert(0, str(TOOLS_DIR / "shots"))

import bagi_play  # noqa: E402
import bagi_qa_checks as checks  # noqa: E402
import bagi_qa_editor as editor  # noqa: E402
import bagi_qa_repro as repro  # noqa: E402
import bagi_qa_scene_file as scene_file  # noqa: E402
import png_ops  # noqa: E402
from bagi_qa import NoEditorError, QaSession  # noqa: E402
from bagi_session import SessionError  # noqa: E402

EXIT_OK = bagi_play.EXIT_PASSED
EXIT_BUG = bagi_play.EXIT_FAILED
EXIT_USAGE = bagi_play.EXIT_USAGE
EXIT_SKIPPED = bagi_play.EXIT_SKIPPED

INVARIANTS = Path(__file__).resolve().with_name("invariants.py")
CHARTER_EDIT = "edit"
CHARTER_PANELS = "panels"
CHARTER_ALL = "all"
EDIT_TEXT = "The editor: edit, save and reload a scene"
PANELS_TEXT = "The editor: panels and chords, no save"
SESSION_EDIT = "edit"
SESSION_RELOAD = "reload"
SESSION_PANELS = "panels"
ORIGINAL_FOLDER = "original"

EDIT_FLAGS = ["--stress-scene=entities", "--stress-count=1"]
RESET_ENTITIES = {"scene": "entities", "count": 1}
RESET_UI = {"scene": "ui", "count": 5}
SCENE_FILE_NAME = "StressEntities"  # the scene of `entities` in SceneConfig.xml
EDITED_NODE = "Run"
EDITED_COMPONENT = "TransformComponent"
EDITED_NUMBER = 37
EDITED_LAYER = 3
RENAMED_NODE = "RunEdited"
GENERATED_NODE = "Entity"  # what StressEntitiesComponent makes under `Field`
EDITS = [  # the rename first: the later edits name the node by its new name, which is where the saved file has it
    {"kind": "node-name", "path": EDITED_NODE, "new_name": RENAMED_NODE},
    {"kind": "component-attribute", "path": RENAMED_NODE, "component": EDITED_COMPONENT, "name": "x", "value": str(EDITED_NUMBER)},
    {"kind": "attribute", "path": RENAMED_NODE, "name": "layer", "value": str(EDITED_LAYER)},
]
EDITOR_SHOT_SIZE = 1280  # the editor's text is unreadable at the session's default 640
PALETTE_FILTER = "game"
PALETTE_FILTER_ROW = "Game Panel"
PALETTE_NO_MATCH = "zzzz"
SETTLE_FRAMES = 3

NAME_MOVE_FAILED = "editor.move_failed"
NAME_MARKER_MISSING = "editor.dirty_marker_missing"
NAME_RELOAD_NODE_MISSING = "editor.reload_node_missing"
NAME_PANEL_MISSING = "editor.panel_missing"
NAME_CHORD_NO_EFFECT = "editor.chord_no_effect"
NAME_PALETTE_FILTER = "editor.palette_filter_wrong"


@dataclass
class Played:
    """One session of a charter, finished: its folder is where the report, the shots and the bug drafts are."""

    name: str
    session: QaSession
    drafts: list[Path]


def launch(session: QaSession, exe: Path, flags: Sequence[str]) -> None:
    """Starts the editor; once more on another port when `free_port()` lost the race. NoEditorError for a build without the editor."""
    for attempt in range(2):
        try:
            session.launch(exe=str(exe), extra=flags, editor=True)
            return
        except NoEditorError:
            raise
        except SessionError as error:
            if attempt == 0 and repro.PORT_TAKEN_MARKER in str(error):
                continue
            raise


def finish(name: str, session: QaSession, content_writes_expected: bool) -> Played:
    finished = session.finish(content_writes_expected=content_writes_expected)
    return Played(name, session, finished.drafts)


def move_failed(session: QaSession, error: SessionError) -> None:
    """A move that could not be made is the editor not showing what a person sees, or a game that is gone: a finding, and the charter ends."""
    session.flag(NAME_MOVE_FAILED, str(error), {"error": str(error)})


def play_edit(exe: Path, out: Path, content_file: Path, invariants: checks.Invariants) -> tuple[Played, bytes | None]:
    """Edit and Save; the saved file is judged, the session ends (the guard puts the original back) and the saved bytes are given for the reload."""
    session = QaSession(out / SESSION_EDIT, EDIT_TEXT, INVARIANTS)
    launch(session, exe, EDIT_FLAGS)
    saved: bytes | None = None
    try:
        before = content_file.read_bytes()
        session.reset(**RESET_ENTITIES)
        editor.widen_left_dock(session)
        editor.open_panel(session, editor.SCENE_EDITOR)
        editor.select_node(session, EDITED_NODE)
        session.shot("selected", EDITOR_SHOT_SIZE)
        editor.open_component(session, EDITED_COMPONENT)
        editor.set_number_field(session, editor.field_label(0), EDITED_NUMBER, EDITED_COMPONENT)
        editor.set_number_field(session, editor.LAYER_FIELD, EDITED_LAYER)
        editor.rename_selected(session, RENAMED_NODE)
        if not editor.dirty(session):
            session.flag(NAME_MARKER_MISSING, "the Scene Editor shows no (unsaved changes) after three edits")
        session.shot("edited", EDITOR_SHOT_SIZE)
        session.note(f"edited {EDITED_NODE}: {EDITED_COMPONENT} x = {EDITED_NUMBER}, layer = {EDITED_LAYER}, name = {RENAMED_NODE}")
        if editor.dirty(session):
            editor.save(session)
            saved = content_file.read_bytes()
            session.shot("saved", EDITOR_SHOT_SIZE)
            session.note("saved through the Save button and compared with the file before")
            for violation in compare(before, saved, invariants):
                session.flag(violation.name, violation.message)
    except SessionError as error:
        move_failed(session, error)
    except scene_file.SceneFileError as error:
        session.flag(NAME_MOVE_FAILED, f"the charter's own edits do not fit the file before: {error}")
    return finish(SESSION_EDIT, session, content_writes_expected=True), saved


def compare(before: bytes, saved: bytes, invariants: checks.Invariants) -> list[checks.Violation]:
    generated = invariants.editor_generated.get(SCENE_FILE_NAME, ())
    return scene_file.compare_scene_files(before, saved, SCENE_FILE_NAME, EDITS, ignore_children_of=generated, added_defaults=invariants.editor_added_defaults)


def play_reload(exe: Path, out: Path, content_file: Path, saved: bytes) -> Played:
    """A fresh start on the saved file: nothing reloads a live scene, so this is what 'reload' is. The hierarchy shows the renamed node; the start writes nothing."""
    content_file.write_bytes(saved)
    session = QaSession(out / SESSION_RELOAD, EDIT_TEXT + " (the start after the save)", INVARIANTS)
    launch(session, exe, EDIT_FLAGS)
    try:
        session.reset(**RESET_ENTITIES)
        editor.widen_left_dock(session)
        found = editor.rows_matching(session, RENAMED_NODE)
        if RENAMED_NODE not in found:
            session.flag(NAME_RELOAD_NODE_MISSING, f"the hierarchy of the second start has no node '{RENAMED_NODE}'; the search left {found}")
        else:
            editor.select_node(session, RENAMED_NODE)
        generated = editor.rows_matching(session, GENERATED_NODE).count(GENERATED_NODE)
        session.note(f"the second start shows {generated} '{GENERATED_NODE}' rows under Field (the saved file holds the one the first run made; the component makes its count again)")
        session.shot("reloaded", EDITOR_SHOT_SIZE)
    except SessionError as error:
        move_failed(session, error)
    return finish(SESSION_RELOAD, session, content_writes_expected=False)


def play_panels(exe: Path, out: Path) -> Played:
    """Every panel of the View menu, a chord, the palette and its filter: a picture of each, and the content file never written."""
    session = QaSession(out / SESSION_PANELS, PANELS_TEXT, INVARIANTS, ui_every=0)
    launch(session, exe, EDIT_FLAGS)
    try:
        known, unknown = editor.view_panels(session)
        if unknown:
            session.note(f"the View menu lists items this charter does not know: {unknown}")
        for title in known:
            editor.open_panel(session, title)
            session.shot(f"panel-{title}", EDITOR_SHOT_SIZE)
        session.note(f"opened {len(known)} panels from the View menu: {known}")
        chord(session)
        palette(session)
        session.reset(**RESET_UI)
        session.step(SETTLE_FRAMES)
        session.shot("ui-scene", EDITOR_SHOT_SIZE)
        session.note("the ui scene shown in the editor; its ui_tree is not checked (the canvas takes the shape of the viewport panel, see qa-project.md)")
    except SessionError as error:
        move_failed(session, error)
    return finish(SESSION_PANELS, session, content_writes_expected=False)


def chord(session: QaSession) -> None:
    """The Game panel's chord shuts it and opens it again; the panel was opened from the menu, so it starts open."""
    for expected in (False, True):
        editor.press_chord(session, editor.GAME_PANEL_CHORD)
        if editor.panel_open(session, "Game Panel") != expected:
            session.flag(NAME_CHORD_NO_EFFECT, f"after Ctrl+Shift+G the Game panel is {'open' if not expected else 'closed'}, expected {'open' if expected else 'closed'}")
    session.shot("chord-game-panel", EDITOR_SHOT_SIZE)


def palette(session: QaSession) -> None:
    editor.open_palette(session)
    session.imgui_type(PALETTE_FILTER)
    rows = editor.palette_rows(session)
    if PALETTE_FILTER_ROW not in rows:
        session.flag(NAME_PALETTE_FILTER, f"the palette filtered by '{PALETTE_FILTER}' lists {rows}, not '{PALETTE_FILTER_ROW}'")
    session.shot("palette", EDITOR_SHOT_SIZE)
    session.imgui_key("A", ["ctrl"])
    session.imgui_type(PALETTE_NO_MATCH)
    rows = editor.palette_rows(session)
    if rows:
        session.flag(NAME_PALETTE_FILTER, f"the palette filtered by '{PALETTE_NO_MATCH}' still lists {rows}")
    editor.close_palette(session)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True, help="the BagiStress executable of this checkout (the editor saves into the content roots compiled into it)")
    parser.add_argument("--out", type=Path, required=True, help="the folder of the sessions, outside every git work tree")
    parser.add_argument("--charter", choices=(CHARTER_EDIT, CHARTER_PANELS, CHARTER_ALL), default=CHARTER_ALL, help="default: %(default)s")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    out = args.out.expanduser().resolve()
    if png_ops.inside_git_work_tree(out):
        print(f"ERROR {out} is inside a git work tree: the sessions write pictures and recordings that are never committed")
        return EXIT_USAGE
    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED
    exe = args.exe.resolve()
    try:
        invariants = checks.load_invariants(INVARIANTS)
        content_file = ENGINE_ROOT / invariants.editor_content_files[0]
        out.mkdir(parents=True, exist_ok=True)
        outer = scene_file.ContentGuard([content_file], out / ORIGINAL_FOLDER)
        outer.arm()
    except (checks.InvariantsError, scene_file.ContentGuardError, IndexError, OSError) as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    played: list[Played] = []
    try:
        if args.charter in (CHARTER_EDIT, CHARTER_ALL):
            first, saved = play_edit(exe, out, content_file, invariants)
            played.append(first)
            if saved is not None:
                played.append(play_reload(exe, out, content_file, saved))
        if args.charter in (CHARTER_PANELS, CHARTER_ALL):
            played.append(play_panels(exe, out))
    except NoEditorError as error:
        print(f"SKIPPED: {error}")
        return EXIT_SKIPPED
    except SessionError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    finally:
        outer.release()
    return report(played, out)


def report(played: Sequence[Played], out: Path) -> int:
    bugs = 0
    for item in played:
        session = item.session
        print(f"CHARTER {item.name}: {session.calls} calls, {len(session.bugs)} bug(s), {len(session.known)} known")
        for finding in session.bugs:
            print(f"FOUND {finding.name}  after call {finding.call_index}\n  {finding.message}")
        for finding in session.known:
            print(f"KNOWN {finding.name}\n  {finding.message}")
        for note in session.notes:
            print(f"NOTE {note}")
        for path in item.drafts:
            print(f"DRAFT {path}")
        bugs += len(session.bugs)
    print(f"OUT {out}")
    return EXIT_BUG if bugs else EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
