"""Scenario files: a recorded session of control calls that can be played back as a test.

Everything about the file that needs no game: the format (tools/control/CLAUDE.md, "Scenario files"), loading and
writing it, the expectations of a call and how an answer is checked against them, and what a recording writes.
Standard library only; the only I/O is load() and dump().

    header, steps = load(Path("play.jsonl"))
    failure = evaluate(steps[0].expect, Outcome(result={"version": 3}, version_step=1))
    dump(Path("play.jsonl"), header, steps)
"""

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

SCENARIO_VERSION = 1

HEADER_MEMBERS = ("scenario", "game", "flags", "title")
STEP_MEMBERS = ("method", "params", "expect", "note")
# The order in which an expect is checked, reported and written
EXPECT_MEMBERS = ("error", "result", "equal", "pointers", "versionStep", "event", "logClean")
UPDATED_MEMBERS = ("error", "result", "equal", "pointers", "versionStep")  # what --update rewrites; event and logClean stay as written
EVENT_MEMBERS = ("name", "equal", "timeout")
DEFAULT_EVENT_TIMEOUT_SECONDS = 5.0
RECORD_MINIMAL = "minimal"
RECORD_FULL = "full"
RECORD_MODES = (RECORD_MINIMAL, RECORD_FULL)

SCREENSHOT_PICTURE_KEY = "png_base64"  # the answer of `screenshot`: the picture is never written, only its size
SCREENSHOT_SIZE_KEYS = ("width", "height")
LOG_PROBLEMS_SHOWN = 5  # lines of the log a logClean failure shows
TEXT_LIMIT = 300  # a value in a failure message is cut to this many characters


class ScenarioError(Exception):
    """A scenario file that cannot be used; the text says where (file and line) and what."""

    def __init__(self, message: str, line: int | None = None, source: str | None = None):
        self.message = message
        self.line = line
        self.source = source
        place = ":".join(str(part) for part in (source, line) if part is not None)
        super().__init__(f"{place}: {message}" if place else message)

    def located(self, line: int, source: str | None) -> "ScenarioError":
        return ScenarioError(self.message, line, source)


class _Absent:
    """The value of a path that is not in the answer; not None, because null is a value."""

    def __repr__(self) -> str:
        return "<absent>"

    def __bool__(self) -> bool:
        return False


ABSENT = _Absent()


@dataclass
class Header:
    version: int
    game: str
    flags: list[str] = field(default_factory=list)
    title: str | None = None


@dataclass
class Step:
    method: str
    params: dict[str, Any] | None = None
    expect: dict[str, Any] = field(default_factory=dict)
    note: str | None = None


@dataclass
class Outcome:
    """What one call came to: the answer or the error, and what the player knows around it.

    `version_step` is the answer's version minus the one seen before (None: no version, or none before). `events` are the
    pushes the player took for this line. `log_problems` are the Warning, Error and Fatal lines of the game's log.
    """

    result: Any = None
    error_code: str | None = None
    error_message: str = ""
    version_step: int | None = None
    events: list[dict[str, Any]] = field(default_factory=list)
    log_problems: list[str] = field(default_factory=list)


@dataclass
class Failure:
    """The first expectation of a line that did not hold. `expectation` is the member name (`error`, `equal`, ...)."""

    expectation: str
    expected: str
    actual: str

    def __str__(self) -> str:
        return f"expected {self.expectation}: {self.expected}\n  actual: {self.actual}"


class VersionTracker:
    """The version step of an answer: its `version` minus the last one seen in an answer of this run."""

    def __init__(self) -> None:
        self.last: int | None = None

    def step_of(self, result: Any) -> int | None:
        version = version_of(result)
        if version is None:
            return None
        step = None if self.last is None else version - self.last
        self.last = version
        return step


def version_of(result: Any) -> int | None:
    """The `version` of an answer: an object result with an integer `version` (a bool is not one)."""
    if isinstance(result, dict):
        version = result.get("version")
        if isinstance(version, int) and not isinstance(version, bool):
            return version
    return None


def json_pointer(value: Any, pointer: str) -> Any:
    """The part of `value` that an RFC 6901 pointer names, or ABSENT; ValueError for a pointer that is not one."""
    check_pointer(pointer)
    if pointer == "":
        return value
    for token in pointer[1:].split("/"):
        token = token.replace("~1", "/").replace("~0", "~")
        if isinstance(value, dict):
            if token not in value:
                return ABSENT
            value = value[token]
        elif isinstance(value, list):
            if not token.isascii() or not token.isdigit() or (len(token) > 1 and token.startswith("0")) or int(token) >= len(value):
                return ABSENT
            value = value[int(token)]
        else:
            return ABSENT
    return value


def check_pointer(pointer: str) -> None:
    if pointer != "" and not pointer.startswith("/"):
        raise ValueError(f"'{pointer}' is not a JSON pointer (it has to be empty or start with '/')")
    for escape in pointer.split("~")[1:]:
        if not escape.startswith(("0", "1")):
            raise ValueError(f"'{pointer}' is not a JSON pointer ('~' is followed by 0 or 1)")


def same(left: Any, right: Any) -> bool:
    """JSON equality: unlike Python's ==, true is not 1."""
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(same(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(same(a, b) for a, b in zip(left, right))
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return left == right
    return type(left) is type(right) and left == right


def match_event(expect_event: dict[str, Any], event: dict[str, Any]) -> bool:
    """Whether a push (`{"event": name, "data": {...}}`) is the one an `event` expectation names: the name, and `equal` on `data`."""
    if event.get("event") != expect_event.get("name"):
        return False
    data = event.get("data")
    for key, wanted in expect_event.get("equal", {}).items():
        if not isinstance(data, dict) or key not in data or not same(data[key], wanted):
            return False
    return True


def evaluate(expect: dict[str, Any], outcome: Outcome, lenient: bool = False) -> Failure | None:
    """The first expectation of `expect` that `outcome` breaks, or None. `lenient` tolerates an error the line did not expect."""
    if "error" in expect:
        if outcome.error_code is None:
            return Failure("error", expect["error"], "no error (the call succeeded)")
        if outcome.error_code != expect["error"]:
            return Failure("error", expect["error"], _error_text(outcome))
    elif outcome.error_code is not None and not lenient:
        return Failure("error", "no error", _error_text(outcome))

    answer = ABSENT if outcome.error_code is not None else outcome.result
    if "result" in expect and not same(answer, expect["result"]):
        return Failure("result", _text(expect["result"]), _answer_text(outcome, answer))
    for key, wanted in expect.get("equal", {}).items():
        actual = answer.get(key, ABSENT) if isinstance(answer, dict) else ABSENT
        if not same(actual, wanted):
            return Failure("equal", f"{key} = {_text(wanted)}", f"{key} = {_answer_text(outcome, actual)}")
    for pointer, wanted in expect.get("pointers", {}).items():
        actual = json_pointer(answer, pointer)
        if not same(actual, wanted):
            return Failure("pointers", f"{pointer} = {_text(wanted)}", f"{pointer} = {_answer_text(outcome, actual)}")
    if "versionStep" in expect:
        if outcome.version_step is None and version_of(answer) is None:
            return Failure("versionStep", str(expect["versionStep"]), _error_text(outcome) if outcome.error_code is not None else "no version in the answer")
        if outcome.version_step is not None and outcome.version_step != expect["versionStep"]:
            return Failure("versionStep", str(expect["versionStep"]), str(outcome.version_step))
    if "event" in expect and not any(match_event(expect["event"], event) for event in outcome.events):
        return Failure("event", _text(expect["event"]), "no such push arrived" + _events_note(outcome.events))
    if expect.get("logClean") and outcome.log_problems:
        return Failure("logClean", "no Warning, Error or Fatal line in the log", "\n    ".join([f"{len(outcome.log_problems)} line(s):", *outcome.log_problems[:LOG_PROBLEMS_SHOWN]]))
    return None


def minimal_expect(outcome: Outcome) -> dict[str, Any]:
    """What a recording writes by default: the error code of a refused call, else the version step. Empty when there is nothing to say."""
    if outcome.error_code is not None:
        return {"error": outcome.error_code}
    if outcome.version_step is not None:
        return {"versionStep": outcome.version_step}
    return {}


def full_expect(outcome: Outcome) -> dict[str, Any]:
    """The minimal expectation plus the answer. A screenshot is not written: its width and height go to `equal`."""
    expect = minimal_expect(outcome)
    if outcome.error_code is None:
        expect.update(_answer_expect(outcome.result))
    return _in_order(expect)


def updated_expect(expect: dict[str, Any], outcome: Outcome) -> dict[str, Any]:
    """`expect` with the values of error, result, equal, pointers and versionStep taken from `outcome`; `event` and `logClean` are kept.

    A member that no longer applies (the call stopped failing, a path is gone) is dropped. A line without any of the
    five gets the minimal expectation.
    """
    if not any(name in expect for name in UPDATED_MEMBERS):
        return _in_order({**minimal_expect(outcome), **{name: value for name, value in expect.items() if name not in UPDATED_MEMBERS}})
    updated = {name: value for name, value in expect.items() if name not in UPDATED_MEMBERS}
    failed = outcome.error_code is not None
    if "error" in expect and failed:
        updated["error"] = outcome.error_code
    if "result" in expect and not failed:
        updated.update(_answer_expect(outcome.result))  # a screenshot becomes `equal`, see _answer_expect
    equal = dict(updated.pop("equal", {}))
    if "equal" in expect and not failed and isinstance(outcome.result, dict):
        equal.update({key: outcome.result[key] for key in expect["equal"] if key in outcome.result})
    if equal:
        updated["equal"] = equal
    if "pointers" in expect:
        answer = ABSENT if failed else outcome.result
        pointers = {pointer: found for pointer in expect["pointers"] if (found := json_pointer(answer, pointer)) is not ABSENT}
        if pointers:
            updated["pointers"] = pointers
    if "versionStep" in expect and outcome.version_step is not None:
        updated["versionStep"] = outcome.version_step
    return _in_order(updated)


def load(path: Path) -> tuple[Header, list[Step]]:
    """Reads a scenario file; ScenarioError (with the file and the line) for anything the format does not allow."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ScenarioError(f"cannot read the scenario: {error}", source=str(path)) from error
    return parse(text, str(path))


def parse(text: str, source: str | None = None) -> tuple[Header, list[Step]]:
    header: Header | None = None
    steps: list[Step] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ScenarioError(f"not JSON ({error.msg} at column {error.colno})") from error
            if not isinstance(value, dict):
                raise ScenarioError("a line has to be a JSON object")
            if header is None:
                header = _parse_header(value)
            else:
                steps.append(_parse_step(value))
        except ScenarioError as error:
            raise error.located(number, source) from error
    if header is None:
        raise ScenarioError("the file is empty: the first line has to be the header", source=source)
    return header, steps


def dump(path: Path, header: Header, steps: list[Step]) -> None:
    """Writes the scenario in its stable form: one object per line, members in a fixed order, UTF-8."""
    path.write_text(dumps(header, steps), encoding="utf-8", newline="\n")


def dumps(header: Header, steps: list[Step]) -> str:
    head: dict[str, Any] = {"scenario": header.version, "game": header.game, "flags": header.flags}
    if header.title is not None:
        head["title"] = header.title
    lines = [_line(head)]
    for step in steps:
        entry: dict[str, Any] = {"method": step.method}
        if step.params is not None:
            entry["params"] = step.params
        if step.expect:
            entry["expect"] = _in_order(step.expect)
        if step.note is not None:
            entry["note"] = step.note
        lines.append(_line(entry))
    return "\n".join(lines) + "\n"


def _line(value: dict[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False)


def _in_order(expect: dict[str, Any]) -> dict[str, Any]:
    return {name: expect[name] for name in EXPECT_MEMBERS if name in expect}


def _is_screenshot(answer: Any) -> bool:
    return isinstance(answer, dict) and SCREENSHOT_PICTURE_KEY in answer


def _answer_expect(answer: Any) -> dict[str, Any]:
    """The expectation that records an answer: `result`, but for a screenshot its size as `equal` (the picture is not kept)."""
    if _is_screenshot(answer):
        return {"equal": {key: answer[key] for key in SCREENSHOT_SIZE_KEYS if key in answer}}
    return {"result": answer}


def _text(value: Any) -> str:
    text = repr(value) if value is ABSENT else json.dumps(value, ensure_ascii=False)
    return text if len(text) <= TEXT_LIMIT else text[:TEXT_LIMIT] + "..."


def _error_text(outcome: Outcome) -> str:
    return f"{outcome.error_code}: {outcome.error_message}" if outcome.error_message else str(outcome.error_code)


def _answer_text(outcome: Outcome, value: Any) -> str:
    """The text of a part of the answer; when the call failed there is no answer, and the error says why."""
    if outcome.error_code is not None:
        return f"the call failed: {_error_text(outcome)}"
    return _text(value)


def _events_note(events: list[dict[str, Any]]) -> str:
    if not events:
        return " (none arrived)"
    return " (arrived: " + ", ".join(_text(event) for event in events[:3]) + ")"


def _parse_header(value: dict[str, Any]) -> Header:
    _only_members(value, HEADER_MEMBERS, "the header")
    version = value.get("scenario")
    if not _is_int(version):
        raise ScenarioError("the first line has to be the header with an integer \"scenario\" (the format version)")
    if version != SCENARIO_VERSION:
        raise ScenarioError(f"scenario format version {version} is not known (this tool reads version {SCENARIO_VERSION}); use a newer tool")
    game = value.get("game")
    if not isinstance(game, str) or not game:
        raise ScenarioError("the header needs \"game\", the name of the game as a string")
    flags = value.get("flags", [])
    if not isinstance(flags, list) or not all(isinstance(flag, str) for flag in flags):
        raise ScenarioError("\"flags\" has to be a list of strings")
    title = value.get("title")
    if title is not None and not isinstance(title, str):
        raise ScenarioError("\"title\" has to be a string")
    return Header(version, game, flags, title)


def _parse_step(value: dict[str, Any]) -> Step:
    _only_members(value, STEP_MEMBERS, "a call")
    method = value.get("method")
    if not isinstance(method, str) or not method:
        raise ScenarioError("a call needs \"method\", the name of the control method as a string")
    params = value.get("params")
    if "params" in value and not isinstance(params, dict):
        raise ScenarioError("\"params\" has to be an object")
    expect = value.get("expect", {})
    if not isinstance(expect, dict):
        raise ScenarioError("\"expect\" has to be an object")
    note = value.get("note")
    if note is not None and not isinstance(note, str):
        raise ScenarioError("\"note\" has to be a string")
    check_expect(expect)
    return Step(method, params, expect, note)


def check_expect(expect: dict[str, Any]) -> None:
    """ScenarioError for an unknown member or a member of the wrong type."""
    _only_members(expect, EXPECT_MEMBERS, "\"expect\"")
    if "error" in expect and not isinstance(expect["error"], str):
        raise ScenarioError("expect.error has to be a string (an error code)")
    if "equal" in expect and not isinstance(expect["equal"], dict):
        raise ScenarioError("expect.equal has to be an object")
    if "pointers" in expect:
        if not isinstance(expect["pointers"], dict):
            raise ScenarioError("expect.pointers has to be an object of JSON pointer to value")
        for pointer in expect["pointers"]:
            try:
                check_pointer(pointer)
            except ValueError as error:
                raise ScenarioError(f"expect.pointers: {error}") from error
    if "versionStep" in expect and not _is_int(expect["versionStep"]):
        raise ScenarioError("expect.versionStep has to be an integer")
    if "logClean" in expect and not isinstance(expect["logClean"], bool):
        raise ScenarioError("expect.logClean has to be true or false")
    if "event" in expect:
        _check_event(expect["event"])


def _check_event(event: Any) -> None:
    if not isinstance(event, dict):
        raise ScenarioError("expect.event has to be an object")
    _only_members(event, EVENT_MEMBERS, "expect.event")
    if not isinstance(event.get("name"), str) or not event["name"]:
        raise ScenarioError("expect.event needs \"name\", the name of the push as a string")
    if "equal" in event and not isinstance(event["equal"], dict):
        raise ScenarioError("expect.event.equal has to be an object")
    timeout = event.get("timeout", DEFAULT_EVENT_TIMEOUT_SECONDS)
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or timeout < 0:
        raise ScenarioError("expect.event.timeout has to be a number of seconds, 0 or more")


def _only_members(value: dict[str, Any], known: tuple[str, ...], where: str) -> None:
    for name in value:
        if name not in known:
            raise ScenarioError(f"unknown member \"{name}\" in {where} (known: {', '.join(known)})")


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)
