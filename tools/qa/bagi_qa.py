"""The session a QA charter script drives: one game on the manual clock, every step checked, what was covered kept for the report.

    session = QaSession(out_dir, "every scene and every language")
    session.launch(preset="macos-debug", game="BagiStress")
    session.step(30)
    outcome = session.act({"type": "set_count", "count": 100})
    session.shot("count-100")
    session.note("count 100 on every scene")
    finished = session.finish()   # finished.report, finished.drafts: report.md, bugs/<id>.md

Standard library only (through tools/control). The checks are in bagi_qa_checks.py; `tools/qa/CLAUDE.md` says what is checked after which call.
A `SessionError` (the game is gone, did not answer, did not start) is raised to the script and kept in `session_errors` for the report.

The command line works on a session that was recorded (`tools/qa/CLAUDE.md`, "Reproducing a finding"):

    python3 tools/qa/bagi_qa.py replay --exe E [--invariants M] SESSION.jsonl
    python3 tools/qa/bagi_qa.py shrink --exe E [--invariants M] [--finding NAME] [--out FILE] [--max-probes N] SESSION.jsonl
    python3 tools/qa/bagi_qa.py report SESSION_DIR      # the report of a finished session again
    python3 tools/qa/bagi_qa.py drafts SESSION_DIR      # the bug drafts again, with the reproductions `shrink` wrote
Exit code of replay and shrink: 0 nothing found, 1 a finding recurs (`shrink`: and its reproduction is written), 2 a bad file or option or a finding that does not recur, 77 no executable.
"""

import argparse
import atexit
import base64
import contextlib
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterator, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "shots"))
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

import bagi_qa_checks as checks  # noqa: E402
import bagi_qa_report as report_module  # noqa: E402
import bagi_qa_scene_file as scene_file  # noqa: E402
import png_ops  # noqa: E402
import scenario  # noqa: E402
from bagi_control_client import ControlError  # noqa: E402
from bagi_qa_checks import PERFORM_ACTION_METHOD, QUIT_METHOD, STEP_METHOD, UI_TREE_METHOD, Finding, Invariants  # noqa: E402
from bagi_session import EDITOR_FLAG, GameSession, SessionError  # noqa: E402
from game_process import preset_executable  # noqa: E402

DEFAULT_UI_EVERY = 10  # `ui_tree` is the dearest check: a step runs it every this many steps
LAUNCH_SETTLE_FRAMES = 2  # canvases are laid out by a draw, so the first checks come after a couple of frames
SHOT_SETTLE_FRAMES = 2  # the picture shows the state only after the frames that follow it
SHOT_MAX_SIZE = 640
HEADLESS_FLAG = "--headless"
MANUAL_CLOCK_FLAG = "--manual-clock"
SCENARIO_FILE = "session.jsonl"
SHOTS_FOLDER = "shots"
SHOT_LABEL_UNSAFE = re.compile(r"[^A-Za-z0-9_-]+")
SHOT_NUMBER_DIGITS = 2
LATE_LOG_NOTE = " (shown only after the game quit: its stdout is block-buffered)"
EDITOR_DIR_VARIABLE = "BAGI_EDITOR_DIR"  # where the editor keeps its per-user files (preferences, layouts): a folder of the session, never the real one
EDITOR_DIR_FOLDER = "editor-dir"
EDITOR_BACKUP_FOLDER = "content-backup"
RUN_MODE_EDITOR = "Editor"
QA_PROJECT_RULES = Path(".claude") / "rules" / "qa-project.md"  # its folder is the root `EDITOR_CONTENT_FILES` is relative to
NAME_CONTENT_FILE_WRITTEN = "editor.content_file_written"
REPEATS_KEY = "repeats"  # details of a finding: how many more times the same name and message showed


@dataclass
class Finished:
    """What `finish()` leaves for the skill to go on with: the recording, the report (`report.md`, `report.json`) and the bodies of the bug drafts (`bugs/<id>.md`)."""

    scenario: Path
    report: Path
    report_json: Path
    drafts: list[Path]
    restored: list[Path] = field(default_factory=list)  # the content files the editor wrote and the guard put back (empty without `--editor`)


class NoEditorError(SessionError):
    """The game was started with `--editor` and answered `status.runMode` Game: a build without the editor, so an editor charter is skipped, not failed."""


@dataclass
class ActOutcome:
    """What the game answered to an action: accepted with a `result`, or refused with an `error_code` (the session does not raise for a refusal)."""

    accepted: bool
    result: Any = None
    error_code: str | None = None
    error_message: str = ""


class _CountedGame(GameSession):
    """A GameSession that reports each control call it made (answered or refused), so the helpers built on `call` (`imgui_click` sends a find and an input)
    are counted as the recording has them. The `quit` that ends a session is not a line of the recording."""

    def __init__(self, counted: Callable[[], None]):
        super().__init__()
        self._counted = counted

    def call(self, method: str, params: dict[str, Any] | None = None, timeout: float | None = None) -> Any:
        try:
            result = super().call(method, params, timeout)
        except ControlError:
            self._report(method)
            raise
        self._report(method)
        return result

    def _report(self, method: str) -> None:
        if method != QUIT_METHOD:
            self._counted()


@contextlib.contextmanager
def _editor_environment(folder: Path) -> Iterator[None]:
    """`BAGI_EDITOR_DIR` is `folder` while the game starts (it inherits the variable) and what it was before afterwards."""
    folder.mkdir(parents=True, exist_ok=True)
    previous = os.environ.get(EDITOR_DIR_VARIABLE)
    os.environ[EDITOR_DIR_VARIABLE] = str(folder)
    try:
        yield
    finally:
        if previous is None:
            os.environ.pop(EDITOR_DIR_VARIABLE, None)
        else:
            os.environ[EDITOR_DIR_VARIABLE] = previous


def _git_toplevel(folder: Path) -> Path | None:
    try:
        done = subprocess.run(["git", "-C", str(folder), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    except OSError:
        return None
    return Path(done.stdout.strip()).resolve() if done.returncode == 0 and done.stdout.strip() else None


def _rules_root(invariants_path: str) -> Path | None:
    """The folder above the invariants file that holds `.claude/rules/qa-project.md`: the root the module's relative paths mean."""
    for folder in Path(invariants_path).resolve().parents:
        if (folder / QA_PROJECT_RULES).is_file():
            return folder
    return None


class QaSession:
    """At most one game. `findings` are everything found, `bugs` and `known` the split by the game's KNOWN_DEVIATIONS."""

    def __init__(self, out_dir: str | Path, charter: str, invariants: Invariants | str | Path | None = None, ui_every: int = DEFAULT_UI_EVERY):
        out = Path(out_dir).expanduser().resolve()
        if png_ops.inside_git_work_tree(out):
            raise SessionError(f"{out} is inside a git work tree: a QA session writes pictures and a recording, and those are never committed; use a folder outside every repository")
        out.mkdir(parents=True, exist_ok=True)
        self.out_dir = out
        self.charter = charter
        self.ui_every = ui_every
        self.invariants = checks.load_invariants(invariants) if isinstance(invariants, (str, Path)) else invariants
        self.findings: list[Finding] = []
        self.notes: list[str] = []
        self.shots: list[tuple[str, Path]] = []
        self.session_errors: list[str] = []
        self.game: str | None = None
        self.flags: list[str] = []
        self.calls = 0  # control calls the game answered or refused: the lines of the recording
        self.steps = 0
        self.actions = 0
        self.refused = 0
        self.restored_files: list[Path] = []  # the content files the guard had to put back (`finish`)
        self._game = _CountedGame(self._count_call)
        self._guard: scene_file.ContentGuard | None = None
        self._guard_problem: str | None = None
        self._content_root: Path | None = None
        self._launched = False
        self._finished: Finished | None = None
        self._state: dict[str, Any] | None = None
        self._version: int | None = None
        self._listed: tuple[int | None, list[dict[str, Any]]] | None = None
        self._log_seen = 0
        self._sightings: dict[tuple[str, str], Finding] = {}

    @property
    def scenario_path(self) -> Path:
        return self.out_dir / SCENARIO_FILE

    @property
    def known_names(self) -> frozenset[str]:
        return self.invariants.known_deviations if self.invariants is not None else frozenset()

    @property
    def bugs(self) -> list[Finding]:
        return checks.apply_known(self.findings, self.known_names)[0]

    @property
    def known(self) -> list[Finding]:
        return checks.apply_known(self.findings, self.known_names)[1]

    def launch(
        self,
        exe: str | None = None,
        preset: str | None = None,
        game: str | None = None,
        extra: Sequence[str] = (),
        replay: bool = False,
        editor: bool = False,
        content_files: Sequence[str | Path] | None = None,
    ) -> dict[str, Any]:
        """Starts the game headless on the manual clock, recording to `session.jsonl`, lets a couple of frames pass and checks it (the `ui_tree` too).

        `replay` is for playing a recorded scenario (bagi_qa_repro): the scenario holds its own first calls, so no frame is run and nothing is recorded.

        `editor` (or `--editor` in `extra`, which is what a replayed header gives) starts the editor with `BAGI_EDITOR_DIR` set to a folder of this session
        for the start only, and needs `status.runMode` to be `Editor`: a build without the editor is stopped and raises `NoEditorError`. The editor's Save
        writes into the checkout, so `content_files` (default: `EDITOR_CONTENT_FILES` of the invariants module, relative to the folder that holds
        `.claude/rules/qa-project.md` above it) are backed up before the game starts and put back by `finish()`, also when the game is lost or does not
        start; an editor launch that has neither is refused with `SessionError` before the process starts. The executable must belong to the checkout of those files (the content roots are compiled into it). Without the editor nothing changes.
        """
        if self._launched:
            raise SessionError("this session already launched a game; make another QaSession")
        editor = editor or EDITOR_FLAG in extra
        extra = [flag for flag in extra if flag != EDITOR_FLAG]
        if editor:
            self._arm_guard(exe, preset, game, content_files)
        try:
            with _editor_environment(self.out_dir / EDITOR_DIR_FOLDER) if editor else contextlib.nullcontext():
                description = self._game.launch(exe=exe, preset=preset, game=game, extra=extra, record=None if replay else str(self.scenario_path), editor=editor)
        except SessionError as error:
            self.session_errors.append(str(error))
            self._release_guard()
            raise
        run_mode = (description.get("status") or {}).get("runMode")
        if editor and run_mode != RUN_MODE_EDITOR:
            self._game.close()
            self._release_guard()
            error = NoEditorError(f"the build has no editor: status.runMode is {run_mode}")
            self.session_errors.append(str(error))
            raise error
        self._launched = True
        self.game = description.get("activeGame")
        self.flags = [HEADLESS_FLAG, MANUAL_CLOCK_FLAG, *([EDITOR_FLAG] if editor else []), *extra]
        if not replay:
            self._step(LAUNCH_SETTLE_FRAMES, None, ui=True)
        return description

    def step(self, frames: int = 1, dt: float | None = None) -> Any:
        """Runs frames on the manual clock, then checks (the `ui_tree` only every `ui_every` steps)."""
        self.steps += 1
        return self._step(frames, dt, ui=self.ui_every > 0 and self.steps % self.ui_every == 0)

    def act(self, action: dict[str, Any], expect_refusal: bool = False) -> ActOutcome:
        """Performs an action, checks the answer (a listed action is accepted, the version holds) and the game's `check_action`, then the state.

        An action the charter invents may be refused on purpose: `expect_refusal=True` skips the listed-action check for it. A refusal is an
        ActOutcome, not an exception.
        """
        before = self.current_state()
        listed = [] if expect_refusal else self.listed_actions()
        outcome = self._perform(action)
        index = self.calls
        self._add(checks.check_action_accepted(action, listed, outcome.error_code), index)
        if outcome.accepted:
            self._add(self._version_findings(outcome.result), index)
        after = self.state()
        self._add(checks.run_action_checks(self.invariants, action, outcome.result, before, after), index)
        self._check_log()
        return outcome

    def replay_call(self, method: str, params: dict[str, Any] | None = None) -> ActOutcome:
        """Sends a recorded call as it was, then checks what it shows (for playing a scenario back, bagi_qa_repro).

        An action goes through `act`, any other call through `call`. The checks add read-only calls (`get_state`, `list_actions`, `ui_tree`),
        which change nothing in the game.
        """
        if method == PERFORM_ACTION_METHOD:
            return self.act(params or {})
        return self.call(method, params)

    def call(self, method: str, params: dict[str, Any] | None = None) -> ActOutcome:
        """Any control call a script or the monkey makes that has no method of its own (`ui_click`, `set_window`, ...): a refusal is an outcome, not an exception.

        The version of the answer is checked, the `ui_tree` too when the call is one, then `check` (with the `ui_tree` after a `step`).
        """
        try:
            result = self._call(method, params)
        except ControlError as error:
            self._check_log()
            return ActOutcome(False, None, error.code, error.message)
        index = self.calls
        self._add(self._version_findings(result), index)
        if method == UI_TREE_METHOD:
            self._add(checks.check_ui_tree(result), index)
        self.check(ui=method == STEP_METHOD)
        return ActOutcome(True, result)

    def imgui_find(self, label: str | None = None, **filters: Any) -> dict[str, Any]:
        """`GameSession.imgui_find`, recorded and counted; the log is checked after it (an ImGui assertion shows at once)."""
        return self._imgui(self._game.imgui_find, label, **filters)

    def imgui_click(self, label: str | None = None, **target: Any) -> dict[str, Any]:
        """`GameSession.imgui_click`: the one visible item called `label` (in `window`), or a point `x`, `y`. An item that is not there, or is there
        several times, is a SessionError kept in `session_errors`, and nothing is clicked."""
        return self._imgui(self._game.imgui_click, label, **target)

    def imgui_input(self, events: Sequence[dict[str, Any]], space: str | None = None) -> dict[str, Any]:
        """`GameSession.imgui_input` (raw events: a drag, a key held), recorded, counted and followed by the log check."""
        return self._imgui(self._game.imgui_input, events, space)

    def imgui_type(self, text: str) -> dict[str, Any]:
        """`GameSession.imgui_type`, recorded, counted and followed by the log check."""
        return self._imgui(self._game.imgui_type, text)

    def imgui_key(self, key: str, mods: Sequence[str] = ()) -> dict[str, Any]:
        """`GameSession.imgui_key`, recorded, counted and followed by the log check."""
        return self._imgui(self._game.imgui_key, key, mods)

    def reset(self, **params: Any) -> Any:
        """The game's `reset` (its options are the game's: tools/stress/CLAUDE.md for BagiStress), then the checks. A refusal is a ControlError."""
        result = self._call("reset", params or None)
        self._add(self._version_findings(result), self.calls)
        self.check()
        return result

    def state(self) -> dict[str, Any]:
        """The full `get_state` answer, checked against the version seen before and the game's `check_state` (its `previous` is the answer before this one)."""
        answer = self._call("get_state")
        index = self.calls
        self._add(self._version_findings(answer), index)
        self._add(checks.run_state_checks(self.invariants, answer, self._state), index)
        self._state = answer
        return answer

    def listed_actions(self) -> list[dict[str, Any]]:
        """The actions `list_actions` offers now; kept while the version does not move."""
        if self._listed is not None and self._version is not None and self._listed[0] == self._version:
            return self._listed[1]
        answer = self._call("list_actions")
        self._add(self._version_findings(answer), self.calls)
        actions = list(answer.get("actions", [])) if isinstance(answer, dict) else []
        self._listed = (self._version, actions)
        return actions

    def check(self, ui: bool = False) -> None:
        """Every check but the action ones: the state (version, `check_state`), the log and, with `ui`, the `ui_tree` (it needs a frame since the last change: `step` first)."""
        self.state()
        if ui:
            self._check_ui()
        self._check_log()

    def shot(self, label: str, max_size: int = SHOT_MAX_SIZE) -> Path:
        """A PNG of the next frame, `shots/<NN>-<label>.png` in the out folder, after a couple of frames so it shows the state; the path.

        `max_size` is the longer side: the editor's panels are unreadable at the default, so an editor charter asks for more."""
        self._call(STEP_METHOD, {"frames": SHOT_SETTLE_FRAMES})
        answer = self._call("screenshot", {"maxSize": max_size})
        folder = self.out_dir / SHOTS_FOLDER
        folder.mkdir(exist_ok=True)
        name = f"{len(self.shots) + 1:0{SHOT_NUMBER_DIGITS}d}-{SHOT_LABEL_UNSAFE.sub('-', label).strip('-') or 'shot'}.png"
        path = folder / name
        path.write_bytes(base64.b64decode(answer[scenario.SCREENSHOT_PICTURE_KEY]))
        self.shots.append((label, path))
        return path

    def flag(self, name: str, message: str, details: dict[str, Any] | None = None, expect: dict[str, Any] | None = None) -> None:
        """A finding of the charter's own, for what no check can see (a HUD or a picture that disagrees with the state), at the last call.

        `bagi_qa.py replay` plays only the generic and the module's checks, so a flagged finding never recurs there: the script that flags it
        reproduces it by a scenario of its own (`repro/<name>.jsonl`, whose last line holds `expect`, a scenario expectation that fails while the
        bug is there).
        """
        found = Finding("invariant", name, message, details={**(details or {}), checks.FLAGGED_KEY: True}, expect=expect)
        self._add([found], self.calls)

    def add_findings(self, found: Sequence[Finding]) -> None:
        """Findings a script got from the checks' functions itself (`run_monkey_moves`), kept at the last call like the session's own."""
        self._add(found, self.calls)

    def current_state(self) -> dict[str, Any]:
        """The newest `get_state` answer: every action, step and call of the session refreshes it, so this asks the game only before the first one."""
        return self._state if self._state is not None else self.state()

    def note(self, text: str) -> None:
        """What the charter covered, counted into the report."""
        self.notes.append(text)

    def finish(self, content_writes_expected: bool = True) -> Finished:
        """Quits the game, checks the log once more (a Warning may show only now), puts the content files back, writes the recording, the report and the bug drafts. Safe to call twice.

        The drafts name the recording as their reproduction; `bagi_qa.py shrink` and `bagi_qa.py drafts` replace it by a cut one. A content file the editor
        wrote is restored whatever happens; `content_writes_expected=False` (a charter that never saves) makes each one the finding
        `editor.content_file_written`. A file that cannot be put back is a SessionError after the report is written, and the paths are in `restored_files`.
        """
        if not self._launched:
            raise SessionError("no game was launched, so there is no session to finish")
        if self._finished is None:
            try:
                self._game.quit()
                self._check_log(late=True)
            finally:
                restored = self._release_guard()
            if not content_writes_expected:
                for path in restored:
                    self.flag(NAME_CONTENT_FILE_WRITTEN, f"the editor wrote the content file {self.shown_path(path)} in a session that did not save", {"file": self.shown_path(path)})
            try:
                report = report_module.build_report(self)
                markdown, data = report_module.write_report(report, self.out_dir)
                drafts = report_module.write_drafts(report_module.make_drafts(report, self.out_dir), self.out_dir)
            except OSError as error:
                raise SessionError(f"cannot write the report into {self.out_dir}: {error}") from error
            self._finished = Finished(self.scenario_path, markdown, data, drafts, list(self.restored_files))
            if self._guard_problem is not None:
                raise SessionError(self._guard_problem)
        return self._finished

    def _arm_guard(self, exe: str | None, preset: str | None, game: str | None, content_files: Sequence[str | Path] | None) -> None:
        files = self._content_file_paths(content_files)
        if not files:
            if content_files is None:
                raise SessionError(
                    "an editor start needs to know which content files its Save writes: pass `content_files` (an empty list opts out) or an invariants module with EDITOR_CONTENT_FILES"
                )
            return
        exe_path = Path(exe).expanduser().resolve() if exe else preset_executable(preset or "", game or "").resolve()
        top = _git_toplevel(files[0].parent)
        if top is not None and not exe_path.is_relative_to(top):
            raise SessionError(f"the executable {exe_path} is not in the checkout of the content file {files[0]} ({top}): the editor saves into the content roots compiled into its own build")
        guard = scene_file.ContentGuard(files, self.out_dir / EDITOR_BACKUP_FOLDER)
        try:
            guard.arm()
        except scene_file.ContentGuardError as error:
            raise SessionError(str(error)) from error
        self._guard = guard
        atexit.register(self._release_guard)  # a script that dies before `finish` must not leave the checkout written

    def _content_file_paths(self, content_files: Sequence[str | Path] | None) -> list[Path]:
        if content_files is not None:
            return [Path(file).expanduser().resolve() for file in content_files]
        if self.invariants is None or not self.invariants.editor_content_files:
            return []
        root = _rules_root(self.invariants.path)
        if root is None:
            raise SessionError(f"{self.invariants.path} names EDITOR_CONTENT_FILES, but no folder above it holds {QA_PROJECT_RULES.as_posix()}: pass `content_files`")
        self._content_root = root
        return [(root / file).resolve() for file in self.invariants.editor_content_files]

    def _release_guard(self) -> list[Path]:
        """Puts the content files back (once); the files it had to restore. A failure is kept in `session_errors` and raised by `finish`."""
        guard, self._guard = self._guard, None
        if guard is None:
            return []
        try:
            restored = guard.release()
        except scene_file.ContentGuardError as error:
            self.session_errors.append(str(error))
            self._guard_problem = str(error)
            return []
        self.restored_files.extend(restored)
        return restored

    def shown_path(self, path: Path) -> str:
        """A content file as the report shows it: relative to the root of the rules file when there is one."""
        if self._content_root is not None and path.is_relative_to(self._content_root):
            return path.relative_to(self._content_root).as_posix()
        return str(path)

    def _imgui(self, method: Callable[..., dict[str, Any]], *arguments: Any, **keywords: Any) -> dict[str, Any]:
        try:
            result = method(*arguments, **keywords)
        except SessionError as error:
            self.session_errors.append(str(error))
            raise
        finally:
            self._check_log()
        return result

    def _count_call(self) -> None:
        self.calls += 1

    def _step(self, frames: int, dt: float | None, ui: bool) -> Any:
        params: dict[str, Any] = {"frames": frames}
        if dt is not None:
            params["dt"] = dt
        result = self._call(STEP_METHOD, params)
        self.check(ui=ui)
        return result

    def _perform(self, action: dict[str, Any]) -> ActOutcome:
        self.actions += 1
        try:
            result = self._call(PERFORM_ACTION_METHOD, action)
        except ControlError as error:
            self.refused += 1
            return ActOutcome(False, None, error.code, error.message)
        return ActOutcome(True, result)

    def _call(self, method: str, params: dict[str, Any] | None = None) -> Any:
        """Every call of the session goes through here: `calls` is the number of the line in the recording."""
        try:
            return self._game.call(method, params)
        except SessionError as error:
            self.session_errors.append(str(error))
            raise

    def _version_findings(self, answer: Any) -> list[Finding]:
        version = scenario.version_of(answer)
        found = checks.check_version(self._version, version)
        if version is not None:
            self._version = version
        return found

    def _check_ui(self) -> None:
        try:
            tree = self._call(UI_TREE_METHOD)
        except ControlError:
            return  # no active scene: nothing to lay out
        self._add(checks.check_ui_tree(tree), self.calls)

    def _check_log(self, late: bool = False) -> None:
        try:
            lines = self._game.log_problems()
        except SessionError:
            return
        found = checks.check_log(lines, self._log_seen)
        self._log_seen = len(lines)
        if late:
            for finding in found:
                finding.message += LATE_LOG_NOTE
                finding.details["late"] = True
        self._add(found, self.calls)

    def _add(self, found: Sequence[Finding], call_index: int) -> None:
        """Keeps the findings with their call; the same name and message again only counts on the first sighting."""
        for finding in found:
            key = (finding.name, finding.message)
            first = self._sightings.get(key)
            if first is not None:
                first.details[REPEATS_KEY] = first.details.get(REPEATS_KEY, 0) + 1
                continue
            kept = finding.at(call_index)
            self._sightings[key] = kept
            self.findings.append(kept)


def build_parser() -> argparse.ArgumentParser:
    import bagi_qa_repro  # imported here: it imports QaSession from this module

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    replay = commands.add_parser("replay", help="play a recorded scenario with the checks and print what it finds")
    bagi_qa_repro.add_arguments(replay, shrinking=False)
    replay.set_defaults(run=bagi_qa_repro.command_replay)
    shrink = commands.add_parser("shrink", help="cut a recorded scenario down to the calls a finding needs and write it as a scenario")
    bagi_qa_repro.add_arguments(shrink, shrinking=True)
    shrink.set_defaults(run=bagi_qa_repro.command_shrink)
    for name, run, text in (("report", report_module.command_report, "print the report of a finished session again"), ("drafts", report_module.command_drafts, "write the bug drafts of a finished session again, with the reproductions `shrink` wrote")):
        command = commands.add_parser(name, help=text)
        command.add_argument("session_dir", help="the out folder of the session")
        command.set_defaults(run=run)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.run(args)


if __name__ == "__main__":
    import bagi_qa  # the one copy of this module that bagi_qa_repro imports too

    sys.exit(bagi_qa.main())
