"""The progress file of `build_and_deploy.py --status-file FILE`: one JSON object, rewritten whole at every change, so a reader (the
studio hub) sees the stage the run is in without parsing the output of cmake and ctest.

    {"format": 1, "target", "stages": ["fetch", "tests", "build", "package", "deploy"], "stage", "state": "running|done|failed",
     "commit", "engineCommit", "service", "address", "url", "failedTests", "error", "startedAt", "updatedAt"}

`done` at the stage `deploy` means `railway up --detach` accepted the upload, not that the page is live. Only these fields are
written (never an environment value or a secret); `error` is one line. Standard library only.
"""

import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path

FORMAT_VERSION = 1
ALL_STAGES = ("fetch", "tests", "build", "package", "deploy")
STATE_RUNNING, STATE_DONE, STATE_FAILED = "running", "done", "failed"
SETTABLE_FIELDS = ("commit", "engineCommit", "failedTests")


def _timestamp() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


class Status:
    """The state of one run. `path` None writes nothing (no --status-file, or a dry run); a file that cannot be written is reported
    once on stderr and then left alone, because the progress report must not stop a deploy."""

    def __init__(self, path: Path | None, target: str | None, stages: list[str], address: str | None, service: str | None, clock=_timestamp):
        unknown = [stage for stage in stages if stage not in ALL_STAGES]
        if unknown:
            raise ValueError(f"unknown stage {unknown[0]!r}")
        self._path = path
        self._clock = clock
        started = clock()
        self._fields: dict = {
            "format": FORMAT_VERSION, "target": target, "stages": list(stages), "stage": None, "state": STATE_RUNNING,
            "commit": None, "engineCommit": None, "service": service, "address": address,
            "url": f"https://{address}" if address else None, "failedTests": [], "error": None, "startedAt": started, "updatedAt": started,
        }
        self._write()

    def enter(self, stage: str) -> None:
        if stage not in self._fields["stages"]:
            raise ValueError(f"stage {stage!r} is not one of {self._fields['stages']}")
        self._fields.update(stage=stage, state=STATE_RUNNING)
        self._write()

    def set(self, **fields) -> None:
        unknown = [name for name in fields if name not in SETTABLE_FIELDS]
        if unknown:
            raise ValueError(f"cannot set {unknown[0]!r}")
        self._fields.update(fields)
        self._write()

    def done(self) -> None:
        self._fields.update(state=STATE_DONE, error=None)
        self._write()

    def fail(self, message: str) -> None:
        """Ends the run as failed in the stage it is in (the first one when it stopped before entering any)."""
        if self._fields["stage"] is None and self._fields["stages"]:
            self._fields["stage"] = self._fields["stages"][0]
        self._fields.update(state=STATE_FAILED, error=" ".join(message.split()))
        self._write()

    def _write(self) -> None:
        if self._path is None:
            return
        self._fields["updatedAt"] = self._clock()
        temp = self._path.with_name(self._path.name + ".tmp")
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            temp.write_text(json.dumps(self._fields, indent=2) + "\n", encoding="utf-8")
            os.replace(temp, self._path)
        except OSError as error:
            print(f"build_and_deploy.py: cannot write the status file {self._path}: {error}", file=sys.stderr)
            self._path = None
