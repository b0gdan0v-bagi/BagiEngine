#!/usr/bin/env python3
"""A game in a browser page that is started like a game executable: for `smoke.py`-style harnesses, `bagi_play.py` and the QA tools.

    BAGI_WEB_DIST=<dist> tools/web/web_game.py --control [--control-port=N] [--headless] [--manual-clock] [--window=WxH] [--density=D]

It serves a dist folder (tools/web/package.py) of a web Debug build, runs the control bridge (tools/control/bagi_bridge.py) on the
control port and opens the page, which dials the bridge: the control API is then on that port, as of a desktop game. The page's
console goes to stdout. --window and --density size the browser's viewport; any other flag is refused. Contract and limits:
.claude/rules/control-web-link.md.

Environment: BAGI_WEB_DIST (absolute, or relative to the engine root: a harness starts an executable in its own folder),
BAGI_WEB_BROWSER (`chromium`, the default, or `webkit`), BAGI_WEB_HEADED (non-empty and not 0: a browser window to watch).
Needs Playwright; when this interpreter has none, the script starts again under <engine>/build/web-venv (tools/web/tests/CLAUDE.md).

Exit code: 0 the game ended (after `quit`); 1 the page did not load, aborted, threw, crashed or was closed, or the port is taken;
2 a wrong command line or BAGI_WEB_BROWSER; 77 no dist, no Playwright or no browser.
"""

import math
import os
import re
import subprocess
import sys
import urllib.parse
from dataclasses import dataclass
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ENGINE_ROOT / "tools" / "control"))
sys.path.insert(0, str(ENGINE_ROOT / "tools" / "web" / "tests"))

from bagi_bridge import DEFAULT_CONTROL_PORT, MAX_PORT, Bridge  # noqa: E402
from web_smoke import CHANNEL_MISSING_PATTERN, DEFAULT_CHROMIUM_ARGS, PAGE_NAME, load_playwright, served  # noqa: E402

EXIT_ENDED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_SKIPPED = 77

DIST_VARIABLE = "BAGI_WEB_DIST"
BROWSER_VARIABLE = "BAGI_WEB_BROWSER"
HEADED_VARIABLE = "BAGI_WEB_HEADED"
BROWSERS = ("chromium", "webkit")
VENV_DIR = ENGINE_ROOT / "build" / "web-venv"

DEFAULT_VIEWPORT = (1280, 720)
LOAD_TIMEOUT_SECONDS = 60.0  # for the document alone; how long the game may take to dial is the harness's start-up timeout
PUMP_MILLISECONDS = 100  # how often the console and page events of the browser are handled
UNLINK_TIMEOUT_SECONDS = 5.0  # for the page's last answer to reach the bridge before the browser is closed

# The lines web/shell.html prints from the runtime's onExit and onAbort
ENDED_LINE = re.compile(r"^The game ended(?: \(exit status (-?\d+)\))?$")
ABORTED_PREFIX = "The game aborted: "

_WINDOW = re.compile(r"([0-9]{1,5})x([0-9]{1,5})")


class UsageError(Exception):
    pass


@dataclass
class Flags:
    control_port: int = DEFAULT_CONTROL_PORT
    manual_clock: bool = False
    viewport: tuple[int, int] = DEFAULT_VIEWPORT
    density: float = 1.0


def parse_flags(argv: list[str]) -> Flags:
    """The flags of a game that mean something for a page. A flag given twice keeps its first value, as in the engine."""
    values: dict[str, str] = {}
    for argument in argv:
        name, equals, value = argument.partition("=")
        if name in ("--control", "--headless", "--manual-clock"):
            if equals:
                raise UsageError(f"{name} takes no value here")
        elif name in ("--control-port", "--window", "--density"):
            if not value:
                raise UsageError(f"{name} needs a value: {name}=...")
        else:
            raise UsageError(f"a game in a page does not take '{argument}'")
        values.setdefault(name, value)
    if "--control" not in values:
        raise UsageError("--control is needed: without the control API there is nothing to do with the page")

    flags = Flags(manual_clock="--manual-clock" in values)
    if (port := values.get("--control-port")) is not None:
        if not port.isdecimal() or int(port) > MAX_PORT:
            raise UsageError(f"--control-port is 0 to {MAX_PORT}")
        flags.control_port = int(port)
    if (window := values.get("--window")) is not None:
        match = _WINDOW.fullmatch(window)
        if match is None or 0 in (int(match.group(1)), int(match.group(2))):
            raise UsageError("--window is WxH, two positive integers")
        flags.viewport = (int(match.group(1)), int(match.group(2)))
    if (density := values.get("--density")) is not None:
        try:
            flags.density = float(density)
        except ValueError:
            raise UsageError("--density is a number") from None
        if not (math.isfinite(flags.density) and flags.density > 0):
            raise UsageError("--density is a positive number")
    return flags


def page_url(base: str, flags: Flags, token: str, page_port: int) -> str:
    """The address of the page; `fullscreen=0` is the switch of web/shell.html that keeps a tap from resizing the canvas."""
    query = {"fullscreen": "0", "controlToken": token, "control": str(page_port)}
    if flags.manual_clock:
        query["controlClock"] = "manual"
    return f"{base}?{urllib.parse.urlencode(query)}"


def console_outcome(text: str) -> int | None:
    """The exit code when the console line says that the game is over, else None."""
    if text.startswith(ABORTED_PREFIX):
        return EXIT_FAILED
    ended = ENDED_LINE.match(text)
    if ended is None:
        return None
    return EXIT_ENDED if ended.group(1) in (None, "0") else EXIT_FAILED


def find_dist(value: str | None) -> Path | None:
    if not value:
        return None
    dist = ENGINE_ROOT / value  # an absolute value replaces the root
    return dist if (dist / PAGE_NAME).is_file() else None


def venv_python(venv: Path) -> Path:
    return venv / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")


def restart_under_venv(venv: Path, argv: list[str]) -> int:
    """Runs this script again under `venv`; 77 when there is none or this already is its interpreter."""
    python = venv_python(venv)
    if not python.is_file() or Path(sys.prefix).resolve() == venv.resolve():
        print(f"SKIPPED: Playwright is not installed, neither here nor in {venv} (tools/web/tests/CLAUDE.md)", flush=True)
        return EXIT_SKIPPED
    command = [str(python), str(Path(__file__).resolve()), *argv]
    if sys.platform == "win32":
        return subprocess.run(command).returncode  # exec there starts a new process and loses the exit code
    os.execv(command[0], command)


def say(text: str) -> None:
    # A harness reads the file stdout goes to while the game runs
    print(text, flush=True)


def play(playwright, browser_name: str, headed: bool, url: str, flags: Flags, errors: type[Exception], bridge: Bridge) -> int:
    """Opens the page and stays until the game is over; the exit code."""
    options = {"headless": not headed}
    if browser_name == "chromium":
        options["args"] = list(DEFAULT_CHROMIUM_ARGS)
    try:
        browser = getattr(playwright, browser_name).launch(**options)
    except errors as error:
        if not re.search(CHANNEL_MISSING_PATTERN, str(error)):
            raise
        say(f"SKIPPED: no {browser_name} (playwright install {browser_name}): {str(error).strip().splitlines()[0]}")
        return EXIT_SKIPPED

    outcomes: list[int] = []  # the first one counts

    def on_console(message) -> None:
        say(message.text)
        outcome = console_outcome(message.text)
        if outcome is not None:
            outcomes.append(outcome)

    def lost(why: str) -> None:
        # The [Fatal] tag makes the line a finding for whoever reads the log of the "game" (game_process.log_problems)
        if not outcomes:
            say(f"[Fatal] web_game: {why}")
            outcomes.append(EXIT_FAILED)

    try:
        context = browser.new_context(viewport={"width": flags.viewport[0], "height": flags.viewport[1]}, device_scale_factor=flags.density)
        page = context.new_page()
        page.on("console", on_console)
        page.on("pageerror", lambda error: lost(f"page error: {error}"))
        page.on("crash", lambda _: lost("the page crashed"))
        page.on("close", lambda _: lost("the page was closed"))
        browser.on("disconnected", lambda _: lost("the browser is gone"))
        response = page.goto(url, wait_until="domcontentloaded", timeout=LOAD_TIMEOUT_SECONDS * 1000)
        if response is None or not response.ok:
            lost(f"the page was not served: HTTP {None if response is None else response.status}")
        while not outcomes:
            page.wait_for_timeout(PUMP_MILLISECONDS)
        if outcomes[0] == EXIT_ENDED:
            # The answer to `quit` may still be on its way: the page unlinks after it
            bridge.wait_unlinked(UNLINK_TIMEOUT_SECONDS)
    except errors as error:
        lost(str(error).strip().splitlines()[0])
    finally:
        outcomes.append(EXIT_FAILED)  # closing the browser below reports a closed page, which is no news
        browser.close()
    return outcomes[0]


def main(argv: list[str] | None = None, environ=None, venv: Path = VENV_DIR) -> int:
    argv = sys.argv[1:] if argv is None else argv
    environ = os.environ if environ is None else environ
    browser_name = environ.get(BROWSER_VARIABLE) or BROWSERS[0]
    try:
        flags = parse_flags(argv)
        if browser_name not in BROWSERS:
            raise UsageError(f"{BROWSER_VARIABLE} is one of {', '.join(BROWSERS)}, not '{browser_name}'")
    except UsageError as error:
        print(f"web_game: {error}", file=sys.stderr, flush=True)
        return EXIT_USAGE
    dist = find_dist(environ.get(DIST_VARIABLE))
    if dist is None:
        say(f"SKIPPED: {DIST_VARIABLE} does not name a folder with {PAGE_NAME} (tools/web/package.py makes one)")
        return EXIT_SKIPPED
    playwright_api = load_playwright()
    if playwright_api is None:
        return restart_under_venv(venv, argv)

    bridge = Bridge(control_port=flags.control_port, page_port=0, log=lambda line: say(f"bagi_bridge: {line}"))
    try:
        page_port = bridge.start()
    except OSError as error:
        say(f"[Fatal] web_game: {error}")
        return EXIT_FAILED
    try:
        with served(dist) as base, playwright_api.sync_playwright() as playwright:
            url = page_url(base, flags, bridge.token, page_port)
            headed = environ.get(HEADED_VARIABLE, "") not in ("", "0")
            return play(playwright, browser_name, headed, url, flags, playwright_api.Error, bridge)
    finally:
        bridge.stop()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(EXIT_FAILED)
