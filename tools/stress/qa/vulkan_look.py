#!/usr/bin/env python3
"""Runs BagiStress in a window on the Vulkan backend with the SDK's validation and screenshot layers, and reports what they found.

    python3 tools/stress/qa/vulkan_look.py --exe build/macos-rmlui-debug/bin/BagiStress --scene rmlui --count 60 --out <dir outside the repository>

The backend has no `RequestCapture()` (src/Modules/Vulkan/CLAUDE.md, "No capture"), so a screenshot of the control API does not work there and the loop's
headless tests never reach the Vulkan paths. This script is the documented way to reach them by hand: it starts the game **not headless** with `--control
--manual-clock`, `reset`s to the scene and count, `step`s frames so the layer has seen the frame it was asked for, `quit`s, and prints the validation
lines of the game's log (the known finding is marked, any other makes exit code 1) and the `.ppm` files the screenshot layer wrote. It needs a window and a
display, so it is not a ctest; it never writes inside a git work tree.

Exit codes: 0 the run is clean (only the known finding, at most), 1 a new validation line, no picture, or a failed run, 77 the executable, the layer folder
or the screenshot layer is missing. `--dry-run` prints the environment and the command and starts nothing.
Standard library only (and tools/control, tools/shots).
"""

import argparse
import os
import re
import shutil
import sys
from pathlib import Path
from typing import Mapping, Sequence

TOOLS_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS_DIR / "control"))
sys.path.insert(0, str(TOOLS_DIR / "shots"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from png_ops import inside_git_work_tree  # noqa: E402

EXIT_CLEAN = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

DEFAULT_LAYER_PATH = "/usr/local/share/vulkan/explicit_layer.d"
VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation"
SCREENSHOT_LAYER = "VK_LAYER_LUNARG_screenshot"
SCREENSHOT_MANIFEST = "VkLayer_screenshot.json"  # the file of the screenshot layer in an explicit layer folder
DEFAULT_SCENE = "rmlui"
DEFAULT_COUNT = 60
DEFAULT_STEPS = 40
DEFAULT_SHOT_FRAMES = "30"  # a frame below the steps, so the layer has seen it before `quit`
STARTUP_TIMEOUT = 60.0  # a window and a Vulkan device come up slower than the headless game
SHUTDOWN_TIMEOUT = 20.0
PORT_TAKEN_RETRIES = 1
# The one finding that is known: the render-finished semaphores are per frame in flight, not per swapchain image (src/Modules/Vulkan/CLAUDE.md)
KNOWN_FINDINGS = ("VUID-vkQueueSubmit-pSignalSemaphores-00067",)
# A message of the validation layer starts with "Validation Error: [ VUID-... ]"; the spec text of one may follow on the same line
VALIDATION_LINE = re.compile(r"Validation (?:Error|Warning|Performance Warning)|VUID-|UNASSIGNED-")
PPM_MAGIC = b"P6"
PPM_MAX_VALUE = 255


class PpmError(Exception):
    pass


def layer_folders(layer_path: str) -> list[Path]:
    return [Path(part) for part in layer_path.split(os.pathsep) if part]


def screenshot_layer_present(layer_path: str) -> bool:
    return any((folder / SCREENSHOT_MANIFEST).is_file() for folder in layer_folders(layer_path))


def missing_requirement(exe: Path, layer_path: str) -> str | None:
    """Why the run cannot start on this machine, or None."""
    if not exe.is_file():
        return f"{exe} does not exist"
    if not any(folder.is_dir() for folder in layer_folders(layer_path)):
        return f"the layer folder {layer_path} does not exist (the Vulkan SDK is not installed, or give --layer-path)"
    if not screenshot_layer_present(layer_path):
        return f"{SCREENSHOT_MANIFEST} is not in {layer_path}: the screenshot layer is not installed"
    return None


def build_environment(layer_path: str, shot_frames: str, shot_dir: Path) -> dict[str, str]:
    """The variables this run adds to the environment of the game."""
    return {
        "VK_LAYER_PATH": layer_path,
        "VK_INSTANCE_LAYERS": f"{VALIDATION_LAYER}:{SCREENSHOT_LAYER}",
        "VK_SCREENSHOT_FRAMES": shot_frames,
        "VK_SCREENSHOT_DIR": str(shot_dir),
    }


def build_command(exe: Path, port: int) -> list[str]:
    """A window (not headless) with the control API and the manual clock, so `step` decides which frames exist."""
    return build_game_command(exe, port, headless=False, manual_clock=True)


def read_ppm_size(path: Path) -> tuple[int, int]:
    """The width and the height of a binary PPM of the screenshot layer (`P6`, the size and the maximum on separate lines)."""
    try:
        with open(path, "rb") as stream:
            head = stream.read(64)
    except OSError as error:
        raise PpmError(f"{path}: {error}") from error
    fields = head.split(maxsplit=4)
    if len(fields) < 4 or fields[0] != PPM_MAGIC:
        raise PpmError(f"{path} is not a binary PPM (P6)")
    try:
        width, height, maximum = int(fields[1]), int(fields[2]), int(fields[3])
    except ValueError as error:
        raise PpmError(f"{path} has a header that is not numbers") from error
    if width <= 0 or height <= 0 or maximum != PPM_MAX_VALUE:
        raise PpmError(f"{path} has the size {width} x {height} and the maximum {maximum}")
    return width, height


def split_validation_lines(lines: Sequence[str]) -> tuple[list[str], list[str]]:
    """The validation lines of a log as (known, new): known are those that name a finding of KNOWN_FINDINGS."""
    known: list[str] = []
    new: list[str] = []
    for line in lines:
        if not VALIDATION_LINE.search(line):
            continue
        (known if any(finding in line for finding in KNOWN_FINDINGS) else new).append(line)
    return known, new


def written_pictures(shot_dir: Path) -> list[Path]:
    return sorted(shot_dir.glob("*.ppm")) if shot_dir.is_dir() else []


def describe_environment(environment: Mapping[str, str]) -> str:
    return "\n".join(f"{name}={value}" for name, value in environment.items())


def play(exe: Path, scene: str, count: int, steps: int, port: int) -> tuple[GameProcess, str | None]:
    """The run itself; (the stopped process, why it failed or None). The log of the process stays readable."""
    process = GameProcess(build_command(exe, port), cwd=exe.parent)
    failure = None
    try:
        process.start()
        client = process.connect(port, STARTUP_TIMEOUT)
        try:
            scenes = client.call("get_state")["state"]["scenes"]
            if scene not in scenes:
                failure = f"the scene {scene!r} is not one of {scenes}: a build with ENGINE_UI_RMLUI lists rmlui"
            else:
                client.call("reset", {"scene": scene, "count": count})
                client.call("step", {"frames": steps})
            client.call("quit")
        finally:
            try:
                client.close()
            except (OSError, ConnectionClosed):
                pass
        code = process.stop(SHUTDOWN_TIMEOUT)
        if failure is None and code != 0:
            failure = f"the game exited with code {code}"
    except (ControlError, ConnectionClosed, GameLaunchError, OSError, KeyError) as error:
        failure = f"{error!r}"
    finally:
        process.stop(0)
    return process, failure


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", type=Path, required=True, help="the BagiStress executable (a Vulkan build, ENGINE_UI_RMLUI for the scene rmlui)")
    parser.add_argument("--scene", default=DEFAULT_SCENE, help=f"the scene to reset to (default {DEFAULT_SCENE})")
    parser.add_argument("--count", type=int, default=DEFAULT_COUNT, help=f"the count of the scene (default {DEFAULT_COUNT})")
    parser.add_argument("--steps", type=int, default=DEFAULT_STEPS, help=f"frames to step before quit (default {DEFAULT_STEPS})")
    parser.add_argument("--shot-frames", default=DEFAULT_SHOT_FRAMES, help="VK_SCREENSHOT_FRAMES: frame numbers or a range, as the layer reads them")
    parser.add_argument("--layer-path", default=os.environ.get("VK_LAYER_PATH", DEFAULT_LAYER_PATH), help="VK_LAYER_PATH: the folder of the SDK's explicit layers")
    parser.add_argument("--out", type=Path, required=True, help="where the pictures and the game's log go (outside every git work tree)")
    parser.add_argument("--dry-run", action="store_true", help="print the environment and the command, start nothing")
    args = parser.parse_args()

    out = args.out.expanduser().resolve()
    if inside_git_work_tree(out):
        print(f"ERROR --out {out} is inside a git work tree: pictures must stay outside every repository")
        return EXIT_FAILED
    exe = args.exe.resolve()
    shot_dir = out / "shots"
    environment = build_environment(args.layer_path, args.shot_frames, shot_dir)
    port = free_port()
    if args.dry_run:
        print(describe_environment(environment))
        print(" ".join(build_command(exe, port)))
        return EXIT_CLEAN
    missing = missing_requirement(exe, args.layer_path)
    if missing is not None:
        print(f"SKIPPED: {missing}")
        return EXIT_SKIPPED

    shot_dir.mkdir(parents=True, exist_ok=True)
    os.environ.update(environment)  # the game inherits the environment of this process
    process, failure = play(exe, args.scene, args.count, args.steps, port)
    log_copy = out / "game.log"
    if process.log_path.is_file():
        shutil.copyfile(process.log_path, log_copy)
    lines = log_copy.read_text(errors="replace").splitlines() if log_copy.is_file() else []
    known, new = split_validation_lines(lines)
    pictures = written_pictures(shot_dir)

    print(f"scene {args.scene}, count {args.count}, {args.steps} frames; the game's log is {log_copy}")
    for line in known:
        print(f"known   {line}")
    for line in new:
        print(f"NEW     {line}")
    for picture in pictures:
        try:
            width, height = read_ppm_size(picture)
            print(f"picture {picture} ({width} x {height})")
        except PpmError as error:
            print(f"BAD     {error}")
            failure = failure or str(error)
    if failure is None and not pictures:
        failure = f"no .ppm file in {shot_dir}: the screenshot layer wrote nothing (frames {args.shot_frames}, {args.steps} stepped)"
    if failure is not None:
        print(f"FAILED: {failure}\n--- game log (tail) ---\n{process.log_tail()}")
        return EXIT_FAILED
    print(f"{len(known)} known and {len(new)} new validation lines, {len(pictures)} pictures")
    return EXIT_FAILED if new else EXIT_CLEAN


if __name__ == "__main__":
    sys.exit(main())
