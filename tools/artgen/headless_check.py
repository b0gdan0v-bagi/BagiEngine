"""Runs the whole art generation flow of the skill /artgen in a headless editor and fails when any step does not hold.

    python tools/artgen/headless_check.py build/macos-debug/bin/BagiStress [--accept SPRITE] [--accept-sound NAME]

The editor starts as the skill starts it (`--editor --headless --manual-clock --control` on a free port, a throw-away
BAGI_EDITOR_DIR), the bridge as `artgen_bridge.py --generator fake --exit-with-editor`. Then: a Generate run, a set, optionally an
Accept of an image and of a sound, with no `step` call: the frame counter stays at 0 through all of it. Accept writes into the game's
real folders (the editor cannot be given another game root), so it is off unless asked and the command undoes it. Exit codes: 0 passed,
1 failed, 77 skipped (no executable, or a build without the editor).
"""

import argparse
import contextlib
import os
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

ARTGEN_DIR = Path(__file__).resolve().parent
CONTROL_DIR = ARTGEN_DIR.parent / "control"
sys.path.insert(0, str(CONTROL_DIR))

from bagi_session import GameSession, SessionError  # noqa: E402
from bagi_control_client import ControlError  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77
EDITOR_DIR_VARIABLE = "BAGI_EDITOR_DIR"  # where the editor keeps its per-user files (widgets-module.md): a temp folder, never the real one
RUN_MODE_EDITOR = "Editor"
BRIDGE_SCRIPT = ARTGEN_DIR / "artgen_bridge.py"
CONNECT_TIMEOUT_S = 15.0
GENERATE_TIMEOUT_S = 20.0
BRIDGE_EXIT_TIMEOUT_S = 10.0
POLL_INTERVAL_S = 0.1
SPRITE_PRESET = "sprite"
SOUND_PRESET = "sfx"
VARIANT_COUNT = 2
FIRST_SEED = 4242
SET_SUBJECTS = ("first piece", "second piece")
SERVICE_STATE_READY = "ready"
RUN_DONE = "Done"
SET_COMPLETE = "Complete"
ARTGEN_FOLDER = "artgen"
OUT_FOLDER = "out"
LIBRARY_FILE = Path("config") / "TextureLibrary.xml"
EMPTY_LIBRARY = "<root><sprites/></root>\n"
STRESS_GAME_ROOT = ARTGEN_DIR.parent / "stress"  # the game root of BagiStress
SPRITES_FOLDER = Path("assets") / "artgen"


class CheckFailed(Exception):
    """A step of the flow did not hold; the message says which and what was seen."""


def check(condition: object, message: str) -> None:
    if not condition:
        raise CheckFailed(message)


def say(message: str) -> None:
    print(message, flush=True)


def wait_until(probe: Callable[[], Any], timeout: float, what: str) -> Any:
    """Polls `probe` until it gives a truthy value; fails with `what` after `timeout` seconds."""
    deadline = time.monotonic() + timeout
    while True:
        value = probe()
        if value:
            return value
        check(time.monotonic() < deadline, f"timed out after {timeout:.0f} s waiting for {what}")
        time.sleep(POLL_INTERVAL_S)


@contextlib.contextmanager
def temporary_editor_directory() -> Iterator[None]:
    """The editor writes its layout and preferences under $BAGI_EDITOR_DIR on exit; the game started inside inherits the variable."""
    previous = os.environ.get(EDITOR_DIR_VARIABLE)
    with tempfile.TemporaryDirectory() as folder:
        os.environ[EDITOR_DIR_VARIABLE] = folder
        try:
            yield
        finally:
            if previous is None:
                del os.environ[EDITOR_DIR_VARIABLE]
            else:
                os.environ[EDITOR_DIR_VARIABLE] = previous


def start_editor(session: GameSession, exe: Path) -> dict[str, Any]:
    """The skill's own editor; answers `{port, status, ...}`. `None` when the build has no editor (`--editor` left it in the Game mode)."""
    description = session.launch(str(exe), editor=True)
    status = description["status"]
    if status["runMode"] != RUN_MODE_EDITOR:
        return {}
    check(status["window"]["headless"] is True, f"the editor is not headless: {status['window']}")
    state = session.call("artgen_state")
    check("service" in state and "gameRoot" in state, f"artgen_state has no service or game root: {state}")
    say(f"editor: {status['runMode']}, headless, control port {description['port']}, artgen_state answers at frame {status.get('frame')}")
    return description


def start_bridge(port: int, log_path: Path) -> subprocess.Popen:
    """The bridge as the skill runs it, with the fake generator."""
    log = open(log_path, "wb")
    command = [sys.executable, str(BRIDGE_SCRIPT), "--port", str(port), "--generator", "fake", "--exit-with-editor"]
    try:
        return subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
    finally:
        log.close()


def wait_for_service(session: GameSession, bridge: subprocess.Popen) -> None:
    def ready() -> bool:
        check(bridge.poll() is None, f"the bridge exited with code {bridge.returncode} before it connected")
        service = session.call("artgen_state")["service"]
        return service["connected"] and service["state"] == SERVICE_STATE_READY

    wait_until(ready, CONNECT_TIMEOUT_S, "the bridge to connect with the generator ready")
    say("bridge: connected, generator ready")


def wait_for_run(session: GameSession) -> dict[str, Any]:
    def finished() -> dict[str, Any] | None:
        run = session.call("artgen_state")["run"]
        check(run["state"] not in ("Failed", "Cancelled"), f"the run ended {run['state']}: {run.get('error')}")
        return run if run["state"] == RUN_DONE else None

    return wait_until(finished, GENERATE_TIMEOUT_S, "the run to be Done")


def generate(session: GameSession, preset: str, subject: str, count: int) -> dict[str, Any]:
    session.call("artgen_generate", {"preset": preset, "subject": subject, "count": count, "seed": FIRST_SEED})
    run = wait_for_run(session)
    variants = run["variants"]
    check([variant["seed"] for variant in variants] == [FIRST_SEED + index for index in range(count)], f"the variants of '{preset}' are {variants}, expected {count} from seed {FIRST_SEED}")
    for variant in variants:
        check(Path(variant["path"]).is_file(), f"the file of a variant is missing: {variant['path']}")
        check(Path(variant["provenance"]).is_file(), f"the provenance of a variant is missing: {variant['provenance']}")
    say(f"generate: preset '{preset}' made {count} variants, seeds {FIRST_SEED}..{FIRST_SEED + count - 1}")
    return run


def run_set(session: GameSession, unique: str) -> None:
    subjects = [f"{subject} {unique}" for subject in SET_SUBJECTS]
    queued = session.call("artgen_enqueue", {"preset": SPRITE_PRESET, "subjects": subjects, "count": 1, "seed": FIRST_SEED, "set": f"headless-{unique}"})
    name = queued["set"]
    check(len(queued["jobs"]) == len(subjects), f"the set has jobs {queued['jobs']}, expected {len(subjects)}")
    finished = wait_until(lambda: (lambda state: state if state["state"] == SET_COMPLETE else None)(session.call("artgen_set", {"set": name})), GENERATE_TIMEOUT_S, f"the set '{name}' to be Complete")
    picked = session.call("artgen_pick", {"set": name, "job": queued["jobs"][0], "seed": FIRST_SEED})
    check(picked.get("pick") is not None, f"artgen_pick answered {picked}")
    say(f"set: '{name}' complete with {len(finished.get('jobs', []))} jobs, one picked")


def snapshot(folder: Path) -> set[Path]:
    return set(folder.rglob("*")) if folder.is_dir() else set()


def remove_new(folder: Path, before: set[Path]) -> None:
    """Removes what a run added under `folder`, the deepest first; what was there before stays."""
    for path in sorted(snapshot(folder) - before, key=lambda item: len(item.parts), reverse=True):
        if path.is_dir():
            with contextlib.suppress(OSError):
                path.rmdir()
        else:
            path.unlink(missing_ok=True)
    if not before:
        with contextlib.suppress(OSError):
            folder.rmdir()


@contextlib.contextmanager
def accepting_into(game_root: Path) -> Iterator[None]:
    """Gives the game's assets and library back as they were: Accept writes into the real folders of the game.

    A game with no `TextureLibrary.xml` cannot take a sprite (the library is read at start-up), so an empty one is there for the run.
    """
    library = game_root / LIBRARY_FILE
    library_bytes = library.read_bytes() if library.is_file() else None
    sprites = game_root / SPRITES_FOLDER
    before = snapshot(sprites)
    if library_bytes is None:
        library.parent.mkdir(parents=True, exist_ok=True)
        library.write_text(EMPTY_LIBRARY, encoding="utf-8")
    try:
        yield
    finally:
        remove_new(sprites, before)
        if library_bytes is None:
            library.unlink(missing_ok=True)
        else:
            library.write_bytes(library_bytes)


def accept_image(session: GameSession, sprite: str, run: dict[str, Any]) -> None:
    answer = session.call("artgen_accept", {"seed": run["variants"][0]["seed"], "sprite": sprite})
    png = Path(answer["png"])
    check(answer["sprite"] == sprite and png.is_file(), f"accept did not write the sprite: {answer}")
    check(png.stat().st_size > 0 and answer["width"] > 0 and answer["height"] > 0, f"the accepted picture is empty: {answer}")
    check("reloadedSprites" in answer, f"the answer has no reloadedSprites: {answer}")
    say(f"accept: '{sprite}' written, {answer['width']}x{answer['height']}, reloaded {answer['reloadedSprites']} sprites")


def accept_sound(session: GameSession, name: str, run: dict[str, Any]) -> None:
    answer = session.call("artgen_accept", {"seed": run["variants"][0]["seed"], "name": name})
    wav = Path(answer["wav"])
    check(answer["clip"] and wav.is_file() and wav.stat().st_size > 0, f"accept did not write the clip: {answer}")
    check(answer["seconds"] > 0, f"the accepted sound is empty: {answer}")
    say(f"accept sound: '{name}' written, {answer['seconds']:.2f} s")


def run_flow(session: GameSession, args: argparse.Namespace, bridge: subprocess.Popen) -> None:
    game_root = Path(session.call("artgen_state")["gameRoot"])
    check(game_root.resolve() == args.game_root.resolve(), f"the editor works on the game root {game_root}, not {args.game_root}: name it with --game-root")
    out_folder = game_root / ARTGEN_FOLDER / OUT_FOLDER
    out_before = snapshot(out_folder)
    unique = f"headless {os.getpid()}"
    try:
        wait_for_service(session, bridge)
        run = generate(session, SPRITE_PRESET, f"a rook {unique}", VARIANT_COUNT)
        run_set(session, str(os.getpid()))
        if args.accept:
            accept_image(session, args.accept, run)
        if args.accept_sound:
            sound_run = generate(session, SOUND_PRESET, f"a click {unique}", 1)
            accept_sound(session, args.accept_sound, sound_run)
        status = session.call("status")
        check(status["frame"] == 0, f"the flow needed frames: the frame counter is {status['frame']}")
        say("frames: none needed, counter at 0")
        check(status["log"]["errors"] == 0, f"the editor logged {status['log']['errors']} errors")
    finally:
        remove_new(out_folder, out_before)


def end_processes(session: GameSession, bridge: subprocess.Popen) -> None:
    """Quits the editor and checks that the bridge follows it by itself."""
    session.quit()
    try:
        bridge.wait(BRIDGE_EXIT_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        bridge.kill()
        bridge.wait()
        raise CheckFailed(f"the bridge did not exit within {BRIDGE_EXIT_TIMEOUT_S:.0f} s of the editor")
    check(bridge.returncode == 0, f"the bridge exited with code {bridge.returncode}")
    say("end: the editor quit and the bridge followed it")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", type=Path, help="a built game with the editor, e.g. build/macos-debug/bin/BagiStress")
    parser.add_argument("--accept", metavar="SPRITE", help="also Accept the first variant as this sprite, then undo it (writes into the game's folders)")
    parser.add_argument("--accept-sound", metavar="NAME", help="also generate a sound and Accept it as this clip, then undo it")
    parser.add_argument("--game-root", type=Path, default=STRESS_GAME_ROOT, help="the game's folder with config/ and assets/ (default: tools/stress, the game root of BagiStress)")
    args = parser.parse_args(argv)
    exe = args.exe.expanduser().resolve()
    if not exe.is_file():
        say(f"SKIPPED: there is no executable at {exe}")
        return EXIT_SKIPPED

    session = GameSession()
    bridge: subprocess.Popen | None = None
    accepting = args.accept or args.accept_sound
    with temporary_editor_directory(), tempfile.TemporaryDirectory() as scratch, accepting_into(args.game_root) if accepting else contextlib.nullcontext():
        bridge_log = Path(scratch) / "bridge.log"
        try:
            description = start_editor(session, exe)
            if not description:
                say("SKIPPED: this build has no editor")
                session.close()
                return EXIT_SKIPPED
            bridge = start_bridge(description["port"], bridge_log)
            run_flow(session, args, bridge)
            end_processes(session, bridge)
            problems = session.log_problems()
            check(not problems, "the editor's log has problem lines:\n" + "\n".join(problems[:10]))
        except (CheckFailed, SessionError, ControlError) as error:
            say(f"FAILED: {error}")
            if bridge is not None:
                say("--- bridge log ---\n" + bridge_log.read_text(errors="replace")[-2000:])
            with contextlib.suppress(SessionError):
                say("--- editor log (tail) ---\n" + session.game_log())
            return EXIT_FAILED
        finally:
            session.close()
            if bridge is not None and bridge.poll() is None:
                bridge.kill()
                bridge.wait()
    say("PASSED")
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
