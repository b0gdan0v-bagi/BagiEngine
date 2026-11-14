#!/usr/bin/env python3
"""Plays the screenshot calls of a scenario on each renderer backend that can take a picture and compares the pictures.

    python3 tools/control/bagi_backends.py <exe> <scenario.jsonl> --out DIR [--backends software,window] [--threshold N] [--max-share X]
    python3 tools/control/bagi_backends.py --preset macos-debug --game BagiStress <scenario.jsonl> --out DIR

The calls of the scenario are sent in order without their expectations (`reference` too); the PNG of every `screenshot` call is kept in
`DIR/<profile>/<n>-<scenario name>.png`. The first available profile is picture A, each other one is compared with it, and the difference maps go to
`DIR/diff-<profile>-<n>.png`. The tool informs: two renderers differ in anti-aliasing, so a difference is not a failure.
Exit code: 0 done (also with fewer than two available profiles), 1 `--max-share` exceeded, 2 a bad file or arguments, 77 the executable does not exist.
Standard library only (through bagi_session.py).
"""

import argparse
import base64
import sys
from dataclasses import dataclass, field
from pathlib import Path

import scenario
from bagi_control_client import ControlError
from bagi_play import EXIT_FAILED, EXIT_PASSED, EXIT_SKIPPED, EXIT_USAGE, launch_options
from bagi_session import GameSession, SessionError
from game_process import DEFAULT_STARTUP_TIMEOUT, preset_executable

import png_diff  # noqa: E402  (after bagi_play, whose imports put tools/shots on sys.path)
import png_ops  # noqa: E402
from png_reader import PngError, read_png  # noqa: E402

SCREENSHOT_METHOD = "screenshot"
PERCENT = 100.0
SDL_DOC = 'src/Modules/SDL/CLAUDE.md, "Headless and screenshots"'


@dataclass(frozen=True)
class Profile:
    """One way to run the game. `headless` is None for a backend that is only listed: it never starts, `why_not` says why."""

    name: str
    headless: bool | None
    why_not: str = ""


PROFILES = {
    profile.name: profile
    for profile in (
        Profile("software", headless=True),  # no window, the SDL software renderer: available wherever the game runs
        Profile("window", headless=False),  # a real window, the SDL renderer on the machine's driver: needs a display
        Profile("vulkan", None, f"cannot capture (VulkanRendererBackend has no capture; {SDL_DOC})"),
        Profile("browser", None, f"cannot capture (the browser build cannot be reached by this tool; {SDL_DOC})"),
    )
}
DEFAULT_BACKENDS = ("software", "window")


@dataclass
class Capture:
    """What a profile gave: the PNG bytes of every screenshot call in order, or why it did not run."""

    profile: str
    pictures: list[bytes] = field(default_factory=list)
    unavailable: str | None = None


def capture_profile(profile: Profile, exe: Path, header: scenario.Header, steps: list[scenario.Step], startup_timeout: float) -> Capture:
    """Starts the game as `profile` says and plays the calls of the scenario without expectations; every failure to run is `unavailable`, not an error."""
    result = Capture(profile.name)
    if profile.headless is None:
        result.unavailable = profile.why_not
        return result
    _, _, extra = launch_options(header)
    session = GameSession()
    try:
        try:
            description = session.launch(str(exe), headless=profile.headless, manual_clock=True, extra=extra, startup_timeout=startup_timeout)
        except SessionError as error:
            result.unavailable = str(error).strip().splitlines()[0] if str(error).strip() else "the game did not start"
            return result
        if description.get("activeGame") != header.game:
            result.unavailable = f"the game that started is {description.get('activeGame')!r}, the scenario names {header.game!r}"
            return result
        for step in steps:
            try:
                answer = session.call(step.method, step.params)
            except ControlError as error:
                if step.method == SCREENSHOT_METHOD:
                    result.unavailable = f"screenshot failed: {error.code}: {error.message}"
                    return result
                continue  # a call that the scenario expected to be refused, or one this profile cannot do: the pictures are what counts
            except SessionError as error:
                result.unavailable = f"lost the game: {error}"
                return result
            if step.method == SCREENSHOT_METHOD:
                try:
                    result.pictures.append(base64.b64decode(answer[scenario.SCREENSHOT_PICTURE_KEY]))
                except (KeyError, TypeError, ValueError):
                    result.unavailable = "screenshot answered without a usable picture"
                    return result
        try:
            session.quit()
        except SessionError:
            pass  # the pictures are taken
    finally:
        session.close()
    return result


def _write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _compare_text(found: png_diff.ImageDiff) -> str:
    comparison = found.comparison
    if comparison.changed == 0:
        return "unchanged"
    box = list(comparison.bbox) if comparison.bbox else None
    scaled = ", B scaled to A" if found.scaled else ""
    return f"{comparison.share * PERCENT:.4g}% changed ({comparison.changed} of {comparison.total} px), box {box}{scaled}"


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", metavar="[exe] scenario", help="the game executable (unless --preset and --game name it) and the scenario file")
    parser.add_argument("--preset", help="the CMake preset that built the game: the executable is build/<preset>/bin/<game>")
    parser.add_argument("--game", help="the executable name for --preset, e.g. BagiStress")
    parser.add_argument("--root", type=Path, help="the repository whose build/ folder --preset means (default: the one holding these tools)")
    parser.add_argument("--out", type=Path, required=True, metavar="DIR", help="where the pictures and the difference maps go (outside every git work tree)")
    parser.add_argument("--backends", default=",".join(DEFAULT_BACKENDS), help=f"comma-separated profiles of {', '.join(PROFILES)} (default: %(default)s)")
    parser.add_argument("--threshold", type=int, default=0, help="a pixel counts as changed when a channel differs by more than this, 0..255 (default %(default)s)")
    parser.add_argument("--max-share", type=float, metavar="X", help="exit 1 when more than this share (0..1) of the pixels of any picture differ")
    parser.add_argument("--timeout", type=float, default=DEFAULT_STARTUP_TIMEOUT, help="seconds to wait for the control API after a start")
    args = parser.parse_args(argv)

    by_preset = args.preset is not None or args.game is not None
    if by_preset and (args.preset is None or args.game is None):
        parser.error("--preset and --game go together")
    if len(args.paths) != (1 if by_preset else 2):
        parser.error("give the scenario file after the executable, or after --preset and --game (the executable is then not given)")
    names = [name.strip() for name in args.backends.split(",") if name.strip()]
    unknown = [name for name in names if name not in PROFILES]
    if unknown or not names:
        parser.error(f"unknown backend {', '.join(unknown) or '(none given)'}; known: {', '.join(PROFILES)}")
    if not 0 <= args.threshold <= png_diff.MAX_THRESHOLD:
        parser.error(f"--threshold has to be from 0 to {png_diff.MAX_THRESHOLD}")
    if args.max_share is not None and not 0.0 <= args.max_share <= 1.0:
        parser.error("--max-share is a share from 0 to 1")
    args.backend_names = list(dict.fromkeys(names))
    args.scenario = Path(args.paths[-1])
    args.exe = preset_executable(args.preset, args.game, args.root) if by_preset else Path(args.paths[0])
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        header, steps = scenario.parse(args.scenario.read_text(encoding="utf-8"), str(args.scenario))
    except (OSError, UnicodeDecodeError) as error:
        print(f"ERROR cannot read {args.scenario}: {error}")
        return EXIT_USAGE
    except scenario.ScenarioError as error:
        print(f"ERROR {error}")
        return EXIT_USAGE
    if not any(step.method == SCREENSHOT_METHOD for step in steps):
        print(f"ERROR {args.scenario} has no screenshot call, so there is nothing to compare")
        return EXIT_USAGE
    if png_ops.inside_git_work_tree(args.out):
        print(f"ERROR --out {args.out} is inside a git work tree: pictures must stay outside every repository")
        return EXIT_USAGE
    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED

    exe = args.exe.resolve()
    captures = [capture_profile(PROFILES[name], exe, header, steps, args.timeout) for name in args.backend_names]
    for capture in captures:
        if capture.unavailable is not None:
            print(f"{capture.profile}: not available: {capture.unavailable}")
            continue
        for number, data in enumerate(capture.pictures, start=1):
            _write(args.out / capture.profile / f"{number}-{args.scenario.stem}.png", data)
        print(f"{capture.profile}: {len(capture.pictures)} pictures in {args.out / capture.profile}")
    available = [capture for capture in captures if capture.unavailable is None]
    if len(available) < 2:
        print(f"{len(available)} backend available: there is nothing to compare (a missing backend is not a failure)")
        return EXIT_PASSED
    return _compare(args, available)


def _compare(args: argparse.Namespace, available: list[Capture]) -> int:
    reference = available[0]
    differing = exceeded = compared = 0
    largest: tuple[float, str] = (0.0, "")
    for other in available[1:]:
        print(f"{reference.profile} (A) vs {other.profile} (B):")
        if len(reference.pictures) != len(other.pictures):
            print(f"  the profiles took {len(reference.pictures)} and {len(other.pictures)} pictures: only the first {min(len(reference.pictures), len(other.pictures))} are compared")
        for number, (first, second) in enumerate(zip(reference.pictures, other.pictures), start=1):
            try:
                found = png_diff.image_diff(read_png(first), read_png(second), args.threshold)
            except (PngError, ValueError) as error:
                print(f"  {number}: cannot be compared: {error}")
                continue
            _write(args.out / f"diff-{other.profile}-{number}.png", png_ops.encode_png(found.map))
            share = found.comparison.share
            compared += 1
            differing += found.comparison.changed > 0
            if share > largest[0]:
                largest = (share, f"{other.profile} {number}")
            if args.max_share is not None and share > args.max_share:
                exceeded += 1
            print(f"  {number}: {_compare_text(found)}")
    detail = f", the largest: {largest[0] * PERCENT:.4g}% ({largest[1]})" if differing else ""
    print(f"summary: {differing} of {compared} pictures differ{detail}; the maps are in {args.out}")
    if exceeded:
        print(f"FAILED: {exceeded} picture(s) over --max-share {args.max_share}")
        return EXIT_FAILED
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
