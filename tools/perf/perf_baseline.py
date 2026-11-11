"""The machine fingerprint and the baseline plan of the perf runner. Standard library only.

A baseline is a plan of kind `baseline` in the todo store (`tools/todo/CLAUDE.md`), one per area and machine:
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
EXIT_STORE_REFUSED = 2  # todo.py: the store refused (404 for a plan that does not exist)


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
    pass


def read_store(name: str) -> str | None:
    """The text of the plan `name` in the todo store, None when it does not exist."""
    done = subprocess.run([sys.executable, str(TODO_CLI), "plan", "get", name, "--stdout"], capture_output=True, text=True)
    if done.returncode == EXIT_STORE_REFUSED:
        return None
    if done.returncode != 0:
        raise StoreError(f"todo.py plan get {name}: {done.stderr.strip() or done.stdout.strip()}")
    return done.stdout


def write_store(name: str, content: str) -> str:
    done = subprocess.run(
        [sys.executable, str(TODO_CLI), "--actor", "perf", "plan", "put", name, "--file", "-", "--kind", "baseline"],
        input=content,
        capture_output=True,
        text=True,
    )
    if done.returncode != 0:
        raise StoreError(f"todo.py plan put {name}: {done.stderr.strip() or done.stdout.strip()}")
    return done.stdout.strip()
