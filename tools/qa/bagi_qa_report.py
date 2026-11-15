"""The report of a QA session and the bug drafts: text built from what a session found. The library never calls the todo store; filing is the skill's step.

    report = build_report(session)
    write_report(report, out_dir)                     # report.md, report.json
    write_drafts(make_drafts(report, out_dir), out_dir)  # bugs/<id>.md, bugs/<id>-repro.md

A draft is the body of an entry in the `todo-add` form and the text of a markdown plan that holds the reproduction (the store keeps markdown
plans). Nothing printed names a home folder or the user: paths are relative to the out folder and the rest is scrubbed (`scrub`).
Standard library only.
"""

import getpass
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Sequence

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

import bagi_qa_checks as checks  # noqa: E402
import scenario  # noqa: E402
from bagi_qa_checks import Finding  # noqa: E402

REPORT_MD = "report.md"
REPORT_JSON = "report.json"
DRAFTS_FOLDER = "bugs"
REPRO_FOLDER = "repro"  # the scenarios `bagi_qa.py shrink` writes by default, one per finding name
REPRO_PLAN_SUFFIX = "-repro"
REPORT_VERSION = 1

ID_PREFIX = "bug-"
ID_LIMIT = 40  # characters of an id: the store's ids are short
ID_WORD = re.compile(r"[a-z]{3,}")
TITLE_LIMIT = 100
LOG_PREFIX = re.compile(r"^(\[[^\]]*\]\s*)+")  # "[Warning] [12:00:00.000] text": the level and the time say nothing in a title
STEPS_SHOWN = 30  # calls of the reproduction listed in words
REPRO_FENCE = "jsonl"
REPRO_BLOCK = re.compile(r"^```" + REPRO_FENCE + r"\n(.*?)^```", re.DOTALL | re.MULTILINE)
MIN_USER_LENGTH = 4  # a shorter login name would mangle ordinary words when it is scrubbed
HOME_MARK = "~"
OUT_MARK = "<out>"
USER_MARK = "<user>"

EXPECTED = {
    checks.KIND_LOG: "no Warning, Error or Fatal line in the game's log",
    checks.KIND_UI_ISSUE: "every canvas laid out and without a layout issue",
    checks.KIND_VERSION_BACK: "the version of the state never goes back",
    checks.KIND_ACTION_REFUSED: "an action that `list_actions` offers is accepted",
    checks.KIND_INVARIANT: "the game's invariant holds",
}


@dataclass
class Report:
    """What a session came to. `bugs` and `known` are the findings split by the game's `KNOWN_DEVIATIONS`; paths are file names relative to `out_dir`."""

    charter: str
    game: str
    flags: list[str]
    calls: int
    steps: int
    actions: int
    refused: int
    notes: list[str] = field(default_factory=list)
    shots: list[tuple[str, str]] = field(default_factory=list)
    bugs: list[Finding] = field(default_factory=list)
    known: list[Finding] = field(default_factory=list)
    scenario: str = ""
    invariants: str = ""
    session_errors: list[str] = field(default_factory=list)
    out_dir: Path | None = None  # for the scrub only; not in report.json

    def to_dict(self) -> dict[str, Any]:
        return {
            "version": REPORT_VERSION,
            "charter": self.charter,
            "game": self.game,
            "flags": self.flags,
            "calls": self.calls,
            "steps": self.steps,
            "actions": self.actions,
            "refused": self.refused,
            "notes": self.notes,
            "shots": [{"label": label, "path": path} for label, path in self.shots],
            "bugs": [finding.to_dict() for finding in self.bugs],
            "known": [finding.to_dict() for finding in self.known],
            "scenario": self.scenario,
            "invariants": self.invariants,
            "session_errors": self.session_errors,
        }

    @staticmethod
    def from_dict(value: dict[str, Any], out_dir: Path | None = None) -> "Report":
        return Report(
            value["charter"],
            value["game"],
            list(value["flags"]),
            value["calls"],
            value["steps"],
            value["actions"],
            value["refused"],
            list(value["notes"]),
            [(shot["label"], shot["path"]) for shot in value["shots"]],
            [Finding.from_dict(item) for item in value["bugs"]],
            [Finding.from_dict(item) for item in value["known"]],
            value["scenario"],
            value["invariants"],
            list(value["session_errors"]),
            out_dir,
        )


@dataclass
class BugDraft:
    """An entry and its reproduction, as text: `id` is `bug-<short>`, `body` the entry body without a title line, `repro_name` the bare name of the plan."""

    id: str
    title: str
    body: str
    repro_name: str
    repro_markdown: str
    finding: Finding | None = None


def build_report(session: Any) -> Report:
    """The report of a `QaSession` (finished or not). The calls the findings showed after are named from the recording when it has been written."""
    out = session.out_dir
    steps = _recorded_steps(session.scenario_path)

    def described(findings: Sequence[Finding]) -> list[Finding]:
        return [_with_call(finding, steps) for finding in findings]

    return Report(
        session.charter,
        session.game or "unknown",
        list(session.flags),
        session.calls,
        session.steps,
        session.actions,
        session.refused,
        list(session.notes),
        [(label, _relative(path, out)) for label, path in session.shots],
        described(session.bugs),
        described(session.known),
        session.scenario_path.name,
        Path(session.invariants.path).name if session.invariants is not None else "",
        list(session.session_errors),
        out,
    )


def groups(findings: Sequence[Finding]) -> list[list[Finding]]:
    """The findings by `name`, in the order the names first showed: one name is one bug."""
    by_name: dict[str, list[Finding]] = {}
    for finding in findings:
        by_name.setdefault(finding.name, []).append(finding)
    return list(by_name.values())


def occurrences(group: Sequence[Finding]) -> int:
    """How many times a bug showed: each finding once, and the repeats the session counted on it."""
    return sum(1 + int(finding.details.get("repeats", 0)) for finding in group)


def render_markdown(report: Report) -> str:
    lines = [f"# QA report: {_one_line(report.charter)}", ""]
    lines.append(f"- Game: {report.game}; flags: {' '.join(report.flags) or 'none'}")
    lines.append(f"- Calls: {report.calls} ({report.steps} steps, {report.actions} actions, {report.refused} refused); recording: `{report.scenario}`")
    if report.invariants:
        lines.append(f"- Invariants: `{report.invariants}`")
    bug_groups, known_groups = groups(report.bugs), groups(report.known)
    verdict = f"{len(bug_groups)} bug(s), {len(known_groups)} known deviation(s)" if report.bugs or report.known else "no findings"
    lines += [f"- Result: {verdict}" + (f", {len(report.session_errors)} session error(s)" if report.session_errors else ""), ""]
    lines += ["## Covered", ""] + ([f"- {note}" for note in report.notes] or ["- nothing was noted"]) + [""]
    if report.shots:
        lines += ["## Screenshots", ""] + [f"- {label}: `{path}`" for label, path in report.shots] + [""]
    if bug_groups:
        lines += ["## Bugs", ""] + [line for group in bug_groups for line in _group_lines(group)]
    if known_groups:
        lines += ["## Known deviations", ""] + [line for group in known_groups for line in _group_lines(group)]
    if report.session_errors:
        lines += ["## Session errors", ""] + [f"- {error}" for error in report.session_errors] + [""]
    return scrub("\n".join(lines).rstrip("\n") + "\n", report.out_dir)


def render_json(report: Report) -> str:
    return scrub(json.dumps(report.to_dict(), indent=2, ensure_ascii=False) + "\n", report.out_dir)


def write_report(report: Report, out: str | Path) -> tuple[Path, Path]:
    """Writes `report.md` and `report.json` into `out`; their paths."""
    folder = Path(out)
    folder.mkdir(parents=True, exist_ok=True)
    markdown, data = folder / REPORT_MD, folder / REPORT_JSON
    markdown.write_text(render_markdown(report), encoding="utf-8", newline="\n")
    data.write_text(render_json(report), encoding="utf-8", newline="\n")
    return markdown, data


def load_report(out: str | Path) -> Report:
    """The report of a session folder, from its `report.json`. ValueError when the folder has none or it is not a report."""
    folder = Path(out)
    try:
        value = json.loads((folder / REPORT_JSON).read_text(encoding="utf-8"))
        if value.get("version") != REPORT_VERSION:
            raise ValueError(f"{REPORT_JSON} is of version {value.get('version')}, this tool reads {REPORT_VERSION}")
        return Report.from_dict(value, folder.resolve())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        raise ValueError(f"{folder} holds no readable {REPORT_JSON}: {error}") from error


def repro_scenario_path(out: str | Path, name: str) -> Path:
    """Where `bagi_qa.py shrink` writes the reproduction of the finding `name` by default; the drafts of that folder use the file when it is there."""
    return Path(out) / REPRO_FOLDER / f"{_slug(name, '_.') or 'finding'}.jsonl"


def make_drafts(report: Report, out: str | Path) -> list[BugDraft]:
    """One draft per bug name; the reproduction is the shrunk scenario of that name in `out` when `bagi_qa.py shrink` wrote it, else the recording as far as the finding."""
    taken: set[str] = set()
    drafts = []
    for group in groups(report.bugs):
        shrunk = repro_scenario_path(out, group[0].name)
        draft = draft_bug(group, report, shrunk if shrunk.is_file() else None, Path(out), taken)
        taken.add(draft.id)
        drafts.append(draft)
    return drafts


def draft_bug(finding: Finding | Sequence[Finding], report: Report, repro_path: str | Path | None, out: Path | None = None, taken: set[str] | None = None) -> BugDraft:
    """The entry and the reproduction plan of a bug. `finding` is one finding, or all those of one name (the first is the one named, the count is of all).

    `repro_path` is the shrunk scenario (None: the recording of the session up to the finding, which `out` or the report's folder holds).
    `taken` are ids already used in this run: the id gets `-2`, `-3` ... to stay unique.
    """
    group = [finding] if isinstance(finding, Finding) else list(finding)
    first = group[0]
    folder = out if out is not None else report.out_dir
    identifier = _unique(_bug_id(first), taken or set())
    repro_name = identifier + REPRO_PLAN_SUFFIX + ".md"
    cut, text = _repro_scenario(first, report, repro_path, folder)
    body = _body(group, report, identifier, repro_name, cut, text)
    markdown = _repro_markdown(first, identifier, cut, text, report)
    return BugDraft(identifier, scrub(_title(first), folder), scrub(body, folder), repro_name, scrub(markdown, folder), first)


def write_drafts(drafts: Sequence[BugDraft], out: str | Path) -> list[Path]:
    """Writes `bugs/<id>.md` (the body) and `bugs/<id>-repro.md` (the plan text) into `out`; the paths of the bodies."""
    folder = Path(out) / DRAFTS_FOLDER
    paths: list[Path] = []
    if not drafts:
        return paths
    folder.mkdir(parents=True, exist_ok=True)
    for draft in drafts:
        paths.append(folder / f"{draft.id}.md")
        paths[-1].write_text(draft.body, encoding="utf-8", newline="\n")
        (folder / draft.repro_name).write_text(draft.repro_markdown, encoding="utf-8", newline="\n")
    return paths


def read_repro_markdown(text: str) -> tuple[scenario.Header, list[scenario.Step]]:
    """The scenario in the `jsonl` block of a reproduction plan. ValueError when there is none; ScenarioError when it is not a scenario."""
    found = REPRO_BLOCK.search(text)
    if found is None:
        raise ValueError(f"no ```{REPRO_FENCE} block in the reproduction text")
    return scenario.parse(found.group(1), "the reproduction text")


def scrub(text: str, out_dir: Path | None = None) -> str:
    """`text` without the folder of the out directory, the home folder and the login name: what a report prints is pasted into a store and a chat."""
    replacements: list[tuple[str, str]] = []
    if out_dir is not None:
        replacements += [(str(out_dir), OUT_MARK), (str(out_dir.resolve()), OUT_MARK)]
    replacements.append((str(Path.home()), HOME_MARK))
    replacements.append((str(Path.home().resolve()), HOME_MARK))
    for old, new in sorted(set(replacements), key=lambda pair: -len(pair[0])):
        if old and old not in ("/", "\\"):
            text = text.replace(old, new)
    try:
        user = getpass.getuser()
    except (KeyError, OSError):
        user = ""
    if len(user) >= MIN_USER_LENGTH:
        text = re.sub(r"(?<![A-Za-z0-9_])" + re.escape(user) + r"(?![A-Za-z0-9_])", USER_MARK, text)
    return text


def command_report(args: Any) -> int:
    """`bagi_qa.py report SESSION_DIR`: prints the report of a finished session again. Exit 0, or 2 when the folder holds no report."""
    try:
        report = load_report(args.session_dir)
    except ValueError as error:
        print(f"ERROR {error}")
        return 2
    sys.stdout.write(render_markdown(report))
    return 0


def command_drafts(args: Any) -> int:
    """`bagi_qa.py drafts SESSION_DIR`: writes the bug drafts again, taking the shrunk scenarios that `shrink` wrote. Exit 0, or 2 when the folder holds no report."""
    try:
        report = load_report(args.session_dir)
    except ValueError as error:
        print(f"ERROR {error}")
        return 2
    drafts = make_drafts(report, args.session_dir)
    for path, draft in zip(write_drafts(drafts, args.session_dir), drafts):
        print(f"{path.relative_to(Path(args.session_dir))}  {draft.title}")
    print(f"{len(drafts)} draft(s)")
    return 0


def _recorded_steps(path: Path) -> list[scenario.Step]:
    try:
        return scenario.load(path)[1]
    except scenario.ScenarioError:
        return []  # not written yet (a report of an unfinished session) or a lost game: the findings are named without their call


def _with_call(finding: Finding, steps: Sequence[scenario.Step]) -> Finding:
    if 1 <= finding.call_index <= len(steps):
        details = dict(finding.details)
        details["call"] = _describe(steps[finding.call_index - 1])
        return Finding(finding.kind, finding.name, finding.message, finding.call_index, details, finding.expect, list(finding.shots))
    return finding


def _relative(path: Path, out: Path) -> str:
    try:
        return path.resolve().relative_to(out.resolve()).as_posix()
    except ValueError:
        return path.name


def _describe(step: scenario.Step) -> str:
    return step.method if step.params is None else f"{step.method} {json.dumps(step.params, ensure_ascii=False, sort_keys=True)}"


def _one_line(text: str) -> str:
    return " ".join(text.split())


def _group_lines(group: Sequence[Finding]) -> list[str]:
    first = group[0]
    where = f"after call {first.call_index}" + (f" (`{first.details['call']}`)" if "call" in first.details else "") if first.call_index else "no call"
    lines = [f"### {first.name} ({first.kind}), {occurrences(group)} time(s), first {where}", ""]
    lines += [f"- {_one_line(finding.message)}" for finding in group] + [""]
    return lines


def _slug(text: str, keep: str = "") -> str:
    return re.sub(r"[^a-z0-9" + re.escape(keep) + r"]+", "-", text.lower()).strip("-")


def _bug_id(finding: Finding) -> str:
    """`bug-` + the words of the name + the first words of the message (letters only: numbers change from run to run), at most about ID_LIMIT characters."""
    name_slug = _slug(finding.name.replace(".", "-").replace("_", "-"))
    parts = [name_slug] if name_slug else []
    seen = set(name_slug.split("-"))
    length = len(ID_PREFIX) + len(name_slug)
    for word in ID_WORD.findall(LOG_PREFIX.sub("", finding.message).lower()):
        if word in seen or length + 1 + len(word) > ID_LIMIT:
            continue
        parts.append(word)
        seen.add(word)
        length += 1 + len(word)
    return (ID_PREFIX + "-".join(parts))[:ID_LIMIT].rstrip("-") if parts else ID_PREFIX + "finding"


def _unique(identifier: str, taken: set[str]) -> str:
    candidate, number = identifier, 1
    while candidate in taken:
        number += 1
        suffix = f"-{number}"
        candidate = identifier[: ID_LIMIT - len(suffix)] + suffix
    return candidate


def _title(finding: Finding) -> str:
    summary = LOG_PREFIX.sub("", _one_line(finding.message)) or finding.name
    text = f"{summary} ({finding.name})"
    return text if len(text) <= TITLE_LIMIT else summary[: TITLE_LIMIT - len(finding.name) - 6].rstrip() + f"... ({finding.name})"


def _repro_scenario(finding: Finding, report: Report, repro_path: str | Path | None, folder: Path | None) -> tuple[bool, str]:
    """(cut down, the scenario text): the shrunk file, or the recording up to the finding's call (header and the first `call_index` calls)."""
    if repro_path is not None:
        return True, Path(repro_path).read_text(encoding="utf-8")
    if folder is None or not report.scenario:
        return False, ""
    try:
        header, steps = scenario.load(folder / report.scenario)
    except scenario.ScenarioError:
        return False, ""
    return False, scenario.dumps(header, steps[: finding.call_index] if finding.call_index else steps)


def _body(group: Sequence[Finding], report: Report, identifier: str, repro_name: str, cut: bool, text: str) -> str:
    finding = group[0]
    call = f", after call {finding.call_index}" + (f" (`{finding.details['call']}`)" if "call" in finding.details else "") if finding.call_index else ""
    canvas = f"; canvas `{finding.details['canvas']}`" if "canvas" in finding.details else ""
    count = occurrences(group)
    lines = [
        f"**Where:** the game `{report.game}`, found by a QA session (charter: {_one_line(report.charter)}){call}{canvas}.",
        "",
        f"**What is wrong:** expected {EXPECTED.get(finding.kind, 'the check to hold')}; actual: {_one_line(finding.message)}"
        + (f" It showed {count} times in the session, the first after call {finding.call_index}." if count > 1 else ""),
        "",
        "**Steps:**",
        *_steps_in_words(text, cut),
        "",
        f"**Reproduction:** the plan `{repro_name}` ({_how(finding, cut)}).",
    ]
    return "\n".join(lines) + "\n"


def _how(finding: Finding, cut: bool) -> str:
    if not cut:
        return "the recording of the session up to the finding: play it with `bagi_qa.py replay`, and cut it down with `bagi_qa.py shrink`"
    if finding.expect is None:
        return "play it with `bagi_qa.py replay`: it has no expectation of its own, so `bagi_play.py` passes it"
    return "play it with `bagi_play.py`: it fails on its last line; `bagi_qa.py replay` shows the finding too"


def _steps_in_words(text: str, cut: bool) -> list[str]:
    if not text:
        return ["(the recording of the session was not available)"]
    try:
        _, steps = scenario.parse(text)
    except scenario.ScenarioError:
        return ["(the scenario could not be read)"]
    lines = [f"{number}. `{_describe(step)}`" for number, step in enumerate(steps[:STEPS_SHOWN], start=1)]
    if len(steps) > STEPS_SHOWN:
        lines.append(f"... and {len(steps) - STEPS_SHOWN} more call(s)")
    if not cut:
        lines.append("(the calls of the session up to the finding, not cut down yet)")
    return lines


def _repro_markdown(finding: Finding, identifier: str, cut: bool, text: str, report: Report) -> str:
    name = identifier + REPRO_PLAN_SUFFIX
    how = _how(finding, cut)
    lines = [
        f"# Reproduction of {identifier}",
        "",
        f"The finding `{finding.name}` of the game `{report.game}` shows when the scenario below is played"
        + f". Save the block as `{name}.jsonl`; `<engine>` is the engine folder (`engine/` in a game) and `<executable>` the built game.",
        "",
        "```",
        f"python3 <engine>/tools/qa/bagi_qa.py replay --exe <executable> [--invariants <the game's invariants module>] {name}.jsonl",
        *([f"python3 <engine>/tools/control/bagi_play.py <executable> {name}.jsonl"] if cut and finding.expect is not None else []),
        "```",
        "",
        how[0].upper() + how[1:] + ".",
        "",
        f"```{REPRO_FENCE}",
        text.rstrip("\n"),
        "```",
    ]
    return "\n".join(lines) + "\n"
