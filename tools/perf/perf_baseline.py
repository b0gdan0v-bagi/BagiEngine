"""The machine fingerprint and the baseline plan of the perf runner. Standard library only.

A baseline is a plan of the todo store (`tools/todo/CLAUDE.md`) without an entry, kind `owner`, one per area and machine:
`perf-baseline-<area>-<machine>.md`. It is markdown with one section per update, each holding a fenced JSON record; the newest record
(the last one) is the baseline, the older ones are its history. A record holds every benchmark the area has measured on the machine, each
with the commit it was measured on, so an update of a few benchmarks keeps the others.
"""

import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

RECORD_RE = re.compile(r"```json\n(.*?)\n```", re.DOTALL)
TODO_CLI = Path(__file__).resolve().parents[1] / "todo" / "todo.py"
PLAN_KIND = "owner"  # the store has no kind of its own for it; the name `perf-baseline-*.md` tells it apart
EXIT_STORE_REFUSED = 2  # todo.py: the store refused (404 for a plan that does not exist)
AREAS_RE = re.compile(r"\bareas (.*?), live updates\b")
NO_RIGHT_RE = re.compile(r"\bHTTP (?:403|404)\b")  # the store answers 404 to a token that may not see the plan
STORE_TIMEOUT_S = 120  # a hung call must not hang the run; the first call of a checkout also downloads the served CLI
LOCAL_HINT = "--baseline-file F uses a local baseline file instead"


def _run(command: list[str]) -> str:
    try:
        return subprocess.run(command, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def machine_fingerprint() -> dict:
    """What names the machine: the model, the CPU, the cores, the memory and the OS with its major version (not the patch level)."""
    system = platform.system()
    if system == "Darwin":
        model = _run(["sysctl", "-n", "hw.model"])
        cpu = _run(["sysctl", "-n", "machdep.cpu.brand_string"])
        memory = _run(["sysctl", "-n", "hw.memsize"])
        os_name, os_version = "macos", platform.mac_ver()[0] or _run(["sw_vers", "-productVersion"])
    else:
        model = platform.node() if system != "Windows" else os.environ.get("COMPUTERNAME", "")
        cpu = platform.processor() or platform.machine()
        memory = ""
        os_name, os_version = system.lower(), platform.release()
    return {
        "model": model or platform.machine(),
        "cpu": cpu,
        "cores": os.cpu_count() or 0,
        "memoryGb": round(int(memory) / 2**30) if memory.isdigit() else None,
        "os": os_name,
        "osMajor": os_version.split(".")[0] if os_version else "",
    }


def machine_slug(fingerprint: dict) -> str:
    """A stable short name of the machine for the plan name: `mac16-7-14c-macos26`."""
    raw = f"{fingerprint['model']}-{fingerprint['cores']}c-{fingerprint['os']}{fingerprint['osMajor']}"
    return re.sub(r"[^a-z0-9]+", "-", raw.lower()).strip("-")


def baseline_name(area: str, slug: str) -> str:
    return f"perf-baseline-{area}-{slug}.md"


def latest_record(content: str) -> dict | None:
    """The newest record of a baseline plan, or None when it holds none."""
    records = RECORD_RE.findall(content)
    return json.loads(records[-1]) if records else None


def append_record(content: str | None, area: str, fingerprint: dict, record: dict) -> str:
    """The plan text with `record` appended as its newest section (a new plan when `content` is None)."""
    if not content:
        content = (
            f"# Perf baseline: {area} on {machine_slug(fingerprint)}\n\n"
            f"Machine: `{json.dumps(fingerprint, sort_keys=True)}`.\n\n"
            "Written by `/perf --update-baseline` (`tools/perf/perf.py`, `.claude/skills/perf/SKILL.md`) on the owner's word. "
            "The newest record (the last section) is the baseline; the ones above it are its history.\n"
        )
    measured = sorted({bench["commit"] for bench in record["benchmarks"].values()})
    heading = f"## {record['date']} {', '.join(measured)} ({record['preset']})"
    return f"{content.rstrip()}\n\n{heading}\n\n```json\n{json.dumps(record, indent=2, sort_keys=True)}\n```\n"


def merge_record(previous: dict | None, results: dict, date: str, preset: str, fingerprint: dict) -> dict:
    """A new record: the benchmarks of `previous` with those of `results` (name -> benchmark) put over them."""
    benchmarks = dict(previous["benchmarks"]) if previous else {}
    benchmarks.update(results)
    return {"date": date, "preset": preset, "machine": fingerprint, "benchmarks": benchmarks}


class StoreError(Exception):
    """The store cannot be used; the message is one line and names the local alternative."""


def _unusable(message: str) -> StoreError:
    return StoreError(f"{message} ({LOCAL_HINT})")


def _last_line(text: str) -> str:
    return text.strip().rpartition("\n")[2].strip()


def _reason(done: subprocess.CompletedProcess) -> str:
    """The last line the CLI printed: its loader may put a warning before the error."""
    return _last_line(done.stderr) or _last_line(done.stdout) or f"exit code {done.returncode}"


def _todo(env_file: str | None, *args: str, text: str | None = None) -> subprocess.CompletedProcess:
    """One call of the todo CLI. Raises StoreError when it cannot be started or does not end in time."""
    command = [sys.executable, str(TODO_CLI), *(["--env-file", env_file] if env_file else []), *args]
    try:
        return subprocess.run(command, input=text, capture_output=True, text=True, timeout=STORE_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        raise _unusable(f"todo.py {' '.join(args)}: no answer within {STORE_TIMEOUT_S} s") from None
    except OSError as error:
        raise _unusable(f"todo.py {' '.join(args)}: {error}") from None


def token_areas(env_file: str | None = None) -> list[str]:
    """The areas the checkout's token holds, as `todo.py ping` names them."""
    done = _todo(env_file, "ping")
    if done.returncode != 0:
        raise _unusable(f"todo.py ping: {_reason(done)}")
    match = AREAS_RE.search(done.stdout)
    if not match:
        raise _unusable(f"todo.py ping does not name the token's areas: {_last_line(done.stdout)}")
    return [area.strip() for area in match.group(1).split(",")]


def require_area(area: str, env_file: str | None = None) -> None:
    """Stops before a measurement (or the recording of a report) whose baseline this token could not write."""
    areas = token_areas(env_file)
    if area not in areas:
        raise StoreError(
            f"the token holds the areas {', '.join(areas)}, not `{area}`, so it cannot write this baseline: "
            f"pass --env-file with a token that holds `{area}`, or --baseline-file for a local file"
        )


def read_store(name: str, env_file: str | None = None) -> str | None:
    """The text of the plan `name` in the todo store, None when it does not exist."""
    done = _todo(env_file, "plan", "get", name, "--stdout")
    if done.returncode == EXIT_STORE_REFUSED:
        return None
    if done.returncode != 0:
        raise _unusable(f"todo.py plan get {name}: {_reason(done)}")
    return done.stdout


def write_store(name: str, content: str, env_file: str | None = None) -> str:
    done = _todo(env_file, "--actor", "perf", "plan", "put", name, "--file", "-", "--kind", PLAN_KIND, text=content)
    if done.returncode != 0:
        detail = _reason(done)
        if NO_RIGHT_RE.search(detail):
            raise _unusable(f"the token has no right to write the baseline {name} (a missing right, not a missing plan): {detail}")
        raise _unusable(f"todo.py plan put {name}: {detail}")
    return done.stdout.strip()
