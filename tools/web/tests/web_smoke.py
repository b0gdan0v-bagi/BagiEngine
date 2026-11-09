#!/usr/bin/env python3
"""Browser smoke test of the web build: serves a dist folder (tools/web/package.py) with serve.py, opens it in a headless Chromium
or Edge through Playwright for Python and checks that the game starts and answers a click or a tap.

    python tools/web/tests/web_smoke.py --dist dist
    python tools/web/tests/web_smoke.py --dist dist --click 320,240
    python tools/web/tests/web_smoke.py --dist dist --touch --device-scale-factor 2
    python tools/web/tests/web_smoke.py --dist dist --expect-console "Initialized with \\d+ worker threads" --browser-channel msedge
    python tools/web/tests/web_smoke.py --selftest

The checks, in this order:
  1. the page loads: HTTP 200 for the page, no uncaught page error;
  2. the first frame renders: the canvas exists and a screenshot of it is not one flat color (polled until --timeout: a wasm takes
     a while to start);
  3. a click on the canvas reaches the game: a screenshot after the click differs from the one taken right before it (--click X,Y in
     pixels from the top left corner of the canvas, the middle of the canvas without it; --touch makes it a tap, which SDL turns
     into a mouse event). The pointer is moved to the click point first (a hover effect is shown before the shots, so it cannot
     pass for the click's; not with --touch, which has no pointer). Then the canvas is watched with no click for as long as it is
     watched after one, --settle + CLICK_TIMEOUT_SECONDS (5 s): if any shot in that window differs from the first one, the canvas
     changes by itself, the click cannot be judged and the run FAILS with "(3) cannot judge" (no click is made);
  4. the browser console has no error-level message outside the allow-list (DEFAULT_CONSOLE_ALLOW plus every --allow-console REGEX,
     searched in the text of the message and in the URL it names), and every --expect-console REGEX matched a message (of any level)
     within --timeout;
  5. the canvas size: the backing store (`canvas.width` x `canvas.height`) is the CSS size times `devicePixelRatio`, to within a
     pixel (--device-scale-factor N sets the ratio of the browser). Skipped, with the reason printed, for the --selftest page.

The report also prints `window.crossOriginIsolated`, `typeof SharedArrayBuffer`, `devicePixelRatio`, the CSS size and the backing
store of the canvas, and whether the input was a click or a tap. The server is serve.py's own, with the isolation decision it
makes for a real run (switch, then manifest.json `threads`, then on): a threaded dist/ is served with the two headers.

--selftest serves a page generated in a temp folder (a canvas that redraws the same still pattern on every animation frame and a
moving one after a click, with a manifest.json of `threads: false`) instead of a build, so the logic of this script runs without a
wasm build. --selftest-fault breaks that page on purpose (flat, console-error, page-error, deaf, moving): the run has to exit 1, which
shows each of the four checks can fail and check 3 can fail in both ways (deaf: the click changes nothing; moving: the canvas
changes by itself, "cannot judge"). Add --timeout 5 for flat, which waits for a frame that never comes.

The browser: Playwright's Chromium by default (`playwright install chromium`), --browser-channel NAME for an installed one
(`msedge`, `chrome`), --executable-path FILE for any Chromium-based executable. Exit code: 0 passed, 1 failed (the reason is
printed), 77 skipped (Playwright is not installed, or the named browser is absent), 2 a wrong command line.

Limits: check 3 needs a canvas that stands still, with the pointer on the click point, for --settle + 5 s before the click (a game
with an idle animation, a fade-in that is not over by then or a cursor that blinks fails with "cannot judge"; the idle shots are about
0.5 s apart, so a blink short enough to fall between two shots is not seen, and a change slower than the whole window is not seen
and can pass for the click's; raise --settle for a start that is slow). The screenshots are decoded by
tools/control/tests/png_reader.py (imported from there, not copied), in plain Python: keep the viewport small.
Verified against a real Chromium and Edge with --selftest and every --selftest-fault (WP 6 of slice 2 of the entry web-build).
"""

import argparse
import importlib
import json
import re
import sys
import tempfile
import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

TOOLS_DIR = Path(__file__).resolve().parents[2]
# serve.py is in tools/web, png_reader.py in tools/control/tests
sys.path.insert(0, str(TOOLS_DIR / "web"))
sys.path.insert(0, str(TOOLS_DIR / "control" / "tests"))

import serve  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_SKIPPED = 77

PAGE_NAME = "index.html"
CANVAS_SELECTOR = "canvas#canvas"
DEFAULT_VIEWPORT = (640, 480)
DEFAULT_TIMEOUT_SECONDS = 60.0
DEFAULT_SETTLE_SECONDS = 1.0
CLICK_TIMEOUT_SECONDS = 5.0
POLL_INTERVAL_SECONDS = 0.5
# Measured on Windows 11 (Chromium 158, Edge 157): a headless Chromium gets WebGL 2 from SwiftShader with no flag at all, but a headless
# Edge picks the machine's GPU (Direct3D 11) without these. The two flags force the same software renderer everywhere, so a run does
# not depend on a driver; --enable-unsafe-swiftshader is what newer Chromium needs to allow SwiftShader when it is asked for.
DEFAULT_CHROMIUM_ARGS = ("--use-angle=swiftshader", "--enable-unsafe-swiftshader")
CANVAS_SIZE_TOLERANCE_PIXELS = 1  # check 5: the backing store may differ from CSS size x pixel ratio by this much (rounding)
# Console errors that say nothing about the game: a browser asking for the icon of a page that has none. Nothing else was needed for
# the selftest page in Chromium 158 and Edge 157 (the WebGL warnings of SwiftShader are warnings, not errors); the game's own
# allowed messages are named by the run that needs them (--allow-console)
DEFAULT_CONSOLE_ALLOW = (r"favicon\.ico",)
SELFTEST_FAULTS = ("none", "flat", "console-error", "page-error", "deaf", "moving")
SELFTEST_THREADED = False  # the generated dist/ is single-thread: its manifest.json says so, and serve.py then sends no isolation headers
# What the page tells about itself (page.evaluate); read after the first frame
PAGE_INFO_SCRIPT = """() => {
    const canvas = document.querySelector("canvas#canvas");
    const box = canvas.getBoundingClientRect();
    return {
        crossOriginIsolated: window.crossOriginIsolated === true,
        sharedArrayBuffer: typeof SharedArrayBuffer,
        devicePixelRatio: window.devicePixelRatio,
        cssWidth: box.width,
        cssHeight: box.height,
        canvasWidth: canvas.width,
        canvasHeight: canvas.height,
    };
}"""


class SmokeFailure(Exception):
    pass


@dataclass
class ConsoleEntry:
    kind: str  # Playwright's message.type: "log", "warning", "error", ...
    text: str
    url: str = ""


@dataclass
class PageInfo:
    """What the page reports about itself: isolation, SharedArrayBuffer, pixel ratio and the canvas sizes."""

    cross_origin_isolated: bool
    shared_array_buffer: str  # the result of `typeof SharedArrayBuffer`: "function" or "undefined"
    device_pixel_ratio: float
    css_width: float
    css_height: float
    canvas_width: int
    canvas_height: int

    @staticmethod
    def from_page(values: dict) -> "PageInfo":
        return PageInfo(bool(values["crossOriginIsolated"]), str(values["sharedArrayBuffer"]), float(values["devicePixelRatio"]),
                        float(values["cssWidth"]), float(values["cssHeight"]), int(values["canvasWidth"]), int(values["canvasHeight"]))


@dataclass
class BrowserRun:
    """What the browser showed, as plain data; evaluate() judges it without a browser."""

    status: int | None = None
    first_png: bytes | None = None  # the first drawn frame (check 2)
    idle_png: bytes | None = None  # the first shot with the pointer on the click point: no later shot before the click may differ
    before_png: bytes | None = None  # the last shot before the click: what the one after it is compared with
    second_png: bytes | None = None  # after the click
    console: list[ConsoleEntry] = field(default_factory=list)
    page_errors: list[str] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)  # found while running (no canvas, no click point inside it, ...)
    page_info: PageInfo | None = None  # read after the first frame (check 5 and the report)
    check_canvas_size: bool = False  # check 5 applies (not to the selftest page)
    touch: bool = False  # the click of check 3 was a tap


@dataclass
class Options:
    dist: Path | None
    selftest: bool
    click: tuple[float, float] | None
    viewport: tuple[int, int]
    timeout: float
    settle: float
    allow_console: list[str]
    chromium_args: list[str]
    selftest_fault: str
    browser_channel: str | None = None
    executable_path: Path | None = None
    device_scale_factor: float = 1.0
    touch: bool = False
    expect_console: list[str] = field(default_factory=list)


# ----- command line -----

def parse_point(text: str) -> tuple[float, float]:
    match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*,\s*(\d+(?:\.\d+)?)\s*", text)
    if not match:
        raise argparse.ArgumentTypeError(f"'{text}' is not X,Y (two non-negative numbers, such as 320,240)")
    return float(match.group(1)), float(match.group(2))


def parse_size(text: str) -> tuple[int, int]:
    match = re.fullmatch(r"\s*(\d+)\s*[xX,]\s*(\d+)\s*", text)
    if not match or int(match.group(1)) < 1 or int(match.group(2)) < 1:
        raise argparse.ArgumentTypeError(f"'{text}' is not WIDTHxHEIGHT (such as 640x480)")
    return int(match.group(1)), int(match.group(2))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Open a web build in headless Chromium and check that it starts and answers a click.")
    parser.add_argument("--dist", type=Path, help="folder made by tools/web/package.py (holds index.html)")
    parser.add_argument("--selftest", action="store_true", help="serve a generated page instead of a build, to test this script")
    parser.add_argument("--selftest-fault", choices=SELFTEST_FAULTS, default="none",
                        help="break the generated page on purpose; the run then has to fail (default: none)")
    parser.add_argument("--click", type=parse_point, metavar="X,Y", help="where to click, in pixels from the top left of the canvas (default: the middle)")
    parser.add_argument("--viewport", type=parse_size, metavar="WxH", default=DEFAULT_VIEWPORT,
                        help=f"size of the browser window (default: {DEFAULT_VIEWPORT[0]}x{DEFAULT_VIEWPORT[1]}; small, the screenshots are decoded in Python)")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_SECONDS,
                        help=f"seconds to wait for the page and the first frame (default: {DEFAULT_TIMEOUT_SECONDS:g})")
    parser.add_argument("--settle", type=float, default=DEFAULT_SETTLE_SECONDS,
                        help=f"seconds to wait after the first frame, after the pointer move and after the click (raise it for a game that is "
                             f"slow to start); the idle window of check 3 is this + {CLICK_TIMEOUT_SECONDS:g} s long (default: {DEFAULT_SETTLE_SECONDS:g})")
    parser.add_argument("--allow-console", action="append", default=[], metavar="REGEX",
                        help="an error-level console message matching this is not a failure (repeatable)")
    parser.add_argument("--chromium-arg", action="append", default=[], metavar="ARG",
                        help="an extra command-line flag for Chromium (repeatable)")
    parser.add_argument("--browser-channel", metavar="NAME",
                        help="an installed browser instead of Playwright's Chromium: msedge, chrome, ... (exit 77 when it is absent)")
    parser.add_argument("--executable-path", type=Path, metavar="FILE",
                        help="a Chromium-based executable to launch (exit 77 when the file is absent)")
    parser.add_argument("--device-scale-factor", type=float, default=1.0, metavar="N",
                        help="the pixel ratio of the browser, to check the canvas backing store against (default: 1)")
    parser.add_argument("--touch", action="store_true",
                        help="the click of check 3 is a tap (a touch screen in the browser; the pointer is not moved first)")
    parser.add_argument("--expect-console", action="append", default=[], metavar="REGEX",
                        help="fail unless a console message (any level) matching this appears within --timeout (repeatable)")
    return parser


def parse_options(argv: list[str] | None = None) -> Options:
    """The command line as Options; a wrong one ends the program with exit code 2 (argparse)."""
    parser = build_parser()
    args = parser.parse_args(argv)
    if bool(args.dist) == args.selftest:
        parser.error("name exactly one of --dist and --selftest")
    if args.selftest_fault != "none" and not args.selftest:
        parser.error("--selftest-fault belongs to --selftest")
    if args.timeout <= 0 or args.settle < 0:
        parser.error("--timeout must be positive and --settle not negative")
    if args.device_scale_factor <= 0:
        parser.error("--device-scale-factor must be positive")
    if args.browser_channel and args.executable_path:
        parser.error("name at most one of --browser-channel and --executable-path")
    for option, patterns in (("--allow-console", args.allow_console), ("--expect-console", args.expect_console)):
        for pattern in patterns:
            try:
                re.compile(pattern)
            except re.error as error:
                parser.error(f"{option} '{pattern}' is not a regular expression: {error}")
    return Options(args.dist, args.selftest, args.click, args.viewport, args.timeout, args.settle, args.allow_console,
                   args.chromium_arg, args.selftest_fault, args.browser_channel, args.executable_path, args.device_scale_factor,
                   args.touch, args.expect_console)


# ----- judging: no browser needed -----

def decode(png: bytes | None, what: str) -> PngImage:
    if not png:
        raise SmokeFailure(f"{what}: no screenshot was taken")
    try:
        return read_png(png)
    except PngError as error:
        raise SmokeFailure(f"{what}: the screenshot is not a readable PNG: {error}") from error


def is_flat(image: PngImage) -> bool:
    """True when every pixel of the picture is the same color (an empty canvas, a cleared screen)."""
    if image.width == 0 or image.height == 0:
        return True
    first = image.rows[0][: image.channels] * image.width
    return all(row == first for row in image.rows)


def images_differ(first: PngImage, second: PngImage) -> bool:
    return (first.width, first.height) != (second.width, second.height) or first.rows != second.rows


def console_error_allowed(entry: ConsoleEntry, allow: list[str]) -> bool:
    patterns = [*DEFAULT_CONSOLE_ALLOW, *allow]
    return any(re.search(pattern, entry.text) or (entry.url and re.search(pattern, entry.url)) for pattern in patterns)


def unmatched_patterns(console: list[ConsoleEntry], patterns: list[str]) -> list[str]:
    """The patterns that no console message (of any level) matches."""
    return [pattern for pattern in patterns if not any(re.search(pattern, entry.text) for entry in console)]


def canvas_size_problem(info: PageInfo, tolerance: int = CANVAS_SIZE_TOLERANCE_PIXELS) -> str | None:
    """Check 5 on given numbers: None when the backing store is the CSS size times the pixel ratio (rounded) to within `tolerance`
    pixels in both directions, else the reason."""
    for axis, css, backing in (("width", info.css_width, info.canvas_width), ("height", info.css_height, info.canvas_height)):
        expected = round(css * info.device_pixel_ratio)
        if abs(backing - expected) > tolerance:
            return (f"(5) canvas {axis}: the backing store is {backing} px but CSS {css:g} px x devicePixelRatio "
                    f"{info.device_pixel_ratio:g} is {expected} px")
    return None


def evaluate(run: BrowserRun, allow_console: list[str] | None = None, expect_console: list[str] | None = None) -> list[str]:
    """The reasons the run failed, one line each; empty when it passed. The five checks of the module comment, in order."""
    allow = allow_console or []
    problems = list(run.problems)

    if run.status != 200:
        problems.append(f"(1) the page answered HTTP {run.status}, not 200")
    for error in run.page_errors:
        problems.append(f"(1) uncaught error in the page: {error}")

    first = None
    try:
        first = decode(run.first_png, "(2) first frame")
        if is_flat(first):
            problems.append(f"(2) the first frame is one flat color {first.pixel(0, 0)}: nothing was drawn")
    except SmokeFailure as error:
        problems.append(str(error))

    # The shot after the click is taken only after a drawn first frame, a still canvas and a click; when the run stopped before,
    # problems say why. It is compared with the shot right before the click, not with the first frame: a game that is still
    # fading in at the first frame would differ from it with no click at all
    if run.second_png is not None:
        try:
            before = decode(run.before_png, "(3) before the click")
            second = decode(run.second_png, "(3) after the click")
            if not images_differ(before, second):
                problems.append("(3) the canvas looks the same after the click: the click did not reach the game")
        except SmokeFailure as error:
            problems.append(str(error))

    for entry in run.console:
        if entry.kind == "error" and not console_error_allowed(entry, allow):
            where = f" ({entry.url})" if entry.url else ""
            problems.append(f"(4) console error: {entry.text}{where}")
    for pattern in unmatched_patterns(run.console, expect_console or []):
        problems.append(f"(4) no console message matched --expect-console '{pattern}'")

    if run.check_canvas_size and run.page_info is not None:
        problem = canvas_size_problem(run.page_info)
        if problem:
            problems.append(problem)
    return problems


def poll(grab: Callable[[], bytes], accept: Callable[[bytes], bool], timeout: float, interval: float | None = None,
         now: Callable[[], float] = time.monotonic, sleep: Callable[[float], None] = time.sleep) -> tuple[bytes, bool]:
    """Calls `grab` until `accept` likes its result or `timeout` seconds are gone (at least one call is made).

    Returns the last result and whether it was accepted."""
    interval = POLL_INTERVAL_SECONDS if interval is None else interval
    deadline = now() + timeout
    while True:
        result = grab()
        if accept(result):
            return result, True
        if now() >= deadline:
            return result, False
        sleep(interval)


def png_is_drawn(png: bytes) -> bool:
    try:
        return not is_flat(read_png(png))
    except PngError:
        return False


# ----- the fake page of --selftest -----

def selftest_page(fault: str = "none") -> str:
    """A page for the checks: a canvas that redraws the same still stripes on every animation frame (so the picture before a click
    does not depend on the frame) and other stripes (another palette, a different direction, moving) after a click. `fault` breaks
    it on purpose: a flat canvas, an error in the console, an uncaught error, a canvas that does not hear the click, a canvas that
    moves by itself before any click."""
    flat = "true" if fault == "flat" else "false"
    deaf = "true" if fault == "deaf" else "false"
    moving = "true" if fault == "moving" else "false"
    console_error = 'console.error("selftest: this error is on purpose");' if fault == "console-error" else ""
    page_error = 'setTimeout(function () { throw new Error("selftest: this error is on purpose"); }, 100);' if fault == "page-error" else ""
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>web_smoke selftest</title>
<link rel="icon" href="data:,">
<style>html, body {{ margin: 0; background: #000; }} canvas {{ display: block; }}</style>
</head>
<body>
<canvas id="canvas" width="400" height="300"></canvas>
<script>
"use strict";
var canvas = document.getElementById("canvas");
var context = canvas.getContext("2d");
var flat = {flat};
var deaf = {deaf};
var moving = {moving};
var frame = 0;
var clicked = false;
function draw() {{
    var width = canvas.width;
    var height = canvas.height;
    if (flat) {{
        context.fillStyle = "#336699";
        context.fillRect(0, 0, width, height);
    }} else if (clicked) {{
        for (var y = 0; y < height; y += 20) {{
            context.fillStyle = "hsl(" + ((y * 3 + frame * 2) % 360) + ", 90%, 50%)";
            context.fillRect(0, y, width, 20);
        }}
    }} else if (moving) {{
        for (var x = 0; x < width; x += 20) {{
            context.fillStyle = "hsl(" + (200 + ((x + frame * 7) % 60)) + ", 40%, 30%)";
            context.fillRect(x, 0, 20, height);
        }}
    }} else {{
        for (var x = 0; x < width; x += 20) {{
            context.fillStyle = "hsl(" + (200 + (x % 60)) + ", 40%, 30%)";
            context.fillRect(x, 0, 20, height);
        }}
    }}
    frame += 1;
    requestAnimationFrame(draw);
}}
if (!deaf) {{
    canvas.addEventListener("mousedown", function () {{ clicked = true; }});
}}
console.log("selftest page started");
{console_error}
{page_error}
requestAnimationFrame(draw);
</script>
</body>
</html>
"""


def write_selftest_site(folder: Path, fault: str = "none") -> Path:
    """index.html and a manifest.json that says `threads: false`: serve.py then decides on no isolation headers, as for a real build."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / PAGE_NAME).write_text(selftest_page(fault), encoding="utf-8")
    manifest = {"game": "selftest", "threads": SELFTEST_THREADED, "files": {}}
    (folder / serve.MANIFEST_NAME).write_text(json.dumps(manifest), encoding="utf-8")
    return folder


# ----- serving -----

@contextmanager
def served(root: Path):
    """serve.py's server for `root` on a free port of 127.0.0.1 in a thread; yields its base URL and stops it afterwards. The
    isolation headers are decided like serve.py does with no switch and no variable: manifest.json of `root`, else on."""
    isolation, reason = serve.decide_cross_origin_isolation(None, None, root)
    print(f"web_smoke: cross-origin isolation {'on' if isolation else 'off'}: {reason}", flush=True)
    server = serve.make_server(root, "127.0.0.1", 0, isolation, quiet=True)
    thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_address[1]}/"
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=10)


# ----- the browser -----

def load_playwright():
    """playwright.sync_api, or None when Playwright is not installed."""
    try:
        return importlib.import_module("playwright.sync_api")
    except ImportError:
        return None


class BrowserMissing(Exception):
    """Playwright is there but the browser is not: its Chromium (`playwright install chromium`), the file of --executable-path or
    the channel of --browser-channel."""


# What Playwright says when an installed browser of a channel is absent ("Chromium distribution 'msedge' is not found at ...")
CHANNEL_MISSING_PATTERN = r"is not found at|Executable doesn't exist|distribution '[^']*' is not found"


def launch_browser(playwright, options: Options):
    """Starts the browser the options name. Raises BrowserMissing when that browser is absent; a browser that is there and does not
    start is an ordinary exception."""
    args = [*DEFAULT_CHROMIUM_ARGS, *options.chromium_args]
    if options.executable_path is not None:
        if not options.executable_path.is_file():
            raise BrowserMissing(f"no browser at '{options.executable_path}'")
        return playwright.chromium.launch(headless=True, args=args, executable_path=str(options.executable_path))
    if options.browser_channel:
        try:
            return playwright.chromium.launch(headless=True, args=args, channel=options.browser_channel)
        except Exception as error:
            if re.search(CHANNEL_MISSING_PATTERN, str(error)):
                raise BrowserMissing(f"no browser of the channel '{options.browser_channel}': {str(error).strip().splitlines()[0]}") from error
            raise
    executable = Path(playwright.chromium.executable_path)
    if not executable.is_file():
        raise BrowserMissing(f"no Chromium at '{executable}'")
    return playwright.chromium.launch(headless=True, args=args)


def unmatched_expectations_wait(page, run: BrowserRun, patterns: list[str], timeout: float,
                                now: Callable[[], float] = time.monotonic) -> None:
    """Waits (with the page's timer, which handles the console events) until every pattern has matched a console message or
    `timeout` seconds are gone."""
    deadline = now() + timeout
    while unmatched_patterns(run.console, patterns) and now() < deadline:
        page.wait_for_timeout(POLL_INTERVAL_SECONDS * 1000)


def read_page_info(page) -> PageInfo | None:
    try:
        return PageInfo.from_page(page.evaluate(PAGE_INFO_SCRIPT))
    except Exception:  # the page was closed or has no canvas any more: the other checks say so
        return None


def press(page, x: float, y: float, touch: bool) -> None:
    """The click of check 3: a mouse click, or a tap with --touch."""
    if touch:
        page.touchscreen.tap(x, y)
    else:
        page.mouse.click(x, y)


def run_browser(playwright_api, url: str, options: Options) -> BrowserRun:
    """Opens `url`, takes the screenshots and collects the console; does not judge (evaluate() does).

    Raises BrowserMissing when the browser the options name is not there; any other failure to start it, like a failure of the
    page, is an ordinary exception (the smoke FAILS)."""
    run = BrowserRun(check_canvas_size=not options.selftest, touch=options.touch)
    timeout_ms = options.timeout * 1000
    with playwright_api.sync_playwright() as playwright:
        browser = launch_browser(playwright, options)
        try:
            width, height = options.viewport
            context = browser.new_context(viewport={"width": width, "height": height},
                                          device_scale_factor=options.device_scale_factor, has_touch=options.touch)
            page = context.new_page()
            page.on("console", lambda message: run.console.append(
                ConsoleEntry(message.type, message.text, (message.location or {}).get("url", ""))))
            page.on("pageerror", lambda error: run.page_errors.append(str(error)))
            page.on("crash", lambda *_: run.page_errors.append("the page crashed"))

            response = page.goto(url, wait_until="load", timeout=timeout_ms)
            run.status = response.status if response else None
            canvas = page.locator(CANVAS_SELECTOR).first
            try:
                canvas.wait_for(state="visible", timeout=timeout_ms)
            except Exception as error:
                run.problems.append(f"(2) no visible {CANVAS_SELECTOR} within {options.timeout:g} s: {str(error).splitlines()[0]}")
                return run

            # The polls wait with the page's own timer: the sync API handles the console and pageerror events only inside its calls
            sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731
            run.first_png, drawn = poll(canvas.screenshot, png_is_drawn, options.timeout, sleep=sleep)
            if not drawn:
                return run  # evaluate() reports the flat or unreadable first frame
            run.page_info = read_page_info(page)
            if options.expect_console:
                unmatched_expectations_wait(page, run, options.expect_console, options.timeout)
            page.wait_for_timeout(options.settle * 1000)

            box = canvas.bounding_box()
            if box is None:
                run.problems.append("(3) the canvas has no box to click in")
                return run
            x, y = options.click if options.click else (box["width"] / 2, box["height"] / 2)
            if x >= box["width"] or y >= box["height"]:
                run.problems.append(f"(3) the click point {x:g},{y:g} is outside the canvas ({box['width']:g}x{box['height']:g})")
                return run

            # The pointer goes to the click point before anything is compared, so a hover effect is in every shot and cannot pass
            # for the effect of the click (a touch screen has no pointer to move)
            if not options.touch:
                page.mouse.move(box["x"] + x, box["y"] + y)
            page.wait_for_timeout(options.settle * 1000)

            # A canvas that moves by itself cannot show whether a click did anything. Watch it with no click for as long as it is
            # watched after the click, and stop at the first shot that differs from the first one
            run.idle_png = canvas.screenshot()
            try:
                idle = decode(run.idle_png, "(3) the shot before the click")
                run.before_png, _ = poll(canvas.screenshot, lambda png: shot_ends_idle_wait(idle, png),
                                         options.settle + CLICK_TIMEOUT_SECONDS, sleep=sleep)
                last = decode(run.before_png, "(3) the shot before the click")
            except SmokeFailure as error:
                run.problems.append(str(error))
                return run
            if images_differ(idle, last):
                run.problems.append(f"(3) cannot judge: the canvas changes by itself (the shots over {options.settle + CLICK_TIMEOUT_SECONDS:g} s "
                                    "with no click differ); no click was made (raise --settle if the game is still starting)")
                return run

            press(page, box["x"] + x, box["y"] + y, options.touch)
            page.wait_for_timeout(options.settle * 1000)

            run.second_png, _ = poll(canvas.screenshot, lambda png: png_differs_from(last, png), CLICK_TIMEOUT_SECONDS, sleep=sleep)
        finally:
            browser.close()
    return run


def shot_ends_idle_wait(idle: PngImage, png: bytes) -> bool:
    """True for a shot that ends the wait with no click: a different picture, or one that cannot be read (the caller tells them apart)."""
    try:
        return images_differ(idle, read_png(png))
    except PngError:
        return True


def png_differs_from(first: PngImage, png: bytes) -> bool:
    try:
        return images_differ(first, read_png(png))
    except PngError:
        return False


def describe_page(run: BrowserRun) -> list[str]:
    """The report lines about the page: input kind, isolation, SharedArrayBuffer, pixel ratio and canvas sizes."""
    lines = [f"input: {'touch (a tap)' if run.touch else 'mouse (a click)'}"]
    info = run.page_info
    if info is None:
        lines.append("page: not read (the run stopped before the first frame)")
        return lines
    lines.append(f"page: crossOriginIsolated={str(info.cross_origin_isolated).lower()}, typeof SharedArrayBuffer={info.shared_array_buffer}")
    lines.append(f"canvas: devicePixelRatio={info.device_pixel_ratio:g}, CSS size {info.css_width:g}x{info.css_height:g}, "
                 f"backing store {info.canvas_width}x{info.canvas_height}")
    return lines


def run_smoke(options: Options, playwright_api) -> int:
    with tempfile.TemporaryDirectory(prefix="web_smoke_") as temp:
        root = write_selftest_site(Path(temp), options.selftest_fault) if options.selftest else options.dist
        if not (root / PAGE_NAME).is_file():
            print(f"FAIL: {root / PAGE_NAME} not found: package a web build first (tools/web/package.py)")
            return EXIT_FAILED
        with served(root) as url:
            print(f"web_smoke: {root} on {url}", flush=True)
            try:
                run = run_browser(playwright_api, url, options)
            except BrowserMissing as error:
                named = options.browser_channel or options.executable_path
                print(f"SKIP: the browser is not installed ({'the one you named' if named else 'playwright install chromium'}): {error}")
                return EXIT_SKIPPED
            except Exception as error:  # a timeout of goto, a closed browser: the page did not come up
                print(f"FAIL: (1) the browser run failed: {str(error).strip().splitlines()[0] if str(error).strip() else type(error).__name__}")
                return EXIT_FAILED
    for line in describe_page(run):
        print(line)
    problems = evaluate(run, options.allow_console, options.expect_console)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return EXIT_FAILED
    errors = sum(1 for entry in run.console if entry.kind == "error")
    print("ok: (1) the page loaded with HTTP 200 and no page error")
    print("ok: (2) the first frame is drawn (not one flat color)")
    print(f"ok: (3) the canvas changed after the {'tap' if run.touch else 'click'}")
    expected = f", {len(options.expect_console)} expected message(s) seen" if options.expect_console else ""
    print(f"ok: (4) no console error outside the allow-list ({errors} allowed, {len(run.console)} messages{expected})")
    if not run.check_canvas_size:
        print("skip: (5) canvas size: the selftest page is not the web shell (its canvas has a fixed size)")
    elif run.page_info is None:
        print("skip: (5) canvas size: the page could not be read")
    else:
        info = run.page_info
        print(f"ok: (5) the backing store {info.canvas_width}x{info.canvas_height} is the CSS size {info.css_width:g}x{info.css_height:g} "
              f"x devicePixelRatio {info.device_pixel_ratio:g}")
    print("PASS")
    return EXIT_PASSED


def main(argv: list[str] | None = None, load: Callable = load_playwright) -> int:
    options = parse_options(argv)
    playwright_api = load()
    if playwright_api is None:
        print("SKIP: playwright is not installed")
        return EXIT_SKIPPED
    return run_smoke(options, playwright_api)


if __name__ == "__main__":
    sys.exit(main())
