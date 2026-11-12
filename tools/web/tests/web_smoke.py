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
     pixel (--device-scale-factor N sets the ratio of the browser). Skipped, with the reason printed, for the --selftest page;
  6. the web app manifest: `manifest.webmanifest` is served as `application/manifest+json`, parses, says `display: fullscreen` and
     lists icons, and every icon is served as `image/png` (plain HTTP requests to the same server, no browser; the generated
     --selftest site has a manifest of its own, so the check runs there too);
  7. the page posts a match log: the page's own `window.bagiWebPost` (web/shell.html) posts one piece, `{"event":"start","version":N}`,
     to /matches/<fresh id>/0 of the smoke's serve.py (which has a temporary match store), polls to HTTP 200 within UPLOAD_TIMEOUT_SECONDS,
     and the file in the store holds the same bytes. A page without `bagiWebPost` fails. --no-upload-check leaves it out (a dist built
     before the call existed). The --selftest page carries the real script, cut out of web/shell.html by its id.

  8. the save folder survives a reload (web/saves.js, web/CLAUDE.md), in three runs of a browser of their own, each in a fresh profile.
     (a) The page's `Module.bagiSaves.state` is "persistent"; `/bagi/saves/smoke.save` is written through `Module.FS`, `Module.bagiSaves.flush`
     answers with no error, the page is reloaded, the game draws again, and the file is read back byte for byte. (b) and (c): the page
     with IndexedDB made to throw on `open`, and with `open` that never answers (the start then waits for the time-out of saves.js):
     the page still starts (a drawn frame), the state is "memory", a file can still be written, and the console has no error outside the
     allow-list. A dist whose page has no `Module.bagiSaves` or no `Module.FS` (built before saves.js, or without it) fails. The
     --selftest page has no module, and --no-saves-check leaves the check out.

The page is opened with `?fullscreen=0` (the switch of the shell, web/shell.html): without it the first tap of --touch would ask for
fullscreen and resize the canvas under checks 3 and 5. So the smoke does not exercise the first-tap request, the toggle button or the
orientation lock (a real phone does).

The report also prints `window.crossOriginIsolated`, `typeof SharedArrayBuffer`, `devicePixelRatio`, the CSS size and the backing
store of the canvas, and whether the input was a click or a tap. The server is serve.py's own, with the isolation decision it
makes for a real run (switch, then manifest.json `threads`, then on): a threaded dist/ is served with the two headers.

--selftest serves a page generated in a temp folder (a canvas that redraws the same still pattern on every animation frame and a
moving one after a click, with a manifest.json of `threads: false`) instead of a build, so the logic of this script runs without a
wasm build. --selftest-fault breaks that page on purpose (flat, console-error, page-error, deaf, moving, upload): the run has to exit 1,
which shows each of the checks can fail and check 3 can fail in both ways (deaf: the click changes nothing; moving: the canvas
changes by itself, "cannot judge"; upload: the server has no match store, so check 7 fails). Add --timeout 5 for flat, which waits for
a frame that never comes.

The browser: Playwright's Chromium by default (`playwright install chromium`), --browser-channel NAME for an installed one
(`msedge`, `chrome`), --executable-path FILE for any Chromium-based executable. Exit code: 0 passed, 1 failed (the reason is
printed), 77 skipped (Playwright is not installed, or the named browser is absent), 2 a wrong command line.

Limits: check 3 needs a canvas that stands still, with the pointer on the click point, for --settle + 5 s before the click (a game
with an idle animation, a fade-in that is not over by then or a cursor that blinks fails with "cannot judge"; the idle shots are about
0.5 s apart, so a blink short enough to fall between two shots is not seen, and a change slower than the whole window is not seen
and can pass for the click's; raise --settle for a start that is slow). The screenshots are decoded by
tools/control/tests/png_reader.py (imported from there, not copied), in plain Python: keep the viewport small.
Run against a real Chromium and Edge with --selftest and every --selftest-fault, and against the page of BagiMatch3 (tools/web/CLAUDE.md).
"""

import argparse
import importlib
import json
import re
import secrets
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

TOOLS_DIR = Path(__file__).resolve().parents[2]
SHELL_PATH = TOOLS_DIR.parent / "web" / "shell.html"
# serve.py is in tools/web, png_reader.py in tools/control/tests
sys.path.insert(0, str(TOOLS_DIR / "web"))
sys.path.insert(0, str(TOOLS_DIR / "control" / "tests"))

import package  # noqa: E402  (flat_color_png: the icon of the selftest site)
import serve  # noqa: E402
from png_reader import PngError, PngImage, read_png  # noqa: E402

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
EXIT_SKIPPED = 77

PAGE_NAME = "index.html"
PAGE_QUERY = "?fullscreen=0"  # the shell's switch: no automatic fullscreen request on the first tap (it would resize the canvas of checks 3 and 5)
WEB_MANIFEST_NAME = package.WEB_MANIFEST_NAME
WEB_MANIFEST_TYPE = "application/manifest+json"
ICON_TYPE = "image/png"
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
# Check 8: the file written and read back, the longest wait for the flush to be answered, the two ways IndexedDB is made to fail (an
# init script of the page replaces IDBFactory.open), and how long the game may need to start after the time-out of saves.js (5 s there)
SAVES_FILE_PATH = "/bagi/saves/smoke.save"
SAVES_FILE_SIZE = 32
SAVES_FLUSH_TIMEOUT_SECONDS = 15.0
SAVES_REFUSED_MODES = ("throws", "silent")
SAVES_REFUSED_SCRIPTS = {
    "throws": "IDBFactory.prototype.open = function () { throw new DOMException('refused on purpose', 'SecurityError'); };",
    "silent": "IDBFactory.prototype.open = function () { return {}; };",
}
SELFTEST_FAULTS = ("none", "flat", "console-error", "page-error", "deaf", "moving", "upload")
SELFTEST_THREADED = False  # the generated dist/ is single-thread: its manifest.json says so, and serve.py then sends no isolation headers
# Check 7: the version a smoke piece names (the smoke's store accepts exactly it), the wait for the answer, the poll step in the page
SMOKE_LOG_VERSION = 1
UPLOAD_TIMEOUT_SECONDS = 10.0
UPLOAD_POLL_MS = 20
UPLOAD_SCRIPT_ID = "bagi-web-post"  # the id of the page script in web/shell.html
UPLOAD_SCRIPT_PATTERN = re.compile(r'<script id="' + UPLOAD_SCRIPT_ID + r'">(.*?)</script>', re.DOTALL)
# Runs in the page (page.evaluate with [path, body, timeoutMs, pollMs]): posts through the page's own call and waits for the status
UPLOAD_PAGE_SCRIPT = """async ([path, body, timeoutMs, pollMs]) => {
    const api = window.bagiWebPost;
    if (!api) {
        return {error: "the page has no window.bagiWebPost"};
    }
    const id = api.start(path, body);
    if (!id) {
        return {error: "bagiWebPost.start refused the post"};
    }
    const deadline = Date.now() + timeoutMs;
    for (;;) {
        const status = api.poll(id);
        if (status !== -1) {
            return {status: status};
        }
        if (Date.now() > deadline) {
            return {error: "no answer within " + timeoutMs / 1000 + " s"};
        }
        await new Promise((resolve) => setTimeout(resolve, pollMs));
    }
}"""
# Run in the page (page.evaluate): the state of the save folder and, when it is persistent, a write through Module.FS and a flush
SAVES_WRITE_SCRIPT = """async ([path, bytes, timeoutMs]) => {
    const saves = window.Module && window.Module.bagiSaves;
    if (!saves) {
        return {error: "the page has no Module.bagiSaves (built without web/saves.js?)"};
    }
    if (!window.Module.FS) {
        return {error: "Module.FS is not exported (-sEXPORTED_RUNTIME_METHODS=FS)"};
    }
    const state = saves.state;
    try {
        window.Module.FS.writeFile(path, new Uint8Array(bytes));
    } catch (error) {
        return {state: state, error: "the file could not be written: " + error};
    }
    if (state !== "persistent") {
        return {state: state, flush: null};
    }
    const flush = await new Promise((resolve) => {
        const timer = setTimeout(() => resolve("no answer within " + timeoutMs / 1000 + " s"), timeoutMs);
        saves.flush((error) => { clearTimeout(timer); resolve(error); });
    });
    return {state: state, flush: flush};
}"""
SAVES_READ_SCRIPT = """(path) => {
    const saves = window.Module && window.Module.bagiSaves;
    if (!saves || !window.Module.FS) {
        return {error: "the page has no Module.bagiSaves or Module.FS after the reload"};
    }
    try {
        return {state: saves.state, bytes: Array.from(window.Module.FS.readFile(path))};
    } catch (error) {
        return {state: saves.state, error: "the file is not there: " + error};
    }
}"""
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
    early_info: PageInfo | None = None  # read as soon as the canvas is visible: the report when the game never draws (sizes are not final)
    check_canvas_size: bool = False  # check 5 applies (not to the selftest page)
    touch: bool = False  # the click of check 3 was a tap
    web_manifest_problems: list[str] | None = None  # check 6, made by check_web_manifest (None: not made)
    upload_problems: list[str] | None = None  # check 7, made by check_upload (None: not made)
    saves_problems: list[str] | None = None  # check 8, made by check_saves (None: not made)


@dataclass
class SavesPersistRun:
    """Check 8 (a): what the page showed across a write, a flush and a reload."""

    written: list[int] = field(default_factory=list)
    state: str | None = None  # Module.bagiSaves.state before the reload
    flush: str | None = None  # None: the flush answered with no error, else why not
    restarted: bool = False  # the game drew a frame after the reload
    state_after: str | None = None
    read_back: list[int] | None = None
    error: str | None = None  # the page could not be driven (no Module.bagiSaves, Module.FS, file)
    console: list[ConsoleEntry] = field(default_factory=list)
    page_errors: list[str] = field(default_factory=list)


@dataclass
class SavesRefusedRun:
    """Check 8 (b), (c): the page started with IndexedDB made to fail in the way `mode` names."""

    mode: str
    started: bool = False  # the game drew a frame
    state: str | None = None
    error: str | None = None
    console: list[ConsoleEntry] = field(default_factory=list)
    page_errors: list[str] = field(default_factory=list)


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
    upload_check: bool = True
    saves_check: bool = True


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
    parser = argparse.ArgumentParser(description="Open a web build in a headless Chromium-based browser (Chromium, Edge) and check that it starts and answers a click or a tap.")
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
    parser.add_argument("--no-upload-check", action="store_true",
                        help="leave out check 7 (the page posts a match log piece): for a dist built before window.bagiWebPost existed")
    parser.add_argument("--no-saves-check", action="store_true",
                        help="leave out check 8 (the save folder survives a reload): for a dist built without web/saves.js; the --selftest page has no module")
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
                   args.touch, args.expect_console, not args.no_upload_check, not args.no_saves_check and not args.selftest)


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
    """The reasons the run failed, one line each; empty when it passed. The checks of the module comment, in order."""
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

    # A first frame was drawn, so the page is there and its sizes are final: for a real build the page not being readable is a failure,
    # because a pass would claim a canvas size nobody looked at
    drawn = first is not None and not is_flat(first)
    if run.check_canvas_size and drawn:
        if run.page_info is None:
            problems.append("(5) the page could not be read, so the canvas size was not checked")
        else:
            problem = canvas_size_problem(run.page_info)
            if problem:
                problems.append(problem)
    if run.web_manifest_problems:
        problems.extend(run.web_manifest_problems)
    if run.upload_problems:
        problems.extend(run.upload_problems)
    if run.saves_problems:
        problems.extend(run.saves_problems)
    return problems


def console_error_problems(prefix: str, console: list[ConsoleEntry], page_errors: list[str], allow: list[str]) -> list[str]:
    problems = [f"{prefix} uncaught error in the page: {error}" for error in page_errors]
    for entry in console:
        if entry.kind == "error" and not console_error_allowed(entry, allow):
            where = f" ({entry.url})" if entry.url else ""
            problems.append(f"{prefix} console error: {entry.text}{where}")
    return problems


def check_saves(persist: SavesPersistRun, refused: list[SavesRefusedRun], allow_console: list[str] | None = None) -> list[str]:
    """Check 8 on what the three browser runs showed: the reasons, empty when the file written before the reload is there after it and
    the page starts, in state "memory", with IndexedDB refused in each of the ways."""
    allow = allow_console or []
    problems = []
    label = "(8a) reload"
    if persist.error:
        problems.append(f"{label}: {persist.error}")
    elif persist.state != "persistent":
        problems.append(f"{label}: Module.bagiSaves.state is {persist.state!r}, not 'persistent': IndexedDB was refused in this browser, "
                        "or did not answer in time")
    elif persist.flush is not None:
        problems.append(f"{label}: Module.bagiSaves.flush did not succeed: {persist.flush}")
    elif not persist.restarted:
        problems.append(f"{label}: the game did not draw a frame after the reload")
    elif persist.state_after != "persistent":
        problems.append(f"{label}: Module.bagiSaves.state is {persist.state_after!r} after the reload, not 'persistent'")
    elif persist.read_back is None:
        problems.append(f"{label}: the file written before the reload is not there after it")
    elif persist.read_back != persist.written:
        problems.append(f"{label}: the file after the reload differs from the one written ({len(persist.read_back)} bytes, "
                        f"{len(persist.written)} written)")
    problems.extend(console_error_problems(label + ":", persist.console, persist.page_errors, allow))
    for run in refused:
        label = f"(8, IndexedDB {run.mode})"
        if run.error:
            problems.append(f"{label}: {run.error}")
        elif not run.started:
            problems.append(f"{label}: the page did not start (no frame was drawn): a refused IndexedDB must not stop the game")
        elif run.state != "memory":
            problems.append(f"{label}: Module.bagiSaves.state is {run.state!r}, not 'memory'")
        problems.extend(console_error_problems(label + ":", run.console, run.page_errors, allow))
    return problems


def check_web_manifest(fetch: Callable[[str], tuple[int, str, bytes]]) -> list[str]:
    """Check 6. `fetch(path)` answers (HTTP status, Content-Type, body) of a path of the served folder. Returns the reasons, empty when
    the manifest is served with its type, parses, says `display: fullscreen`, lists icons, and every icon is served as a PNG."""
    status, content_type, body = fetch(WEB_MANIFEST_NAME)
    if status != 200:
        return [f"(6) {WEB_MANIFEST_NAME} answered HTTP {status}, not 200: package the build with tools/web/package.py"]
    problems = []
    if content_type.split(";")[0].strip().lower() != WEB_MANIFEST_TYPE:
        problems.append(f"(6) {WEB_MANIFEST_NAME} is served as '{content_type}', not {WEB_MANIFEST_TYPE}")
    try:
        manifest = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        return [*problems, f"(6) {WEB_MANIFEST_NAME} is not JSON: {error}"]
    if not isinstance(manifest, dict):
        return [*problems, f"(6) {WEB_MANIFEST_NAME} is not a JSON object"]
    if manifest.get("display") != "fullscreen":
        problems.append(f"(6) {WEB_MANIFEST_NAME}: display is {manifest.get('display')!r}, not 'fullscreen'")
    icons = manifest.get("icons")
    if not isinstance(icons, list) or not icons:
        return [*problems, f"(6) {WEB_MANIFEST_NAME} lists no icons: Chrome would not offer to install the page"]
    for icon in icons:
        source = icon.get("src") if isinstance(icon, dict) else None
        if not isinstance(source, str) or not source:
            problems.append(f"(6) an icon of {WEB_MANIFEST_NAME} has no 'src'")
            continue
        # relative to the manifest, which is in the root, as the browser resolves it
        path = urllib.parse.urljoin(WEB_MANIFEST_NAME, source)
        icon_status, icon_type, icon_body = fetch(path)
        if icon_status != 200:
            problems.append(f"(6) icon {source} answered HTTP {icon_status}, not 200")
        elif icon_type.split(";")[0].strip().lower() != ICON_TYPE or not icon_body.startswith(package.PNG_SIGNATURE):
            problems.append(f"(6) icon {source} is not served as a PNG (Content-Type '{icon_type}')")
    return problems


def check_upload(post: Callable[[str, str], dict], store: Path, version: int = SMOKE_LOG_VERSION) -> list[str]:
    """Check 7. `post(path, body)` posts through the page's `bagiWebPost` and answers {"status": N} or {"error": text}. Returns the
    reasons, empty when the post got HTTP 200 and `store` (the match store of the server) holds the body, byte for byte, as piece 0."""
    match_id = secrets.token_hex(16)
    body = json.dumps({"event": "start", "version": version}, separators=(",", ":")) + "\n"
    try:
        answer = post(f"/matches/{match_id}/0", body)
    except Exception as error:  # the page was closed or the script threw
        return [f"(7) the post could not be run in the page: {str(error).strip().splitlines()[0] if str(error).strip() else type(error).__name__}"]
    if not isinstance(answer, dict) or "error" in answer:
        return [f"(7) the page could not post: {answer.get('error') if isinstance(answer, dict) else answer!r}"]
    if answer.get("status") != 200:
        return [f"(7) the post was answered with HTTP {answer.get('status')}, not 200: the server has no match store, or the version "
                "is outside its MATCH_LOG_VERSIONS"]
    try:
        stored = (store / match_id / "0.jsonl").read_bytes()
    except OSError:
        return ["(7) the post was answered with HTTP 200 but the piece is not in the match store"]
    if stored != body.encode("utf-8"):
        return ["(7) the stored piece differs from the body that was posted"]
    return []


def fetch_from(base_url: str) -> Callable[[str], tuple[int, str, bytes]]:
    """The `fetch` of check_web_manifest for the server at `base_url` (http://host:port/): plain GET requests, no browser."""
    def fetch(path: str) -> tuple[int, str, bytes]:
        try:
            with urllib.request.urlopen(urllib.parse.urljoin(base_url, path), timeout=10) as response:
                return response.status, response.headers.get("Content-Type", ""), response.read()
        except urllib.error.HTTPError as error:
            with error:  # it holds the connection open until closed
                return error.code, error.headers.get("Content-Type", ""), b""
    return fetch


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

def web_post_script() -> str:
    """The text of the page script `window.bagiWebPost` of web/shell.html, found by its id, so the selftest page cannot drift from it."""
    found = UPLOAD_SCRIPT_PATTERN.search(SHELL_PATH.read_text(encoding="utf-8"))
    if not found:
        raise SmokeFailure(f"{SHELL_PATH} has no <script id=\"{UPLOAD_SCRIPT_ID}\">")
    return found.group(1)


def selftest_page(fault: str = "none") -> str:
    """A page for the checks: a canvas that redraws the same still stripes on every animation frame (so the picture before a click
    does not depend on the frame) and other stripes (another palette, a different direction, moving) after a click. `fault` breaks
    it on purpose: a flat canvas, an error in the console, an uncaught error, a canvas that does not hear the click, a canvas that
    moves by itself before any click. The page carries the real bagiWebPost script; the fault `upload` is in the server, not here."""
    flat = "true" if fault == "flat" else "false"
    post_script = web_post_script()
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
<script id="{UPLOAD_SCRIPT_ID}">{post_script}</script>
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
    """index.html, a manifest.json that says `threads: false` (serve.py then decides on no isolation headers, as for a real build) and
    a web app manifest with one icon, for check 6."""
    folder.mkdir(parents=True, exist_ok=True)
    (folder / PAGE_NAME).write_text(selftest_page(fault), encoding="utf-8")
    icon = f"{package.ICONS_FOLDER}/selftest.png"
    (folder / package.ICONS_FOLDER).mkdir(exist_ok=True)
    (folder / icon).write_bytes(package.flat_color_png(192, package.DEFAULT_COLOR))
    web_manifest = {"name": "selftest", "display": "fullscreen", "icons": [{"src": icon, "sizes": "192x192", "type": ICON_TYPE}]}
    (folder / WEB_MANIFEST_NAME).write_text(json.dumps(web_manifest), encoding="utf-8")
    manifest = {"game": "selftest", "threads": SELFTEST_THREADED, "files": {}}
    (folder / serve.MANIFEST_NAME).write_text(json.dumps(manifest), encoding="utf-8")
    return folder


# ----- serving -----

@contextmanager
def served(root: Path, match_store: Path | None = None):
    """serve.py's server for `root` on a free port of 127.0.0.1 in a thread; yields its base URL and stops it afterwards. The
    isolation headers are decided like serve.py does with no switch and no variable: manifest.json of `root`, else on. With
    `match_store` the match endpoint is on, for the one version SMOKE_LOG_VERSION and with no auth (the smoke does not test it)."""
    isolation, reason = serve.decide_cross_origin_isolation(None, None, root)
    print(f"web_smoke: cross-origin isolation {'on' if isolation else 'off'}: {reason}", flush=True)
    versions = (SMOKE_LOG_VERSION, SMOKE_LOG_VERSION) if match_store is not None else None
    server = serve.make_server(root, "127.0.0.1", 0, isolation, quiet=True, match_store=match_store, match_versions=versions)
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


def wait_for_expected_console(page, run: BrowserRun, patterns: list[str], timeout: float,
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


def run_browser(playwright_api, url: str, options: Options, upload_store: Path | None = None) -> BrowserRun:
    """Opens `url`, takes the screenshots and collects the console; does not judge (evaluate() does). `upload_store` is the match
    store of the server at `url`, where check 7 looks for the piece the page posts (no check without it or with --no-upload-check).

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

            response = page.goto(url + PAGE_QUERY, wait_until="load", timeout=timeout_ms)
            run.status = response.status if response else None
            canvas = page.locator(CANVAS_SELECTOR).first
            try:
                canvas.wait_for(state="visible", timeout=timeout_ms)
            except Exception as error:
                run.problems.append(f"(2) no visible {CANVAS_SELECTOR} within {options.timeout:g} s: {str(error).splitlines()[0]}")
                return run

            # The isolation and SharedArrayBuffer of the page do not depend on the game starting: a threaded page served without the
            # headers never draws, and this is the only reading that run gets
            run.early_info = read_page_info(page)

            # The polls wait with the page's own timer: the sync API handles the console and pageerror events only inside its calls
            sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731
            run.first_png, drawn = poll(canvas.screenshot, png_is_drawn, options.timeout, sleep=sleep)
            if not drawn:
                return run  # evaluate() reports the flat or unreadable first frame
            run.page_info = read_page_info(page)
            if upload_store is not None and options.upload_check:
                run.upload_problems = check_upload(lambda path, body: page.evaluate(
                    UPLOAD_PAGE_SCRIPT, [path, body, UPLOAD_TIMEOUT_SECONDS * 1000, UPLOAD_POLL_MS]), upload_store)
            if options.expect_console:
                wait_for_expected_console(page, run, options.expect_console, options.timeout)
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


def open_game_page(browser, url: str, options: Options, console: list[ConsoleEntry], page_errors: list[str], init_script: str | None = None):
    """A page in a context of its own (a fresh profile, so no IndexedDB of an earlier run), console and errors collected into the
    lists. Returns (page, canvas)."""
    width, height = options.viewport
    context = browser.new_context(viewport={"width": width, "height": height}, device_scale_factor=options.device_scale_factor)
    if init_script:
        context.add_init_script(init_script)
    page = context.new_page()
    page.on("console", lambda message: console.append(ConsoleEntry(message.type, message.text, (message.location or {}).get("url", ""))))
    page.on("pageerror", lambda error: page_errors.append(str(error)))
    page.on("crash", lambda *_: page_errors.append("the page crashed"))
    page.goto(url + PAGE_QUERY, wait_until="load", timeout=options.timeout * 1000)
    return page, page.locator(CANVAS_SELECTOR).first


def game_draws(page, canvas, timeout: float) -> bool:
    """Waits (with the page's timer) for a frame that is not one flat color."""
    try:
        canvas.wait_for(state="visible", timeout=timeout * 1000)
    except Exception:
        return False
    _, drawn = poll(canvas.screenshot, png_is_drawn, timeout, sleep=lambda seconds: page.wait_for_timeout(seconds * 1000))
    return drawn


def run_saves_persistence(browser, url: str, options: Options) -> SavesPersistRun:
    """Check 8 (a): write a file, flush, reload, read it back."""
    run = SavesPersistRun(written=list(secrets.token_bytes(SAVES_FILE_SIZE)))
    page, canvas = open_game_page(browser, url, options, run.console, run.page_errors)
    if not game_draws(page, canvas, options.timeout):
        run.error = "the game did not draw a frame, so the save folder was not tried"
        return run
    answer = page.evaluate(SAVES_WRITE_SCRIPT, [SAVES_FILE_PATH, run.written, SAVES_FLUSH_TIMEOUT_SECONDS * 1000])
    if "error" in answer:
        run.error = answer["error"]
        return run
    run.state = answer.get("state")
    run.flush = answer.get("flush")
    if run.state != "persistent" or run.flush is not None:
        return run
    page.reload(wait_until="load", timeout=options.timeout * 1000)
    run.restarted = game_draws(page, canvas, options.timeout)
    if not run.restarted:
        return run
    answer = page.evaluate(SAVES_READ_SCRIPT, SAVES_FILE_PATH)
    run.state_after = answer.get("state")
    if "bytes" in answer:
        run.read_back = answer["bytes"]
    elif run.state_after is None:
        run.error = answer.get("error")
    return run


def run_saves_refused(browser, url: str, options: Options, mode: str) -> SavesRefusedRun:
    """Check 8 (b), (c): the page starts with IndexedDB failing as `mode` says and keeps its saves in memory."""
    run = SavesRefusedRun(mode)
    page, canvas = open_game_page(browser, url, options, run.console, run.page_errors, SAVES_REFUSED_SCRIPTS[mode])
    run.started = game_draws(page, canvas, options.timeout)
    if not run.started:
        return run
    answer = page.evaluate(SAVES_WRITE_SCRIPT, [SAVES_FILE_PATH, list(range(SAVES_FILE_SIZE)), SAVES_FLUSH_TIMEOUT_SECONDS * 1000])
    run.state = answer.get("state")
    run.error = answer.get("error")
    return run


def run_saves_checks(playwright_api, url: str, options: Options) -> list[str]:
    """Check 8: runs the three browsers of the module comment against the page at `url` and judges them."""
    with playwright_api.sync_playwright() as playwright:
        browser = launch_browser(playwright, options)
        try:
            persist = run_saves_persistence(browser, url, options)
            refused = [run_saves_refused(browser, url, options, mode) for mode in SAVES_REFUSED_MODES]
        finally:
            browser.close()
    return check_saves(persist, refused, options.allow_console)


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
    if info is None and run.early_info is not None:
        early = run.early_info
        lines.append(f"page, before the first frame: crossOriginIsolated={str(early.cross_origin_isolated).lower()}, "
                     f"typeof SharedArrayBuffer={early.shared_array_buffer}")
        lines.append("canvas: not read after the first frame (the run stopped before it)")
        return lines
    if info is None:
        lines.append("page: not read (no page information was collected: the page was closed, or the run failed, before it could be read)")
        return lines
    lines.append(f"page: crossOriginIsolated={str(info.cross_origin_isolated).lower()}, typeof SharedArrayBuffer={info.shared_array_buffer}")
    lines.append(f"canvas: devicePixelRatio={info.device_pixel_ratio:g}, CSS size {info.css_width:g}x{info.css_height:g}, "
                 f"backing store {info.canvas_width}x{info.canvas_height}")
    return lines


def run_smoke(options: Options, playwright_api) -> int:
    with tempfile.TemporaryDirectory(prefix="web_smoke_") as temp, tempfile.TemporaryDirectory(prefix="web_smoke_store_") as store_temp:
        root = write_selftest_site(Path(temp), options.selftest_fault) if options.selftest else options.dist
        if not (root / PAGE_NAME).is_file():
            print(f"FAIL: {root / PAGE_NAME} not found: package a web build first (tools/web/package.py)")
            return EXIT_FAILED
        store = Path(store_temp) / "matches"
        # The fault `upload`: the server has no store, so the post of check 7 cannot be kept
        served_store = store if options.upload_check and options.selftest_fault != "upload" else None
        with served(root, served_store) as url:
            print(f"web_smoke: {root} on {url}", flush=True)
            try:
                run = run_browser(playwright_api, url, options, store if options.upload_check else None)
                run.web_manifest_problems = check_web_manifest(fetch_from(url))
                if options.saves_check:
                    run.saves_problems = run_saves_checks(playwright_api, url, options)
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
    else:
        info = run.page_info
        print(f"ok: (5) the backing store {info.canvas_width}x{info.canvas_height} is the CSS size {info.css_width:g}x{info.css_height:g} "
              f"x devicePixelRatio {info.device_pixel_ratio:g}")
    print(f"ok: (6) {WEB_MANIFEST_NAME} is served with its type, says display fullscreen, and its icons are PNGs")
    if options.upload_check:
        print("ok: (7) the page posted a match log piece through window.bagiWebPost and the server stored it byte for byte")
    else:
        print("skip: (7) the match log post (--no-upload-check)")
    if options.saves_check:
        print("ok: (8) a file written, flushed and reloaded is read back; with IndexedDB refused the page still starts, in state memory")
    else:
        print("skip: (8) the save folder (--no-saves-check, or the --selftest page, which has no module)")
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
