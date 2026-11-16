"""Is this reproduction a bug the store already has? The signature of a reproduction and the rule that compares two.

    known = signature(plan_text)                      # `bug-<short>-repro.md` of the store, or a `.jsonl` scenario
    fresh = signature_of_file(Path("repro/generic.log_warning.jsonl"))
    similar(fresh, known)                             # True: the same finding reached the same way

A signature is cheap and deliberately blunt: the finding's name and the shape of the calls that led to it (the methods and the one parameter
that says which thing was acted on), so a walk that took another seed to the same defect, with other counts or a few more steps in between,
still matches. It never reads the todo store: the skill fetches the plans. Standard library only.
"""

import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import NamedTuple

QA_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(QA_DIR.parent / "control"))
sys.path.insert(0, str(QA_DIR))

import bagi_qa_report as report_module  # noqa: E402
import scenario  # noqa: E402
from bagi_qa_checks import PERFORM_ACTION_METHOD, SET_WINDOW_METHOD, UI_CLICK_METHOD  # noqa: E402

# Two reproductions of one finding that share this part of their calls (the longer list is the divisor, the calls are a multiset) are taken for one
# bug: a majority, so one call off in a short cut reproduction (another count, an extra step) still matches and a half of it does not.
SIMILAR_SHARE = 0.6

_NOTE_NAME = re.compile(r"^QA finding (\S+?):")
_PLAN_NAME = re.compile(r"The finding `([^`]+)`")


class Signature(NamedTuple):
    """`finding` is the finding name (None when the text does not say), `calls` one string per call (`perform_action:<type>`, `ui_click:<name>`,
    `set_window:<w>x<h>`, the bare method for the rest), `last` the canonical JSON of the last call's method and params ("" for no call).
    """

    finding: str | None
    calls: tuple[str, ...]
    last: str


def signature(text: str) -> Signature:
    """The signature of a scenario (`.jsonl`) or of a reproduction plan (the `jsonl` block of `bug-<short>-repro.md`); ValueError for anything else."""
    if report_module.REPRO_BLOCK.search(text):
        try:
            _, steps = report_module.read_repro_markdown(text)
        except scenario.ScenarioError as error:
            raise ValueError(f"the scenario in the plan is not valid: {error}") from error
        found = _PLAN_NAME.search(text)
        return _of_steps(found.group(1) if found else _name_of_note(steps), steps)
    try:
        _, steps = scenario.parse(text)
    except scenario.ScenarioError as error:
        raise ValueError(f"neither a scenario nor a reproduction plan: {error}") from error
    return _of_steps(_name_of_note(steps), steps)


def signature_of_file(path: str | Path) -> Signature:
    """`signature` of a file; ValueError also for a file that cannot be read."""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ValueError(f"cannot read {path}: {error}") from error
    return signature(text)


def similar(first: Signature, second: Signature, threshold: float = SIMILAR_SHARE) -> bool:
    """The same finding name on both, and either most of the calls in common (`threshold` of the longer list, as a multiset) or the same last call.

    A signature with no finding name is never similar to anything: without the name the calls say nothing about the defect.
    """
    if first.finding is None or first.finding != second.finding:
        return False
    if first.last and first.last == second.last:
        return True
    longest = max(len(first.calls), len(second.calls))
    if longest == 0:
        return False
    common = sum((Counter(first.calls) & Counter(second.calls)).values())
    return common / longest >= threshold


def _of_steps(finding: str | None, steps: list[scenario.Step]) -> Signature:
    return Signature(finding, tuple(_call(step) for step in steps), _canonical(steps[-1]) if steps else "")


def _name_of_note(steps: list[scenario.Step]) -> str | None:
    found = _NOTE_NAME.match(steps[-1].note or "") if steps else None
    return found.group(1) if found else None


def _call(step: scenario.Step) -> str:
    params = step.params if isinstance(step.params, dict) else {}
    if step.method == PERFORM_ACTION_METHOD and "type" in params:
        return f"{step.method}:{params['type']}"
    if step.method == UI_CLICK_METHOD and "name" in params:
        return f"{step.method}:{params['name']}"
    if step.method == SET_WINDOW_METHOD and "width" in params and "height" in params:
        return f"{step.method}:{params['width']}x{params['height']}"
    return step.method


def _canonical(step: scenario.Step) -> str:
    return json.dumps({"method": step.method, "params": step.params}, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
