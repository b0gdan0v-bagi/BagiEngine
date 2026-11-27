#!/usr/bin/env python3
"""Records a session of BagiDemo as a scenario file, gives it a reference picture, plays it green and then red: the second half of docs/guide/control-api.md.
Run by ctest as BagiDemoGuideScript.record_a_scenario.

    python3 docs/guide/scripts/record_a_scenario.py [path of BagiDemo] [--cleanup]

The scenario goes into a temporary folder, because a recording refuses a folder inside a git work tree; the folder is kept for the reader and its name printed; `--cleanup` (what ctest passes) removes it only after a pass, so a failed run leaves the pictures to look at.
The pictures of a failed play go into the same folder (`--failures`), so two runs never share one.
Exit code: 0 every claim held, 1 one did not (an assert), 77 the game is not built (ctest reads it as skipped).
Standard library only; Python 3.10 or later.
"""

import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ENGINE_ROOT / "tools" / "control"))

from bagi_session import GameSession  # noqa: E402

DEFAULT_GAME = "build/macos-debug/bin/BagiDemo"
EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77
FRAME_SECONDS = 1 / 60
TITLE_NODE = "Text/Title"
TITLE_COMPONENT = "TextComponent"
SCENARIO_TITLE = "Hello from a scenario"
CHANGED_TITLE = "Hello from a changed scenario"
PICTURE_SIZE = 400
REFERENCE = {"png": "references/title.png"}
SHOWN_LINES = 6  # of what a play prints: the lines that name the failure and its pictures
FAILURES_FOLDER = "failures"
CLEANUP_FLAG = "--cleanup"
FOLDER_PATH = re.compile(r"/\S+/")  # the folders of this machine in a printed path: the page and the reader's screen show file names


def play(exe: Path, scenario: Path, *flags: str) -> tuple[int, str]:
    player = ENGINE_ROOT / "tools" / "control" / "bagi_play.py"
    failures = scenario.parent / FAILURES_FOLDER
    done = subprocess.run([sys.executable, str(player), str(exe), str(scenario), "--failures", str(failures), *flags], capture_output=True, text=True, check=False)
    return done.returncode, FOLDER_PATH.sub("", done.stdout + done.stderr)


def show(label: str, code: int, output: str) -> None:
    print(f"{label}: exit {code}")
    for line in output.strip().splitlines()[:SHOWN_LINES]:
        print("   ", line)


def main() -> int:
    arguments = [argument for argument in sys.argv[1:] if argument != CLEANUP_FLAG]
    exe = Path(arguments[0] if arguments else ENGINE_ROOT / DEFAULT_GAME)
    if not exe.is_file():
        print(f"skipped: no game at {exe.name}")
        return EXIT_SKIPPED

    folder = Path(tempfile.mkdtemp(prefix="bagi-guide-"))
    print("folder:", folder.name)
    code = record_and_play(exe, folder)
    if CLEANUP_FLAG in sys.argv:
        shutil.rmtree(folder, ignore_errors=True)
    return code


def record_and_play(exe: Path, folder: Path) -> int:
    scenario = folder / "title.jsonl"
    session = GameSession()
    try:
        session.launch(exe=str(exe), record=str(scenario))
        session.call("scene_set", {"node": TITLE_NODE, "component": TITLE_COMPONENT, "path": "text", "value": SCENARIO_TITLE})
        session.step(2, FRAME_SECONDS)
        session.screenshot(max_size=PICTURE_SIZE)
        session.quit()
    finally:
        session.close()

    lines = [json.loads(line) for line in scenario.read_text(encoding="utf-8").splitlines() if line.strip()]
    assert [line.get("method") for line in lines[1:]] == ["scene_set", "step", "screenshot"]
    lines[-1].setdefault("expect", {})["reference"] = REFERENCE
    scenario.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")

    code, output = play(exe, scenario, "--update")
    show("update", code, output)
    assert code == EXIT_PASSED and (folder / REFERENCE["png"]).is_file()

    code, output = play(exe, scenario)
    show("play", code, output)
    assert code == EXIT_PASSED and "PASSED" in output

    scenario.write_text(scenario.read_text(encoding="utf-8").replace(SCENARIO_TITLE, CHANGED_TITLE), encoding="utf-8")
    code, output = play(exe, scenario)
    show("play after the change", code, output)
    assert code == EXIT_FAILED and ".actual.png" in output and ".diff.png" in output
    return 0


if __name__ == "__main__":
    sys.exit(main())
