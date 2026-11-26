"""Stand-ins for cmake, ctest and the todo CLI, run as programs by alltests.py (`python3 fakes.py <tool> ...`).

The environment steers them: FAKE_CALLS (a file every call appends its arguments to, one JSON list per line), FAKE_FAIL_STEP
(`configure` or `build`: cmake fails there), FAKE_TESTS (the test names), FAKE_FAILING (fail in both runs), FAKE_FLAKY (fail only
in the first run), FAKE_SKIPPED (skip, as exit 77 does), FAKE_SMOKE (labelled `smoke`), FAKE_STORE (the JSON file the fake todo CLI
keeps its entries in), FAKE_TODO_DOWN (set: the todo CLI cannot use its store), FAKE_TODO_REFUSED (entry ids whose write the store
refuses)."""

import json
import os
import sys
from html import escape
from pathlib import Path

CTEST_FAILED_EXIT = 8
STORE_REFUSED_EXIT = 2


def names(variable: str) -> list[str]:
    return [name for name in os.environ.get(variable, "").split(",") if name]


def record(tool: str, args: list[str]) -> None:
    with open(os.environ["FAKE_CALLS"], "a", encoding="utf-8") as stream:
        stream.write(json.dumps([tool, os.getcwd(), *args]) + "\n")


def cmake(args: list[str]) -> int:
    step = "build" if "--build" in args else "configure"
    if os.environ.get("FAKE_FAIL_STEP") == step:
        print(f"src/Broken.cpp:12: error: {step} broke")
        return 1
    return 0


def ctest(args: list[str]) -> int:
    junit = Path(args[args.index("--output-junit") + 1])
    rerun = "--rerun-failed" in args
    failing = set(names("FAKE_FAILING")) | (set() if rerun else set(names("FAKE_FLAKY")))
    tests = names("FAKE_TESTS")
    if rerun:
        tests = [name for name in tests if name in set(names("FAKE_FAILING")) | set(names("FAKE_FLAKY"))]
    cases = []
    for name in tests:
        labels = '<properties><property name="cmake_labels" value="smoke"/></properties>' if name in names("FAKE_SMOKE") else "<properties/>"
        if name in names("FAKE_SKIPPED"):
            cases.append(f'<testcase name="{escape(name)}" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/>{labels}'
                         f'<system-out>no executable</system-out></testcase>')
        elif name in failing:
            output = escape(f"[ RUN      ] {name}\nExpected: 1\n  Actual: 2 {'(rerun)' if rerun else '(first run)'}\n")
            cases.append(f'<testcase name="{escape(name)}" status="fail"><failure message="Failed"/><system-out>{output}</system-out></testcase>')
        else:
            cases.append(f'<testcase name="{escape(name)}" status="run"><system-out>ok</system-out></testcase>')
    junit.write_text(f'<?xml version="1.0"?>\n<testsuite tests="{len(tests)}">{"".join(cases)}</testsuite>\n', encoding="utf-8")
    return CTEST_FAILED_EXIT if any(name in failing for name in tests) else 0


def option(args: list[str], name: str) -> str | None:
    return args[args.index(name) + 1] if name in args else None


def todo(args: list[str]) -> int:
    if args[:1] == ["--actor"]:
        args = args[2:]
    command, entry, rest = args[0], args[1], args[2:]
    if os.environ.get("FAKE_TODO_DOWN"):
        print("todo loader: the service did not answer; running the last copy", file=sys.stderr)
        print("todo: the todo service does not answer", file=sys.stderr)
        return 1
    if command in ("add", "edit") and entry in names("FAKE_TODO_REFUSED"):
        print(f"todo: PUT /entries/{entry}: HTTP 409: refused", file=sys.stderr)
        return STORE_REFUSED_EXIT
    store_file = Path(os.environ["FAKE_STORE"])
    store = json.loads(store_file.read_text(encoding="utf-8")) if store_file.is_file() else {}
    if command == "show":
        if entry not in store:
            print(f"404: no entry {entry}", file=sys.stderr)
            return STORE_REFUSED_EXIT
        print(json.dumps({"id": entry, **store[entry]}))
        return 0
    if command == "add":
        if entry in store:
            print(f"409: {entry} exists", file=sys.stderr)
            return STORE_REFUSED_EXIT
        store[entry] = {"area": option(rest, "--area"), "title": option(rest, "--title"), "status": "open", "version": 1, "body": sys.stdin.read()}
    elif command == "edit":
        expected = option(rest, "--expected-version")
        if entry not in store or (expected and int(expected) != store[entry]["version"]):
            print(f"409: stale {entry}", file=sys.stderr)
            return STORE_REFUSED_EXIT
        store[entry]["body"] = sys.stdin.read()
        store[entry]["status"] = option(rest, "--status") or store[entry]["status"]
        store[entry]["version"] += 1
    else:
        print(f"the fake knows no {command}", file=sys.stderr)
        return 1
    store_file.write_text(json.dumps(store), encoding="utf-8")
    return 0


if __name__ == "__main__":
    tool, arguments = sys.argv[1], sys.argv[2:]
    record(tool, arguments)
    sys.exit({"cmake": cmake, "ctest": ctest, "todo": todo}[tool](arguments))
