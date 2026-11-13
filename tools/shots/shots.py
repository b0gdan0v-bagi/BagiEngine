"""The screenshot tool of the todo loop (`tools/shots/CLAUDE.md`): before / after pictures of a game's scenes and a text diff of them. Standard library only.

    python3 <engine>/tools/shots/shots.py scenes [--root DIR]
    python3 <engine>/tools/shots/shots.py capture --label before|after --out DIR --scenes a,b@720x1280 [--preset P] [--no-build] [--root DIR]
    python3 <engine>/tools/shots/shots.py diff DIR [--tolerance N] [--root DIR]

Run from the root of a repository whose `.claude/rules/qa-project.md` holds the set of scenes (a fenced JSON block under "## Screenshots";
the contract is in `tools/shots/CLAUDE.md`), or give `--root` (a flag of each command). The tool informs and never gates: `diff` exits 0 whatever the pictures show.
Exit code: 0 done, 1 an error (a bad request, a failed build or run, a missing picture), 77 skipped (no set, or the game's executable is missing).
"""

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import png_diff  # noqa: E402  (also puts the PNG reader on sys.path)
from png_reader import PngError, read_png  # noqa: E402

RULES_FILE = Path(".claude/rules/qa-project.md")
SET_RE = re.compile(r"^## Screenshots\s*$.*?```json\n(.*?)\n```", re.DOTALL | re.MULTILINE)
SHOTS_DIR = Path(__file__).resolve().parent
ENGINE_DIR = SHOTS_DIR.parents[1]
EXIT_ERROR = 1
EXIT_SKIPPED = 77
RUN_TIMEOUT_SECONDS = 300
OUTPUT_TAIL_CHARS = 2000
DIFF_FILE = "diff.txt"
LABELS = ("before", "after")
SIZE_RE = re.compile(r"^\d+x\d+$")
REQUIRED_KEYS = ("preset", "exe", "command", "scenes", "sizes", "defaultSize")


class ShotsError(Exception):
    """A request or a set that cannot be served; the message is printed and the exit code is `code`."""

    def __init__(self, message: str, code: int = EXIT_ERROR):
        super().__init__(message)
        self.code = code


def load_set(root: Path) -> dict:
    """The set of the rules file, checked. A missing file or section is a skip (77), a broken one an error."""
    rules = root / RULES_FILE
    if not rules.is_file():
        raise ShotsError(f"no screenshots: {rules} not found", EXIT_SKIPPED)
    found = SET_RE.search(rules.read_text(encoding="utf-8"))
    if not found:
        raise ShotsError(f"no screenshots: {rules} has no ```json block under '## Screenshots'", EXIT_SKIPPED)
    try:
        shots_set = json.loads(found.group(1))
    except json.JSONDecodeError as error:
        raise ShotsError(f"{rules}: the screenshot set is not JSON: {error}") from error
    for key in REQUIRED_KEYS:
        if key not in shots_set:
            raise ShotsError(f"{rules}: the screenshot set has no '{key}'")
    if not shots_set["command"] or not shots_set["scenes"] or not shots_set["sizes"]:
        raise ShotsError(f"{rules}: 'command', 'scenes' and 'sizes' must not be empty")
    for size in [*shots_set["sizes"], shots_set["defaultSize"]]:
        if not SIZE_RE.match(size):
            raise ShotsError(f"{rules}: '{size}' is not a size WxH")
    if shots_set["defaultSize"] not in shots_set["sizes"]:
        raise ShotsError(f"{rules}: 'defaultSize' {shots_set['defaultSize']} is not in 'sizes'")
    return shots_set


def parse_scenes(spec: str, shots_set: dict) -> dict[str, list[str]]:
    """`a,b@720x1280` as `{size: [scene, ...]}` in request order; every scene and size must be known to the set."""
    groups: dict[str, list[str]] = {}
    for item in filter(None, (part.strip() for part in spec.split(","))):
        name, _, size = item.partition("@")
        size = size or shots_set["defaultSize"]
        if name not in shots_set["scenes"]:
            raise ShotsError(f"unknown scene '{name}'; known scenes: {', '.join(shots_set['scenes'])}")
        if size not in shots_set["sizes"]:
            raise ShotsError(f"unknown size '{size}' for '{name}'; known sizes: {', '.join(shots_set['sizes'])}")
        if name not in groups.setdefault(size, []):
            groups[size].append(name)
    if not groups:
        raise ShotsError("no scenes requested")
    return groups


def inside_git_work_tree(folder: Path) -> bool:
    """Whether `folder` (which may not exist yet) lies in a git work tree: pictures must never end up in a repository."""
    probe = folder.resolve()
    while not probe.exists():
        probe = probe.parent
    try:
        done = subprocess.run(["git", "-C", str(probe), "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True)
    except OSError:
        return False  # no git: nothing it could be a work tree of
    return done.stdout.strip() == "true"


def expand(command: list[str], values: dict[str, str]) -> list[str]:
    """The command with `{name}` of `values` put in; any other braces stay as written (not str.format: an argument may hold JSON)."""
    expanded = []
    for part in command:
        for key, value in values.items():
            part = part.replace("{" + key + "}", value)
        expanded.append(part)
    return expanded


def build(root: Path, preset: str, targets: list[str]) -> None:
    if not (root / "build" / preset / "CMakeCache.txt").is_file():
        done = subprocess.run(["cmake", "--preset", preset], cwd=root, capture_output=True, text=True)
        if done.returncode != 0:
            raise ShotsError(f"configure of preset {preset} failed:\n{(done.stdout + done.stderr)[-OUTPUT_TAIL_CHARS:]}")
    command = ["cmake", "--build", "--preset", preset, "--target", *targets]
    done = subprocess.run(command, cwd=root, capture_output=True, text=True)
    if done.returncode != 0:
        raise ShotsError(f"the build failed:\n{(done.stdout + done.stderr)[-OUTPUT_TAIL_CHARS:]}")


def command_scenes(root: Path, _args: argparse.Namespace) -> int:
    shots_set = load_set(root)
    print(f"sizes: {', '.join(shots_set['sizes'])}")
    print(f"default size: {shots_set['defaultSize']}")
    for name, about in shots_set["scenes"].items():
        print(f"{name}: {about}")
    return 0


def command_capture(root: Path, args: argparse.Namespace) -> int:
    shots_set = load_set(root)
    groups = parse_scenes(args.scenes, shots_set)
    out = Path(args.out).expanduser()
    if inside_git_work_tree(out):
        raise ShotsError(f"--out {out} is inside a git work tree: pictures must stay outside every repository")
    preset = args.preset or shots_set["preset"]
    targets = shots_set.get("targets", [])
    if targets and not args.no_build:
        build(root, preset, targets)

    folder = out / args.label
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.glob("*.png"):  # a picture left by an earlier capture would hide a scene the command did not write
        old.unlink()
    bin_dir = root / "build" / preset / "bin"
    for size, scenes in groups.items():
        values = {
            "root": str(root),
            "engine": str(ENGINE_DIR),
            "python": sys.executable,
            "bin": str(bin_dir),
            "exe": str(bin_dir / shots_set["exe"]),
            "label": args.label,
            "out": str(out),
            "scenes": ",".join(scenes),
            "window": size,
        }
        command = expand(shots_set["command"], values)
        try:
            done = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=RUN_TIMEOUT_SECONDS)
        except subprocess.TimeoutExpired as error:
            raise ShotsError(f"timed out after {error.timeout} s: {' '.join(command)}") from error
        except OSError as error:
            raise ShotsError(f"cannot run {' '.join(command)}: {error}") from error
        if done.returncode == EXIT_SKIPPED:
            print(f"capture skipped: the command exited {EXIT_SKIPPED} (the executable is missing?): {' '.join(command)}")
            return EXIT_SKIPPED
        if done.returncode != 0:
            raise ShotsError(f"exit code {done.returncode}: {' '.join(command)}\n{(done.stdout + done.stderr)[-OUTPUT_TAIL_CHARS:]}")

    names = [path.name for path in folder.glob("*.png")]
    missing = [scene for scenes in groups.values() for scene in scenes if not any(scene in name for name in names)]
    if missing:
        raise ShotsError(f"no picture in {folder} for: {', '.join(missing)} (a picture's file name has to hold its scene's name)")
    print(f"captured {len(names)} pictures into {folder}")
    return 0


def command_diff(_root: Path, args: argparse.Namespace) -> int:
    base = Path(args.dir).expanduser()
    sides = {label: base / label for label in LABELS}
    for label, folder in sides.items():
        if not folder.is_dir():
            raise ShotsError(f"no '{label}' folder in {base}: take the '{label}' pictures first")
    names = {label: {path.name: path for path in folder.glob("*.png")} for label, folder in sides.items()}
    counts = {"changed": 0, "unchanged": 0, "not compared": 0, "only in one side": 0}
    lines: list[str] = []
    for name in sorted(names["before"].keys() | names["after"].keys()):
        if name not in names["after"]:
            lines.append(f"only in before: {name}")
            counts["only in one side"] += 1
        elif name not in names["before"]:
            lines.append(f"only in after: {name}")
            counts["only in one side"] += 1
        else:
            try:
                result = png_diff.compare(read_png(names["before"][name].read_bytes()), read_png(names["after"][name].read_bytes()), args.tolerance)
            except (PngError, OSError) as error:
                lines.append(f"{name}: cannot be read ({error}), not compared")
                counts["not compared"] += 1
                continue
            lines.extend(png_diff.describe(name, result))
            if isinstance(result, png_diff.SizeChange):
                counts["not compared"] += 1
            else:
                counts["changed" if result.changed else "unchanged"] += 1
    lines.append("summary: " + ", ".join(f"{count} {what}" for what, count in counts.items()))
    text = "\n".join(lines) + "\n"
    (base / DIFF_FILE).write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


def main_with(argv: list[str] | None = None) -> int:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--root", default=".", help="the checkout root holding .claude/rules/qa-project.md (default: the current folder)")
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("scenes", parents=[common], help="list the scenes of the rules file")
    capture = commands.add_parser("capture", parents=[common], help="take the pictures of some scenes into DIR/<label>/")
    capture.add_argument("--label", required=True, choices=LABELS)
    capture.add_argument("--out", required=True, help="the folder of the pairs, outside every git work tree")
    capture.add_argument("--scenes", required=True, help="scene[@WxH],... (a scene without a size uses the default size)")
    capture.add_argument("--preset", help="the build preset (default: the set's)")
    capture.add_argument("--no-build", action="store_true", help="do not build the set's targets first")
    diff = commands.add_parser("diff", parents=[common], help="compare DIR/before with DIR/after and write DIR/diff.txt")
    diff.add_argument("dir")
    diff.add_argument("--tolerance", type=int, default=0, help="a pixel is changed when a channel differs by more than this (default 0)")
    args = parser.parse_args(argv)
    handler = {"scenes": command_scenes, "capture": command_capture, "diff": command_diff}[args.command]
    try:
        return handler(Path(args.root).resolve(), args)
    except ShotsError as error:
        print(str(error), file=sys.stderr if error.code == EXIT_ERROR else sys.stdout)
        return error.code


def main() -> int:
    return main_with()


if __name__ == "__main__":
    sys.exit(main())
