"""The findings of a QA session and the checks that make them: pure functions over control answers, with no process and no I/O.

Only `load_invariants` reads a file (the game's invariants module, `tools/qa/CLAUDE.md`). Standard library only.

    findings = check_ui_tree(session.call("ui_tree")) + check_version(previous_version, version_of(answer))
    bugs, known = apply_known(findings, invariants.known_deviations)
"""

import dataclasses
import json
import sys
import traceback
import types
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterable, NamedTuple, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "control"))

import scenario  # noqa: E402

KIND_LOG = "log"
KIND_UI_ISSUE = "ui_issue"
KIND_VERSION_BACK = "version_back"
KIND_ACTION_REFUSED = "action_refused"
KIND_INVARIANT = "invariant"
KINDS = (KIND_LOG, KIND_UI_ISSUE, KIND_VERSION_BACK, KIND_ACTION_REFUSED, KIND_INVARIANT)

NAME_LOG_PREFIX = "generic.log_"  # + the level in lower case: generic.log_warning
NAME_UI_NOT_LAID_OUT = "generic.ui_not_laid_out"
NAME_UI_ISSUES = "generic.ui_issues"
NAME_VERSION_BACK = "generic.version_back"
NAME_ACTION_REFUSED = "generic.action_refused"
NAME_MODULE_ERROR = "qa.module_error"

LOG_LEVEL_END = "]"  # a problem line starts with "[Warning]", "[Error]" or "[Fatal]" (game_process.LOG_PROBLEM_PATTERN)
UNKNOWN_LEVEL = "problem"
TRACEBACK_LINES = 6  # the end of a module's traceback that a module_error finding keeps in its details

STATE_CHECK = "check_state"
ACTION_CHECK = "check_action"
MONKEY_MOVES = "monkey_moves"
PERFORM_ACTION_METHOD = "perform_action"  # the control methods every QA module sends or reads the answer of
STEP_METHOD = "step"
UI_CLICK_METHOD = "ui_click"
SET_WINDOW_METHOD = "set_window"
UI_TREE_METHOD = "ui_tree"
KNOWN_MEMBER = "KNOWN_DEVIATIONS"
EDITOR_FILES_MEMBER = "EDITOR_CONTENT_FILES"
QUIT_METHOD = "quit"  # the call that ends a session is not a line of its recording


class InvariantsError(Exception):
    """A game's invariants module that cannot be used at all: missing, not Python that imports, or a member of the wrong shape."""


class Violation(NamedTuple):
    """What a game's check reports: the stable name of the broken rule and a message with the numbers."""

    name: str
    message: str


class Move(NamedTuple):
    """One thing the monkey may do next: `kind` is its class (`action`, `click`, `step`, `reset`, `window`), `method` and `params` the control call, `label` a short text for notes."""

    kind: str
    method: str
    params: dict[str, Any]
    label: str = ""


FLAGGED_KEY = "flagged"  # details of a finding the charter script raised itself (`QaSession.flag`): no replay shows it


@dataclass
class Finding:
    """One thing a session found.

    `name` is the stable key a reproduction is matched by. `call_index` is the 1-based number of the recorded call after which it showed
    (0 until the session sets it). `expect` is the scenario expectation that would have caught it on that line: None when the format
    cannot say it, `{}` when a plain line already fails (a call with no `error` expectation that the game refused). `shots` are file names.
    """

    kind: str
    name: str
    message: str
    call_index: int = 0
    details: dict[str, Any] = field(default_factory=dict)
    expect: dict[str, Any] | None = None
    shots: list[str] = field(default_factory=list)

    def at(self, call_index: int) -> "Finding":
        return dataclasses.replace(self, call_index=call_index)

    def to_dict(self) -> dict[str, Any]:
        return dataclasses.asdict(self)

    @staticmethod
    def from_dict(value: dict[str, Any]) -> "Finding":
        return Finding(
            value["kind"],
            value["name"],
            value["message"],
            value.get("call_index", 0),
            dict(value.get("details", {})),
            value.get("expect"),
            list(value.get("shots", [])),
        )


@dataclass
class Invariants:
    """A game's invariants module: its optional checks and the names that are not bugs. `path` is only for messages."""

    path: str
    check_state: Callable[..., Any] | None = None
    check_action: Callable[..., Any] | None = None
    monkey_moves: Callable[..., Any] | None = None
    known_deviations: frozenset[str] = frozenset()
    editor_content_files: tuple[str, ...] = ()  # the files the editor's Save writes, relative to the folder holding `.claude/rules/qa-project.md`


def check_log(problem_lines: Sequence[str], seen: int) -> list[Finding]:
    """One finding per log line after the first `seen` of `problem_lines` (what `GameSession.log_problems()` gives: the log only grows).

    The caller keeps `seen = len(problem_lines)` for the next call. The expectation is `logClean`, which holds for the whole log so far.
    """
    findings = []
    for line in problem_lines[seen:]:
        level = line[1 : line.find(LOG_LEVEL_END)].lower() if line.startswith("[") and LOG_LEVEL_END in line else UNKNOWN_LEVEL
        findings.append(Finding(KIND_LOG, NAME_LOG_PREFIX + level, line, details={"line": line}, expect={"logClean": True}))
    return findings


def check_ui_tree(tree: Any) -> list[Finding]:
    """One finding per canvas of a `ui_tree` answer that is not laid out or has validator issues; the issue text is in the message.

    A tree of another shape is not a finding here: the control answers are the game's contract, and a missing member would only hide the rest.
    """
    canvases = tree.get("canvases") if isinstance(tree, dict) else None
    if not isinstance(canvases, list):
        return []
    findings = []
    for index, canvas in enumerate(canvases):
        if not isinstance(canvas, dict):
            continue
        node = canvas.get("node", f"#{index}")
        if not canvas.get("laidOut"):
            findings.append(
                Finding(
                    KIND_UI_ISSUE,
                    NAME_UI_NOT_LAID_OUT,
                    f"the canvas '{node}' is not laid out",
                    details={"canvas": node},
                    expect={"pointers": {f"/canvases/{index}/laidOut": True}},
                )
            )
        issues = canvas.get("issues")
        if isinstance(issues, list) and issues:
            findings.append(
                Finding(
                    KIND_UI_ISSUE,
                    NAME_UI_ISSUES,
                    f"the canvas '{node}' has layout issues: " + "; ".join(_issue_text(issue) for issue in issues),
                    details={"canvas": node, "issues": issues},
                    expect={"pointers": {f"/canvases/{index}/issues": []}},
                )
            )
    return findings


def check_version(previous: int | None, current: int | None) -> list[Finding]:
    """A finding when the version went back. Equal is fine (an action that changes nothing); either one None (no version seen) is not checked.

    `versionStep` of the scenario format is exact, not a bound, so the finding has no expectation.
    """
    if previous is None or current is None or current >= previous:
        return []
    return [Finding(KIND_VERSION_BACK, NAME_VERSION_BACK, f"the version went back from {previous} to {current}", details={"previous": previous, "current": current})]


def check_action_accepted(action: dict[str, Any], listed: Sequence[dict[str, Any]], error_code: str | None) -> list[Finding]:
    """A finding when the game refused (`error_code`) an action that `list_actions` had offered. A refusal of an action nobody listed is not one.

    Any code counts, not only `illegal_action`: a listed action that is `invalid_params` is as wrong.
    """
    if error_code is None or not any(scenario.same(action, offered) for offered in listed):
        return []
    return [
        Finding(
            KIND_ACTION_REFUSED,
            NAME_ACTION_REFUSED,
            f"the listed action {_text(action)} was refused: {error_code}",
            details={"action": action, "error": error_code},
            expect={},
        )
    ]


def apply_known(findings: Iterable[Finding], known: Iterable[str]) -> tuple[list[Finding], list[Finding]]:
    """Splits `findings` into (bugs, known deviations) by name; the order is kept."""
    known_names = frozenset(known)
    bugs: list[Finding] = []
    deviations: list[Finding] = []
    for finding in findings:
        (deviations if finding.name in known_names else bugs).append(finding)
    return bugs, deviations


def load_invariants(path: str | Path) -> Invariants:
    """Loads the invariants module at `path` by its path, never through `sys.path` (a game's `tests/qa/invariants.py` must not shadow a module).

    Every member is optional. InvariantsError for a file that cannot be read or run, or a member of the wrong shape. The source is run
    without a byte-code cache, so nothing is written next to the file.
    """
    file = Path(path).expanduser()
    try:
        source = file.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise InvariantsError(f"cannot read the invariants module {file}: {error}") from error
    module = types.ModuleType(f"bagi_qa_invariants_{file.stem}")
    module.__file__ = str(file)
    sys.modules[module.__name__] = module  # a dataclass in the module looks its own module up here
    try:
        exec(compile(source, str(file), "exec"), module.__dict__)
    except Exception as error:  # noqa: BLE001  (a module can raise anything at import)
        del sys.modules[module.__name__]
        raise InvariantsError(f"the invariants module {file} does not load: {type(error).__name__}: {error}") from error
    return Invariants(
        str(file),
        _callable_member(module, STATE_CHECK, file),
        _callable_member(module, ACTION_CHECK, file),
        _callable_member(module, MONKEY_MOVES, file),
        _known_member(module, file),
        _editor_files_member(module, file),
    )


def run_state_checks(module: Invariants | None, answer: dict[str, Any], previous: dict[str, Any] | None) -> list[Finding]:
    """The game's `check_state(answer, previous)` as findings; no module or no such function: none. A raising module is a `qa.module_error` finding."""
    if module is None or module.check_state is None:
        return []
    return _run(module, STATE_CHECK, module.check_state, answer, previous)


def run_action_checks(module: Invariants | None, action: dict[str, Any], result: Any, before: dict[str, Any] | None, after: dict[str, Any] | None) -> list[Finding]:
    """The game's `check_action(action, result, before, after)` as findings, with the same rules as `run_state_checks`.

    `result` is the answer of the action (None when it was refused), `before` and `after` the full `get_state` answers around it.
    """
    if module is None or module.check_action is None:
        return []
    return _run(module, ACTION_CHECK, module.check_action, action, result, before, after)


def run_monkey_moves(module: Invariants | None, answer: dict[str, Any], moves: list[Move]) -> tuple[list[Move], list[Finding]]:
    """The game's `monkey_moves(answer, moves)`: the list the monkey picks from, and the findings of the call.

    `answer` is the full `get_state` result, `moves` the generic candidates. No module or no such function: `moves` unchanged. A module that raises
    or returns anything but a list of `Move` leaves `moves` as they were and reports a `qa.module_error` finding, as the checks do.
    """
    if module is None or module.monkey_moves is None:
        return moves, []
    try:
        returned = module.monkey_moves(answer, list(moves))
        if not isinstance(returned, list) or not all(isinstance(move, Move) for move in returned):
            raise TypeError(f"{MONKEY_MOVES} has to return a list of Move, not {_describe(returned)}")
    except Exception as error:  # noqa: BLE001  (the game's code is not ours to trust)
        return moves, [_module_error(module, MONKEY_MOVES, error)]
    return returned, []


def _run(module: Invariants, function_name: str, function: Callable[..., Any], *arguments: Any) -> list[Finding]:
    try:
        returned = function(*arguments)
        violations = [Violation(*_pair(item)) for item in (returned or [])]
    except Exception as error:  # noqa: BLE001  (the game's code is not ours to trust)
        return [_module_error(module, function_name, error)]
    return [Finding(KIND_INVARIANT, violation.name, violation.message) for violation in violations]


def _module_error(module: Invariants, function_name: str, error: Exception) -> Finding:
    """The finding for a module function that raised: called from the `except` of the call, so the traceback is the error's."""
    tail = traceback.format_exc().strip().splitlines()[-TRACEBACK_LINES:]
    return Finding(
        KIND_INVARIANT,
        NAME_MODULE_ERROR,
        f"{function_name} of {module.path} raised {type(error).__name__}: {error}",
        details={"function": function_name, "traceback": tail},
    )


def _describe(value: Any) -> str:
    return "a list with an item that is not a Move" if isinstance(value, list) else type(value).__name__


def _pair(item: Any) -> tuple[str, str]:
    name, message = item  # a Violation or any pair; anything else raises and becomes a module_error
    return str(name), str(message)


def _callable_member(module: types.ModuleType, name: str, file: Path) -> Callable[..., Any] | None:
    member = getattr(module, name, None)
    if member is not None and not callable(member):
        raise InvariantsError(f"{name} of {file} has to be a function")
    return member


def _known_member(module: types.ModuleType, file: Path) -> frozenset[str]:
    member = getattr(module, KNOWN_MEMBER, ())
    if isinstance(member, (str, bytes)) or not isinstance(member, (set, frozenset, list, tuple)) or not all(isinstance(name, str) for name in member):
        raise InvariantsError(f"{KNOWN_MEMBER} of {file} has to be a set of names (strings)")
    return frozenset(member)


def _editor_files_member(module: types.ModuleType, file: Path) -> tuple[str, ...]:
    member = getattr(module, EDITOR_FILES_MEMBER, ())
    if isinstance(member, (str, bytes)) or not isinstance(member, (list, tuple)) or not all(isinstance(name, str) for name in member):
        raise InvariantsError(f"{EDITOR_FILES_MEMBER} of {file} has to be a list of paths (strings)")
    return tuple(member)


def _issue_text(issue: Any) -> str:
    """A validator issue (`{"kind", "element", "other", "rect"}`) in one phrase."""
    if not isinstance(issue, dict):
        return _text(issue)
    text = str(issue.get("kind", "issue"))
    if issue.get("element"):
        text += f" on '{issue['element']}'"
    if issue.get("other"):
        text += f" (with '{issue['other']}')"
    return text


def _text(value: Any) -> str:
    return json.dumps(value, sort_keys=True)
