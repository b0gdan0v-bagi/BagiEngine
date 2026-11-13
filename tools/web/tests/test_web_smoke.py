"""Tests of tools/web/tests/web_smoke.py. Everything that needs no browser is tested here: the command line, the decisions on PNG
bytes, the allow-list, the polling, the generated page, and the orchestration against a fake Playwright (which checks the order of
the calls, not the real API). The class at the end runs the script in a real Chromium and is skipped where Playwright is missing
(tools/web/tests/CLAUDE.md: the venv `build/web-venv`).
Run: python -m unittest discover tools/web/tests"""

import contextlib
import http.client
import io
import json
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import zlib
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import web_fixtures  # puts tools/web on sys.path

import package
import web_smoke
from web_smoke import BrowserRun, ConsoleEntry, PageInfo

NODE = shutil.which("node")


def make_png(width: int, height: int, pixel, channels: int = 3) -> bytes:
    """A PNG (8 bits, RGB or RGBA, filter 0) whose pixel (x, y) is the tuple pixel(x, y), written with the standard library."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw.extend(pixel(x, y)[:channels])

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", width, height, 8, 2 if channels == 3 else 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b"")


FLAT = make_png(8, 6, lambda x, y: (10, 20, 30))
FLAT_RGBA = make_png(8, 6, lambda x, y: (10, 20, 30, 255), channels=4)
STRIPES = make_png(8, 6, lambda x, y: (200, 0, 0) if x < 4 else (0, 0, 200))
OTHER_STRIPES = make_png(8, 6, lambda x, y: (0, 200, 0) if y < 3 else (0, 0, 200))
ONE_PIXEL_OFF = make_png(8, 6, lambda x, y: (11, 20, 30) if (x, y) == (7, 5) else (10, 20, 30))


def http_get(url: str, path: str = "/"):
    """GET `path` of the server at `url` (http://host:port/): the response (its headers stay readable) and the body as text."""
    host, port = url[len("http://"):].rstrip("/").split(":")
    connection = http.client.HTTPConnection(host, int(port), timeout=10)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        return response, response.read().decode("utf-8")
    finally:
        connection.close()


def package_fake_dist(folder: Path) -> Path:
    """A dist/ of the fake build, written by package.py (the page, the package manifest, the web app manifest and its icons)."""
    out = folder / "dist"
    package.package(web_fixtures.write_build_output(folder / "build"), out, brotli_module=None)
    return out


def good_run(**changes) -> BrowserRun:
    values = dict(status=200, first_png=STRIPES, idle_png=STRIPES, before_png=STRIPES, second_png=OTHER_STRIPES)
    values.update(changes)
    return BrowserRun(**values)


class ArgumentsTest(unittest.TestCase):
    def parse(self, *argv):
        with contextlib.redirect_stderr(io.StringIO()):
            return web_smoke.parse_options(list(argv))

    def fails(self, *argv):
        with self.assertRaises(SystemExit) as caught:
            self.parse(*argv)
        self.assertEqual(caught.exception.code, 2)

    def test_defaults(self):
        options = self.parse("--dist", "dist")
        self.assertEqual(options.dist, Path("dist"))
        self.assertFalse(options.selftest)
        self.assertIsNone(options.click)
        self.assertEqual(options.viewport, web_smoke.DEFAULT_VIEWPORT)
        self.assertEqual(options.allow_console, [])
        self.assertEqual(options.selftest_fault, "none")

    def test_the_click_point_and_the_viewport_are_read(self):
        options = self.parse("--dist", "d", "--click", "320, 240.5", "--viewport", "800x600")
        self.assertEqual(options.click, (320.0, 240.5))
        self.assertEqual(options.viewport, (800, 600))

    def test_a_bad_click_or_viewport_is_exit_2(self):
        for bad in ("10", "a,b", "-1,5", "1,2,3", ""):
            with self.subTest(click=bad):
                self.fails("--dist", "d", "--click", bad)
        for bad in ("640", "0x480", "axb"):
            with self.subTest(viewport=bad):
                self.fails("--dist", "d", "--viewport", bad)

    def test_exactly_one_of_dist_and_selftest(self):
        self.fails()
        self.fails("--dist", "d", "--selftest")
        self.assertTrue(self.parse("--selftest").selftest)

    def test_a_fault_belongs_to_the_selftest(self):
        self.fails("--dist", "d", "--selftest-fault", "flat")
        self.assertEqual(self.parse("--selftest", "--selftest-fault", "flat").selftest_fault, "flat")

    def test_an_allow_pattern_must_be_a_regular_expression(self):
        self.fails("--dist", "d", "--allow-console", "(")
        self.assertEqual(self.parse("--dist", "d", "--allow-console", "a.b", "--allow-console", "c").allow_console, ["a.b", "c"])

    def test_the_timeouts_must_make_sense(self):
        self.fails("--dist", "d", "--timeout", "0")
        self.fails("--dist", "d", "--settle", "-1")

    def test_the_new_options_default_to_the_old_behaviour(self):
        options = self.parse("--dist", "d")
        self.assertIsNone(options.browser_channel)
        self.assertIsNone(options.executable_path)
        self.assertEqual(options.device_scale_factor, 1.0)
        self.assertFalse(options.touch)
        self.assertEqual(options.expect_console, [])

    def test_the_upload_check_is_on_and_no_upload_check_switches_it_off(self):
        self.assertTrue(self.parse("--dist", "d").upload_check)
        self.assertFalse(self.parse("--dist", "d", "--no-upload-check").upload_check)
        self.assertEqual(self.parse("--selftest", "--selftest-fault", "upload").selftest_fault, "upload")

    def test_the_saves_check_is_on_for_a_dist_and_off_for_no_saves_check_and_for_the_selftest_page(self):
        self.assertTrue(self.parse("--dist", "d").saves_check)
        self.assertFalse(self.parse("--dist", "d", "--no-saves-check").saves_check)
        self.assertFalse(self.parse("--selftest").saves_check)

    def test_the_browser_and_input_options_are_read(self):
        options = self.parse("--selftest", "--browser-channel", "msedge", "--device-scale-factor", "2", "--touch")
        self.assertEqual(options.browser_channel, "msedge")
        self.assertEqual(options.device_scale_factor, 2.0)
        self.assertTrue(options.touch)
        self.assertEqual(self.parse("--selftest", "--executable-path", "x/chrome.exe").executable_path, Path("x/chrome.exe"))

    def test_a_channel_and_an_executable_are_exclusive(self):
        self.fails("--selftest", "--browser-channel", "msedge", "--executable-path", "x")

    def test_the_scale_factor_must_be_positive_and_a_number(self):
        for bad in ("0", "-1", "two"):
            with self.subTest(bad):
                self.fails("--selftest", "--device-scale-factor", bad)

    def test_the_expected_console_patterns_are_repeatable_and_checked(self):
        self.fails("--selftest", "--expect-console", "(")
        self.assertEqual(self.parse("--selftest", "--expect-console", r"worker threads", "--expect-console", "b").expect_console,
                         ["worker threads", "b"])


class PictureDecisionsTest(unittest.TestCase):
    def image(self, png):
        return web_smoke.read_png(png)

    def test_one_color_is_flat_in_rgb_and_rgba(self):
        self.assertTrue(web_smoke.is_flat(self.image(FLAT)))
        self.assertTrue(web_smoke.is_flat(self.image(FLAT_RGBA)))

    def test_two_colors_are_not_flat_and_one_pixel_is_enough(self):
        self.assertFalse(web_smoke.is_flat(self.image(STRIPES)))
        self.assertFalse(web_smoke.is_flat(self.image(ONE_PIXEL_OFF)))

    def test_a_picture_of_one_pixel_is_flat(self):
        self.assertTrue(web_smoke.is_flat(self.image(make_png(1, 1, lambda x, y: (1, 2, 3)))))

    def test_the_same_picture_does_not_differ_and_one_pixel_does(self):
        self.assertFalse(web_smoke.images_differ(self.image(STRIPES), self.image(STRIPES)))
        self.assertTrue(web_smoke.images_differ(self.image(FLAT), self.image(ONE_PIXEL_OFF)))
        self.assertTrue(web_smoke.images_differ(self.image(STRIPES), self.image(OTHER_STRIPES)))

    def test_another_size_differs(self):
        small = make_png(4, 6, lambda x, y: (10, 20, 30))
        self.assertTrue(web_smoke.images_differ(self.image(FLAT), self.image(small)))

    def test_png_is_drawn_is_false_for_flat_garbage_and_empty(self):
        self.assertFalse(web_smoke.png_is_drawn(FLAT))
        self.assertFalse(web_smoke.png_is_drawn(b"not a png"))
        self.assertTrue(web_smoke.png_is_drawn(STRIPES))


class EvaluateTest(unittest.TestCase):
    def test_a_good_run_has_no_problem(self):
        self.assertEqual(web_smoke.evaluate(good_run()), [])

    def test_a_page_that_is_not_200_is_check_1(self):
        for status in (404, 500, None):
            with self.subTest(status):
                problems = web_smoke.evaluate(good_run(status=status))
                self.assertEqual(len(problems), 1)
                self.assertIn("(1)", problems[0])
                self.assertIn(str(status), problems[0])

    def test_an_uncaught_page_error_is_check_1(self):
        problems = web_smoke.evaluate(good_run(page_errors=["RuntimeError: unreachable"]))
        self.assertEqual(len(problems), 1)
        self.assertIn("(1)", problems[0])
        self.assertIn("unreachable", problems[0])

    def test_a_flat_first_frame_is_check_2(self):
        problems = web_smoke.evaluate(good_run(first_png=FLAT))
        self.assertTrue(any("(2)" in problem and "flat" in problem for problem in problems), problems)

    def test_a_missing_or_broken_first_frame_is_check_2(self):
        for png in (None, b"", b"garbage"):
            with self.subTest(png):
                problems = web_smoke.evaluate(good_run(first_png=png, second_png=None))
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("(2)", problems[0])

    def test_no_change_after_the_click_is_check_3(self):
        problems = web_smoke.evaluate(good_run(second_png=STRIPES))
        self.assertEqual(len(problems), 1)
        self.assertIn("(3)", problems[0])

    def test_a_click_that_changes_one_pixel_passes(self):
        self.assertEqual(web_smoke.evaluate(good_run(before_png=ONE_PIXEL_OFF, second_png=FLAT)), [])

    def test_the_shot_after_the_click_is_compared_with_the_one_before_it_and_not_with_the_first_frame(self):
        # a game that was still fading in at the first frame: it differs from the first frame with no click at all
        problems = web_smoke.evaluate(good_run(first_png=ONE_PIXEL_OFF, before_png=STRIPES, second_png=STRIPES))
        self.assertEqual(len(problems), 1)
        self.assertIn("(3)", problems[0])
        self.assertEqual(web_smoke.evaluate(good_run(first_png=STRIPES, before_png=ONE_PIXEL_OFF, second_png=STRIPES)), [])

    def test_a_broken_second_shot_is_check_3(self):
        problems = web_smoke.evaluate(good_run(second_png=b"garbage"))
        self.assertEqual(len(problems), 1)
        self.assertIn("(3)", problems[0])

    def test_a_missing_shot_before_the_click_is_check_3(self):
        problems = web_smoke.evaluate(good_run(before_png=None))
        self.assertEqual(len(problems), 1)
        self.assertIn("(3) before the click", problems[0])

    def test_a_run_that_stopped_early_reports_its_own_reason_once(self):
        run = good_run(second_png=None, problems=["(3) the canvas has no box to click in"])
        self.assertEqual(web_smoke.evaluate(run), ["(3) the canvas has no box to click in"])

    def test_an_error_in_the_console_is_check_4_and_other_levels_are_not(self):
        console = [ConsoleEntry("log", "hello"), ConsoleEntry("warning", "slow"), ConsoleEntry("info", "x"), ConsoleEntry("debug", "y")]
        self.assertEqual(web_smoke.evaluate(good_run(console=console)), [])
        problems = web_smoke.evaluate(good_run(console=[*console, ConsoleEntry("error", "Assertion failed: x")]))
        self.assertEqual(len(problems), 1)
        self.assertIn("(4)", problems[0])
        self.assertIn("Assertion failed", problems[0])

    def test_a_favicon_error_is_allowed_by_default_by_its_url_or_its_text(self):
        console = [ConsoleEntry("error", "Failed to load resource: 404", "http://127.0.0.1:1/favicon.ico"),
                   ConsoleEntry("error", "GET /favicon.ico 404")]
        self.assertEqual(web_smoke.evaluate(good_run(console=console)), [])

    def test_a_failed_resource_that_is_not_the_favicon_is_not_allowed(self):
        console = [ConsoleEntry("error", "Failed to load resource: 404", "http://127.0.0.1:1/game.data")]
        self.assertEqual(len(web_smoke.evaluate(good_run(console=console))), 1)

    def test_the_allow_list_of_the_command_line_is_searched_in_text_and_url(self):
        console = [ConsoleEntry("error", "ALSA lib: no soundcards"), ConsoleEntry("error", "x", "http://h/vendor/thing.js")]
        self.assertEqual(len(web_smoke.evaluate(good_run(console=console))), 2)
        self.assertEqual(web_smoke.evaluate(good_run(console=console), ["ALSA", r"vendor/"]), [])
        self.assertEqual(len(web_smoke.evaluate(good_run(console=console), ["ALSA"])), 1)

    def test_several_problems_are_all_reported(self):
        problems = web_smoke.evaluate(good_run(status=500, first_png=FLAT, before_png=FLAT, second_png=FLAT, console=[ConsoleEntry("error", "boom")]))
        self.assertEqual(sorted(problem[:3] for problem in problems), ["(1)", "(2)", "(3)", "(4)"])


def page_info(**changes) -> PageInfo:
    values = dict(cross_origin_isolated=True, shared_array_buffer="function", device_pixel_ratio=1.0, css_width=640.0, css_height=480.0,
                  canvas_width=640, canvas_height=480)
    values.update(changes)
    return PageInfo(**values)


class CanvasSizeTest(unittest.TestCase):
    def test_the_backing_store_of_the_css_size_times_the_ratio_is_right(self):
        self.assertIsNone(web_smoke.canvas_size_problem(page_info()))
        self.assertIsNone(web_smoke.canvas_size_problem(page_info(device_pixel_ratio=2.0, canvas_width=1280, canvas_height=960)))

    def test_one_pixel_of_rounding_is_allowed_two_are_not(self):
        info = page_info(device_pixel_ratio=1.5, css_width=641.0, canvas_width=961, canvas_height=720)  # 961.5 rounds to 962, 961 is within a pixel
        self.assertIsNone(web_smoke.canvas_size_problem(info))
        self.assertIsNone(web_smoke.canvas_size_problem(page_info(canvas_width=641)))
        problem = web_smoke.canvas_size_problem(page_info(canvas_width=642))
        self.assertIn("(5) canvas width", problem)
        self.assertIn("642", problem)

    def test_a_backing_store_that_ignores_the_ratio_is_the_failure_it_is_meant_to_find(self):
        problem = web_smoke.canvas_size_problem(page_info(device_pixel_ratio=2.0))
        self.assertIn("(5) canvas width", problem)
        self.assertIn("1280", problem)

    def test_the_height_is_checked_too(self):
        problem = web_smoke.canvas_size_problem(page_info(device_pixel_ratio=2.0, canvas_width=1280, canvas_height=480))
        self.assertIn("(5) canvas height", problem)

    def test_the_check_applies_to_a_real_run_and_not_to_the_selftest_page(self):
        wrong = page_info(device_pixel_ratio=2.0)
        self.assertEqual(len(web_smoke.evaluate(good_run(page_info=wrong, check_canvas_size=True))), 1)
        self.assertEqual(web_smoke.evaluate(good_run(page_info=wrong, check_canvas_size=False)), [])

    def test_a_real_run_whose_page_could_not_be_read_fails_check_5_and_the_selftest_page_does_not(self):
        problems = web_smoke.evaluate(good_run(page_info=None, check_canvas_size=True))
        self.assertEqual(len(problems), 1)
        self.assertIn("(5)", problems[0])
        self.assertEqual(web_smoke.evaluate(good_run(page_info=None, check_canvas_size=False)), [])
        # No frame was drawn: (2) says so, and the canvas size of a game that never started is not judged on top of it
        problems = web_smoke.evaluate(good_run(first_png=FLAT, second_png=None, page_info=None, check_canvas_size=True))
        self.assertEqual([problem[:3] for problem in problems], ["(2)"])

    def test_the_page_info_is_read_from_the_dictionary_the_page_returns(self):
        info = PageInfo.from_page({"crossOriginIsolated": True, "sharedArrayBuffer": "function", "devicePixelRatio": 2, "cssWidth": 10.5,
                                   "cssHeight": 20, "canvasWidth": 21, "canvasHeight": 40})
        self.assertEqual(info, PageInfo(True, "function", 2.0, 10.5, 20.0, 21, 40))

    def test_the_report_has_the_isolation_of_a_page_that_never_drew(self):
        early = page_info(cross_origin_isolated=False, shared_array_buffer="undefined", canvas_width=300, canvas_height=150)
        lines = web_smoke.describe_page(BrowserRun(early_info=early))
        self.assertIn("before the first frame: crossOriginIsolated=false, typeof SharedArrayBuffer=undefined", lines[1])
        self.assertIn("not read after the first frame", lines[2])
        self.assertIn("not read", web_smoke.describe_page(BrowserRun())[1])

    def test_the_report_lines(self):
        lines = web_smoke.describe_page(good_run(page_info=page_info(device_pixel_ratio=2.0, canvas_width=1280, canvas_height=960), touch=True))
        self.assertEqual(lines[0], "input: touch (a tap)")
        self.assertIn("crossOriginIsolated=true, typeof SharedArrayBuffer=function", lines[1])
        self.assertIn("devicePixelRatio=2, CSS size 640x480, backing store 1280x960", lines[2])
        self.assertIn("mouse (a click)", web_smoke.describe_page(good_run())[0])
        self.assertIn("not read", web_smoke.describe_page(good_run())[1])


class ExpectConsoleTest(unittest.TestCase):
    CONSOLE = [ConsoleEntry("log", "[TaskManager] Initialized with 4 worker threads"), ConsoleEntry("warning", "slow")]

    def test_the_unmatched_patterns_are_listed(self):
        self.assertEqual(web_smoke.unmatched_patterns(self.CONSOLE, [r"Initialized with \d+ worker threads"]), [])
        self.assertEqual(web_smoke.unmatched_patterns(self.CONSOLE, [r"single-thread mode", "slow", "x"]), ["single-thread mode", "x"])
        self.assertEqual(web_smoke.unmatched_patterns([], ["a"]), ["a"])
        self.assertEqual(web_smoke.unmatched_patterns(self.CONSOLE, []), [])

    def test_a_missing_expected_message_fails_check_4_and_a_seen_one_passes(self):
        run = good_run(console=self.CONSOLE)
        self.assertEqual(web_smoke.evaluate(run, None, [r"worker threads"]), [])
        problems = web_smoke.evaluate(run, None, [r"worker threads", "never"])
        self.assertEqual(len(problems), 1)
        self.assertIn("(4)", problems[0])
        self.assertIn("never", problems[0])

    def test_the_wait_ends_when_the_message_has_come_and_gives_up_at_the_timeout(self):
        times = iter(float(n) for n in range(100))
        page = SimpleNamespace(wait_for_timeout=lambda milliseconds: waits.append(milliseconds))
        waits = []
        run = BrowserRun()
        web_smoke.wait_for_expected_console(page, run, ["a"], 3.0, now=lambda: next(times))
        self.assertGreaterEqual(len(waits), 2)  # gave up
        self.assertEqual(waits[0], web_smoke.POLL_INTERVAL_SECONDS * 1000)
        waits.clear()
        run.console.append(ConsoleEntry("log", "a"))
        web_smoke.wait_for_expected_console(page, run, ["a"], 3.0, now=lambda: next(times))
        self.assertEqual(waits, [])


class PollTest(unittest.TestCase):
    def setUp(self):
        self.time = 0.0
        self.sleeps = []

    def now(self):
        return self.time

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.time += seconds

    def poll(self, results, accept, timeout=2.0, interval=0.5):
        calls = iter(results)
        return web_smoke.poll(lambda: next(calls), accept, timeout, interval, self.now, self.sleep)

    def test_it_stops_at_the_first_accepted_result(self):
        result, accepted = self.poll([b"a", b"b", b"c"], lambda value: value == b"b")
        self.assertEqual((result, accepted), (b"b", True))
        self.assertEqual(self.sleeps, [0.5])

    def test_the_first_try_needs_no_wait(self):
        self.assertEqual(self.poll([b"a"], lambda value: True), (b"a", True))
        self.assertEqual(self.sleeps, [])

    def test_it_gives_up_after_the_timeout_with_the_last_result(self):
        result, accepted = self.poll([bytes([n]) for n in range(20)], lambda value: False, timeout=2.0, interval=0.5)
        self.assertFalse(accepted)
        self.assertEqual(result, bytes([4]))
        self.assertEqual(self.sleeps, [0.5] * 4)

    def test_a_zero_timeout_still_tries_once(self):
        self.assertEqual(self.poll([b"a"], lambda value: False, timeout=0.0), (b"a", False))


class SelftestPageTest(unittest.TestCase):
    def test_the_picture_before_a_click_does_not_depend_on_the_frame(self):
        page = web_smoke.selftest_page()
        still = page.split("} else {", 1)[1].split("}", 1)[0]  # the branch that draws before a click
        self.assertNotIn("frame", still)
        self.assertIn("frame", page.split("else if (clicked)", 1)[1].split("} else {", 1)[0])  # after the click it moves

    def test_the_page_has_the_canvas_the_animation_and_the_click_handler(self):
        page = web_smoke.selftest_page()
        self.assertIn('<canvas id="canvas"', page)
        self.assertIn("requestAnimationFrame", page)
        self.assertIn("mousedown", page)
        self.assertNotIn("http://", page.replace("http://127", ""))  # no external script, font or CDN

    def test_the_faults_change_the_page_and_none_does_not_break_it(self):
        pages = {fault: web_smoke.selftest_page(fault) for fault in web_smoke.SELFTEST_FAULTS}
        # the fault `upload` is in the server (it has no match store), so its page is the good one
        self.assertEqual(pages["upload"], pages["none"])
        self.assertEqual(len(set(pages.values())), len(pages) - 1)
        self.assertNotIn("on purpose", pages["none"])
        self.assertIn("var flat = false", pages["none"])
        self.assertIn("var flat = true", pages["flat"])
        self.assertIn("var deaf = false", pages["none"])
        self.assertIn("var deaf = true", pages["deaf"])
        self.assertIn("var moving = false", pages["none"])
        self.assertIn("var moving = true", pages["moving"])
        self.assertIn("console.error", pages["console-error"])
        self.assertIn("throw new Error", pages["page-error"])

    def test_the_page_carries_the_script_of_the_shell_unchanged_before_its_own(self):
        shell = web_smoke.SHELL_PATH.read_text(encoding="utf-8")
        script = web_smoke.web_post_script()
        self.assertIn("window.bagiWebPost", script)
        self.assertIn(script, shell)
        page = web_smoke.selftest_page()
        self.assertIn(f'<script id="bagi-web-post">{script}</script>', page)
        self.assertLess(page.index("bagi-web-post"), page.index("<script>"))

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_script_of_every_variant_passes_node_check(self):
        for fault in web_smoke.SELFTEST_FAULTS:
            with self.subTest(fault), tempfile.TemporaryDirectory() as temp:
                page = web_smoke.selftest_page(fault)
                body = page.split("<script>", 1)[1].split("</script>", 1)[0]
                script = Path(temp) / "page.js"
                script.write_text(body, encoding="utf-8")
                result = subprocess.run([NODE, "--check", str(script)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_site_has_a_manifest_that_says_single_thread_and_is_served_without_the_isolation_headers(self):
        with tempfile.TemporaryDirectory() as temp:
            root = web_smoke.write_selftest_site(Path(temp) / "site")
            self.assertEqual(json.loads((root / "manifest.json").read_text(encoding="utf-8"))["threads"], False)
            self.assertEqual(web_smoke.serve.decide_cross_origin_isolation(None, None, root)[0], False)
            with contextlib.redirect_stdout(io.StringIO()), web_smoke.served(root) as url:
                response, _ = http_get(url)
                self.assertIsNone(response.getheader("Cross-Origin-Opener-Policy"))

    def test_a_threaded_dist_or_one_without_a_manifest_is_served_with_both_headers(self):
        for manifest in ('{"game": "g", "threads": true, "files": {}}', None):
            with self.subTest(manifest), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                (root / "index.html").write_text("<!doctype html>", encoding="utf-8")
                if manifest:
                    (root / "manifest.json").write_text(manifest, encoding="utf-8")
                out = io.StringIO()
                with contextlib.redirect_stdout(out), web_smoke.served(root) as url:
                    response, _ = http_get(url)
                self.assertEqual(response.getheader("Cross-Origin-Opener-Policy"), "same-origin")
                self.assertEqual(response.getheader("Cross-Origin-Embedder-Policy"), "require-corp")
                self.assertIn("isolation on", out.getvalue())

    def test_the_site_is_served_by_serve_py_on_a_free_port_that_is_closed_afterwards(self):
        with tempfile.TemporaryDirectory() as temp:
            root = web_smoke.write_selftest_site(Path(temp) / "site")
            with contextlib.redirect_stdout(io.StringIO()), web_smoke.served(root) as url:
                self.assertTrue(url.startswith("http://127.0.0.1:"))
                host, port = url[len("http://"):].rstrip("/").split(":")
                response, body = http_get(url)
                self.assertEqual(response.status, 200)
                self.assertIn("<canvas", body)
            with self.assertRaises(OSError):
                http.client.HTTPConnection(host, int(port), timeout=2).request("GET", "/")


class WebManifestCheckTest(unittest.TestCase):
    """Check 6 on a fetch function (no server), and on a dist that package.py wrote, served by serve.py."""

    PNG = package.flat_color_png(192, "#101114")

    @classmethod
    def fetcher(cls, files: dict):
        """files: {path: (status, content type, body)}; a path that is not there is a 404."""
        return lambda path: files.get(path, (404, "text/plain", b""))

    @classmethod
    def good_files(cls, **manifest_changes) -> dict:
        manifest = {"name": "g", "display": "fullscreen", "icons": [{"src": "icons/a.png", "sizes": "192x192"}]}
        manifest.update(manifest_changes)
        return {"manifest.webmanifest": (200, "application/manifest+json", json.dumps(manifest).encode()),
                "icons/a.png": (200, "image/png", cls.PNG)}

    def test_a_good_manifest_has_no_problem(self):
        self.assertEqual(web_smoke.check_web_manifest(self.fetcher(self.good_files())), [])

    def test_a_charset_after_the_type_is_fine(self):
        files = self.good_files()
        files["manifest.webmanifest"] = (200, "Application/Manifest+JSON; charset=utf-8", files["manifest.webmanifest"][2])
        self.assertEqual(web_smoke.check_web_manifest(self.fetcher(files)), [])

    def test_a_missing_manifest_is_one_problem(self):
        problems = web_smoke.check_web_manifest(self.fetcher({}))
        self.assertEqual(len(problems), 1)
        self.assertIn("(6)", problems[0])
        self.assertIn("HTTP 404", problems[0])

    def test_the_wrong_type_is_found(self):
        files = self.good_files()
        files["manifest.webmanifest"] = (200, "application/json", files["manifest.webmanifest"][2])
        problems = web_smoke.check_web_manifest(self.fetcher(files))
        self.assertEqual(len(problems), 1)
        self.assertIn("application/manifest+json", problems[0])

    def test_text_that_is_not_json_or_not_an_object_is_found(self):
        for body in (b"{not json", b"\xff\xfe", b"[1]"):
            with self.subTest(body=body):
                files = self.good_files()
                files["manifest.webmanifest"] = (200, "application/manifest+json", body)
                problems = web_smoke.check_web_manifest(self.fetcher(files))
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("(6)", problems[0])

    def test_a_display_other_than_fullscreen_is_found(self):
        for change in ({"display": "standalone"}, {"display": None}):
            with self.subTest(change):
                problems = web_smoke.check_web_manifest(self.fetcher(self.good_files(**change)))
                self.assertEqual(len(problems), 1)
                self.assertIn("not 'fullscreen'", problems[0])

    def test_no_icons_is_found(self):
        for change in ({"icons": []}, {"icons": None}, {"icons": "x.png"}):
            with self.subTest(change):
                problems = web_smoke.check_web_manifest(self.fetcher(self.good_files(**change)))
                self.assertEqual(len(problems), 1)
                self.assertIn("lists no icons", problems[0])

    def test_an_icon_that_is_missing_or_not_a_png_or_has_the_wrong_type_is_found(self):
        cases = {
            "missing": (404, "text/plain", b""),
            "wrong type": (200, "application/octet-stream", self.PNG),
            "not a png": (200, "image/png", b"<html>"),
        }
        for name, answer in cases.items():
            with self.subTest(name):
                files = self.good_files()
                files["icons/a.png"] = answer
                problems = web_smoke.check_web_manifest(self.fetcher(files))
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("icon icons/a.png", problems[0])

    def test_an_icon_path_is_resolved_against_the_manifest_in_the_root(self):
        files = self.good_files(icons=[{"src": "./icons/a.png"}, {"src": "/icons/a.png"}])
        files["/icons/a.png"] = files["icons/a.png"]
        self.assertEqual(web_smoke.check_web_manifest(self.fetcher(files)), [])

    def test_an_icon_without_src_is_found(self):
        problems = web_smoke.check_web_manifest(self.fetcher(self.good_files(icons=[{"sizes": "1x1"}, 5])))
        self.assertEqual(len(problems), 2)

    def test_the_problems_reach_the_verdict_of_a_run(self):
        run = good_run(web_manifest_problems=["(6) boom"])
        self.assertEqual(web_smoke.evaluate(run), ["(6) boom"])
        self.assertEqual(web_smoke.evaluate(good_run(web_manifest_problems=[])), [])
        self.assertEqual(web_smoke.evaluate(good_run()), [])

    def test_a_dist_written_by_package_py_passes_over_http_and_the_same_dist_without_its_icons_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            build = web_fixtures.write_build_output(Path(temp) / "build")
            out = Path(temp) / "dist"
            package.package(build, out, brotli_module=None)  # no app file: two generated icons
            with contextlib.redirect_stdout(io.StringIO()), web_smoke.served(out) as url:
                self.assertEqual(web_smoke.check_web_manifest(web_smoke.fetch_from(url)), [])
                shutil.rmtree(out / "icons")
                problems = web_smoke.check_web_manifest(web_smoke.fetch_from(url))
            self.assertEqual(len(problems), 2, problems)  # the 192 and the 512 icon
            self.assertTrue(all("HTTP 404" in problem for problem in problems))

    def test_the_selftest_site_passes_check_6_over_http(self):
        with tempfile.TemporaryDirectory() as temp:
            root = web_smoke.write_selftest_site(Path(temp) / "site")
            with contextlib.redirect_stdout(io.StringIO()), web_smoke.served(root) as url:
                self.assertEqual(web_smoke.check_web_manifest(web_smoke.fetch_from(url)), [])


class UploadCheckTest(unittest.TestCase):
    """Check 7 on a `post` function and a store folder (no browser): the file is read back from the store."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.store = Path(self.temp.name)
        self.posts = []

    def storing(self, status=200, content=None):
        """A post that stores `content` (the posted body when None) as the piece and answers `status`."""
        def post(path, body):
            self.posts.append((path, body))
            match_id = path.split("/")[2]
            if content != b"":
                (self.store / match_id).mkdir()
                (self.store / match_id / "0.jsonl").write_bytes(body.encode("utf-8") if content is None else content)
            return {"status": status}
        return post

    def test_a_stored_piece_passes_and_the_post_is_a_start_line_to_a_fresh_id_with_the_smoke_version(self):
        self.assertEqual(web_smoke.check_upload(self.storing(), self.store), [])
        path, body = self.posts[0]
        self.assertRegex(path, r"^/matches/[0-9a-f]{32}/0$")
        self.assertEqual(json.loads(body), {"event": "start", "version": web_smoke.SMOKE_LOG_VERSION})
        self.assertTrue(body.endswith("\n"))
        self.assertEqual(web_smoke.check_upload(self.storing(), self.store), [])
        self.assertNotEqual(self.posts[0][0], self.posts[1][0])  # a fresh id every time

    def test_an_error_of_the_page_or_a_status_other_than_200_is_check_7(self):
        for name, answer in (("error", {"error": "the page has no window.bagiWebPost"}), ("405", {"status": 405}), ("0", {"status": 0}),
                             ("no dict", None)):
            with self.subTest(name):
                problems = web_smoke.check_upload(lambda path, body: answer, self.store)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("(7)", problems[0])
        self.assertIn("window.bagiWebPost", web_smoke.check_upload(lambda path, body: {"error": "the page has no window.bagiWebPost"}, self.store)[0])
        self.assertIn("HTTP 405", web_smoke.check_upload(lambda path, body: {"status": 405}, self.store)[0])

    def test_a_page_that_cannot_run_the_script_is_check_7(self):
        def post(path, body):
            raise RuntimeError("Target page, context or browser has been closed\nlog")
        problems = web_smoke.check_upload(post, self.store)
        self.assertEqual(len(problems), 1)
        self.assertIn("(7)", problems[0])
        self.assertIn("has been closed", problems[0])

    def test_a_200_with_no_file_or_other_bytes_in_the_store_is_check_7(self):
        for content, expected in ((b"", "not in the match store"), (b'{"event":"start","version":2}\n', "differs")):
            with self.subTest(content):
                problems = web_smoke.check_upload(self.storing(content=content), self.store)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("(7)", problems[0])
                self.assertIn(expected, problems[0])

    def test_the_problems_reach_the_verdict_of_a_run(self):
        self.assertEqual(web_smoke.evaluate(good_run(upload_problems=["(7) boom"])), ["(7) boom"])
        self.assertEqual(web_smoke.evaluate(good_run(upload_problems=[])), [])
        self.assertEqual(web_smoke.evaluate(good_run()), [])  # not made (--no-upload-check)

    def test_the_real_server_stores_what_the_check_posts_and_a_server_without_a_store_does_not(self):
        with tempfile.TemporaryDirectory() as site:
            root = Path(site)
            (root / "index.html").write_text("<!doctype html>", encoding="utf-8")
            store = self.store / "matches"

            def post_through(base):
                def post(path, body):
                    request = urllib.request.Request(base.rstrip("/") + path, data=body.encode("utf-8"), method="POST")
                    try:
                        with urllib.request.urlopen(request, timeout=10) as response:
                            return {"status": response.status}
                    except urllib.error.HTTPError as error:
                        with error:
                            return {"status": error.code}
                return post

            with contextlib.redirect_stdout(io.StringIO()):
                with web_smoke.served(root, store) as url:
                    self.assertEqual(web_smoke.check_upload(post_through(url), store), [])
                with web_smoke.served(root) as url:
                    problems = web_smoke.check_upload(post_through(url), store)
            self.assertEqual(len(problems), 1, problems)
            self.assertIn("HTTP 405", problems[0])


class SavesCheckTest(unittest.TestCase):
    """Check 8 on what the three browser runs showed (no browser)."""

    def good_persist(self, **changes):
        written = list(range(32))
        values = dict(written=written, state="persistent", flush=None, restarted=True, state_after="persistent", read_back=list(written))
        values.update(changes)
        return web_smoke.SavesPersistRun(**values)

    def good_refused(self):
        return [web_smoke.SavesRefusedRun(mode, started=True, state="memory") for mode in web_smoke.SAVES_REFUSED_MODES]

    def test_a_file_that_came_back_and_pages_that_started_in_memory_pass(self):
        self.assertEqual(web_smoke.check_saves(self.good_persist(), self.good_refused()), [])

    def test_each_way_the_reload_can_go_wrong_is_one_reason_of_check_8a(self):
        cases = {
            "error": (dict(error="the page has no Module.bagiSaves"), "no Module.bagiSaves"),
            "state": (dict(state="memory"), "'memory', not 'persistent'"),
            "flush": (dict(flush="quota"), "flush did not succeed: quota"),
            "restart": (dict(restarted=False), "did not draw a frame after the reload"),
            "state after": (dict(state_after="memory"), "'memory' after the reload"),
            "gone": (dict(read_back=None), "is not there after it"),
            "changed": (dict(read_back=[1, 2, 3]), "differs from the one written (3 bytes, 32 written)"),
        }
        for name, (changes, text) in cases.items():
            with self.subTest(name):
                problems = web_smoke.check_saves(self.good_persist(**changes), self.good_refused())
                self.assertEqual(len(problems), 1, problems)
                self.assertTrue(problems[0].startswith("(8a) reload: "), problems)
                self.assertIn(text, problems[0])

    def test_a_refused_indexeddb_must_not_stop_the_page_and_the_state_must_be_memory(self):
        for changes, text in ((dict(started=False, state=None), "did not start"), (dict(state="persistent"), "not 'memory'"),
                              (dict(error="the file could not be written"), "could not be written")):
            with self.subTest(text):
                refused = self.good_refused()
                refused[1] = web_smoke.SavesRefusedRun("silent", **{"started": True, "state": "memory", **changes})
                problems = web_smoke.check_saves(self.good_persist(), refused)
                self.assertEqual(len(problems), 1, problems)
                self.assertTrue(problems[0].startswith("(8, IndexedDB silent): "), problems)
                self.assertIn(text, problems[0])

    def test_console_errors_and_page_errors_count_in_every_run_and_the_allow_list_applies(self):
        error = ConsoleEntry("error", "storage exploded")
        persist = self.good_persist(console=[error, ConsoleEntry("warning", "[saves] x"), ConsoleEntry("error", "favicon.ico")],
                                    page_errors=["boom"])
        refused = self.good_refused()
        refused[0].console.append(error)
        problems = web_smoke.check_saves(persist, refused)
        self.assertEqual(len(problems), 3, problems)
        self.assertEqual(web_smoke.check_saves(persist, refused, ["storage exploded"]), ["(8a) reload: uncaught error in the page: boom"])

    def test_the_problems_reach_the_verdict_of_a_run(self):
        self.assertEqual(web_smoke.evaluate(good_run(saves_problems=["(8a) boom"])), ["(8a) boom"])
        self.assertEqual(web_smoke.evaluate(good_run(saves_problems=[])), [])
        self.assertEqual(web_smoke.evaluate(good_run()), [])  # not made (--no-saves-check, the selftest page)

    def test_the_refused_scripts_make_open_throw_and_never_answer(self):
        self.assertIn("throw", web_smoke.SAVES_REFUSED_SCRIPTS["throws"])
        self.assertIn("return {}", web_smoke.SAVES_REFUSED_SCRIPTS["silent"])
        self.assertEqual(set(web_smoke.SAVES_REFUSED_SCRIPTS), set(web_smoke.SAVES_REFUSED_MODES))

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_page_scripts_are_valid_javascript(self):
        for name in ("SAVES_WRITE_SCRIPT", "SAVES_READ_SCRIPT"):
            with self.subTest(name), tempfile.TemporaryDirectory() as scratch:
                path = Path(scratch) / "script.js"
                path.write_text(f"const f = {getattr(web_smoke, name)};\n", encoding="utf-8")
                result = subprocess.run([NODE, "--check", str(path)], capture_output=True, text=True, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)


class FakeSavesPage:
    """A page for check 8: the state of Module.bagiSaves and the file the reload keeps, as the world of the test says."""

    def __init__(self, world, init_script):
        self.world = world
        self.init_script = init_script
        self.canvas = FakeCanvas([STRIPES])
        self.file = None
        self.reloads = 0
        self.urls = []

    @property
    def state(self):
        return "memory" if self.init_script else self.world.get("state", "persistent")

    def on(self, name, handler):
        pass

    def goto(self, url, wait_until, timeout):
        self.urls.append(url)

    def reload(self, wait_until, timeout):
        self.reloads += 1
        if not self.world.get("keeps", True) or self.state != "persistent":
            self.file = None

    def locator(self, selector):
        return self.canvas

    def wait_for_timeout(self, milliseconds):
        pass

    def evaluate(self, script, arg=None):
        if script == web_smoke.SAVES_WRITE_SCRIPT:
            _, written, _ = arg
            self.file = list(written)
            if "error" in self.world:
                return {"error": self.world["error"]}
            return {"state": self.state, "flush": self.world.get("flush") if self.state == "persistent" else None}
        assert script == web_smoke.SAVES_READ_SCRIPT
        return {"state": self.state, "bytes": self.file} if self.file is not None else {"state": self.state, "error": "the file is not there"}


class FakeSavesBrowser:
    def __init__(self, world):
        self.world = world
        self.pages = []
        self.contexts = []
        self.closed = False

    def new_context(self, viewport, **kwargs):
        browser = self
        context = SimpleNamespace(scripts=[], kwargs=kwargs)
        self.contexts.append(context)
        context.add_init_script = context.scripts.append

        def new_page():
            page = FakeSavesPage(browser.world, context.scripts[0] if context.scripts else None)
            browser.pages.append(page)
            return page

        context.new_page = new_page
        return context

    def close(self):
        self.closed = True


class SavesRunTest(unittest.TestCase):
    """run_saves_persistence and run_saves_refused against a fake browser: the order of the calls and what is judged."""

    def setUp(self):
        for name, value in (("POLL_INTERVAL_SECONDS", 0.01),):
            patcher = mock.patch.object(web_smoke, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_checks(self, **world):
        browser = FakeSavesBrowser(world)
        playwright = SimpleNamespace(chromium=SimpleNamespace(launch=lambda headless, args, **kwargs: browser,
                                                              executable_path=str(Path(__file__))))
        api = SimpleNamespace(sync_playwright=lambda: contextlib.nullcontext(playwright))
        problems = web_smoke.run_saves_checks(api, "http://h/", options(saves_check=True))
        return problems, browser

    def test_a_page_that_keeps_the_file_passes_in_three_fresh_contexts_and_the_browser_is_closed(self):
        problems, browser = self.run_checks()
        self.assertEqual(problems, [])
        self.assertEqual(len(browser.contexts), 1 + len(web_smoke.SAVES_REFUSED_MODES))
        self.assertEqual([page.reloads for page in browser.pages], [1, 0, 0])
        self.assertEqual([bool(context.scripts) for context in browser.contexts], [False, True, True])
        self.assertTrue(all(page.urls == ["http://h/?fullscreen=0"] for page in browser.pages))
        self.assertTrue(browser.closed)

    def test_a_page_that_loses_the_file_at_the_reload_fails_check_8a_only(self):
        problems, _ = self.run_checks(keeps=False)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("is not there after it", problems[0])

    def test_a_page_whose_state_is_memory_never_reloads_and_fails_8a(self):
        problems, browser = self.run_checks(state="memory")
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("'memory', not 'persistent'", problems[0])
        self.assertEqual(browser.pages[0].reloads, 0)

    def test_a_failed_flush_and_a_page_without_the_module_are_reported(self):
        problems, _ = self.run_checks(flush="no answer within 15 s")
        self.assertIn("no answer within 15 s", problems[0])
        problems, _ = self.run_checks(error="the page has no Module.bagiSaves")
        self.assertEqual(len(problems), 1 + len(web_smoke.SAVES_REFUSED_MODES), problems)

    def test_run_smoke_runs_check_8_for_a_dist_and_prints_its_line(self):
        with tempfile.TemporaryDirectory() as folder:
            dist = package_fake_dist(Path(folder))
            browser = FakeSavesBrowser({})
            canvas = FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])
            page = FakePage(canvas)
            fake = FakePlaywright(page)
            launched = []  # the first browser is the one of the checks 1 to 7, the next ones are those of check 8

            def launch(headless, args, **kwargs):
                launched.append(1)
                return fake._launch(headless, args, **kwargs) if len(launched) == 1 else browser

            fake.chromium.launch = launch
            out = io.StringIO()
            with contextlib.redirect_stdout(out), mock.patch.object(web_smoke, "CLICK_TIMEOUT_SECONDS", 0.05):
                code = web_smoke.run_smoke(options(selftest=False, dist=dist, saves_check=True, upload_check=False),
                                           SimpleNamespace(sync_playwright=fake.sync_playwright))
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("ok: (8)", out.getvalue())
        self.assertEqual(len(browser.pages), 3)

    def test_run_smoke_skips_check_8_for_the_selftest_page(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.run_smoke(options(), SimpleNamespace(sync_playwright=fake.sync_playwright))
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("skip: (8)", out.getvalue())


class FakeCanvas:
    """`shots` are the screenshots in order (the last one repeats); once the page has been clicked they are `after_click` instead,
    when given: a canvas that changes only because of the click."""

    def __init__(self, shots, box=None, after_click=None):
        self.shots = list(shots)
        self.after_click = list(after_click) if after_click is not None else None
        self.clicked = False
        self.events = []
        self.box = box if box is not None else {"x": 10.0, "y": 20.0, "width": 400.0, "height": 300.0}
        self.taken = 0

    @property
    def first(self):
        return self

    def wait_for(self, state, timeout):
        pass

    def screenshot(self):
        self.taken += 1
        self.events.append(("shot",))
        shots = self.after_click if self.clicked and self.after_click is not None else self.shots
        return shots.pop(0) if len(shots) > 1 else shots[0]

    def bounding_box(self):
        return self.box


class FakePage:
    def __init__(self, canvas, status=200, console=(), page_errors=(), info=None, post_answer=None):
        self.canvas = canvas
        self.post_answer = post_answer  # what the page's post answers (None: a real POST to the server of the run)
        self.posted = []
        self.info = info if info is not None else {"crossOriginIsolated": False, "sharedArrayBuffer": "undefined", "devicePixelRatio": 1,
                                                   "cssWidth": 400, "cssHeight": 300, "canvasWidth": 400, "canvasHeight": 300}
        self.taps = []
        self.status = status
        self.handlers = {}
        self.clicks = []
        self.moves = []
        self.urls = []
        self.waits = []
        self.events = canvas.events  # the shots, the waits, the moves and the clicks in the order they came
        self._console = console
        self._page_errors = page_errors
        self.mouse = SimpleNamespace(click=self._click, move=self._move)
        self.touchscreen = SimpleNamespace(tap=self._tap)

    def evaluate(self, script, arg=None):
        if script == web_smoke.UPLOAD_PAGE_SCRIPT:  # check 7: what the page script does, done here by a plain POST to the served page
            path, body, _, _ = arg
            self.posted.append((path, body))
            return self.post_answer if self.post_answer is not None else self._post_to_server(path, body)
        return dict(self.info)

    def _post_to_server(self, path, body):
        request = urllib.request.Request(self.urls[0].split("?", 1)[0].rstrip("/") + path, data=body.encode("utf-8"), method="POST")
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return {"status": response.status}
        except urllib.error.HTTPError as error:
            with error:
                return {"status": error.code}

    def _tap(self, x, y):
        self.taps.append((x, y))
        self.events.append(("tap", x, y))
        self.canvas.clicked = True

    def _click(self, x, y):
        self.clicks.append((x, y))
        self.events.append(("click", x, y))
        self.canvas.clicked = True

    def _move(self, x, y):
        self.moves.append((x, y))
        self.events.append(("move", x, y))

    def on(self, name, handler):
        self.handlers[name] = handler

    def goto(self, url, wait_until, timeout):
        self.urls.append(url)
        for entry in self._console:
            self.handlers["console"](SimpleNamespace(type=entry.kind, text=entry.text, location={"url": entry.url}))
        for error in self._page_errors:
            self.handlers["pageerror"](error)
        return SimpleNamespace(status=self.status)

    def locator(self, selector):
        self.selector = selector
        return self.canvas

    def wait_for_timeout(self, milliseconds):
        self.waits.append(milliseconds)
        self.events.append(("wait", milliseconds))
        time.sleep(milliseconds / 1000)  # the polls wait here, like in a real page, so they do not spin


class FakePlaywright:
    """The part of playwright.sync_api that web_smoke uses, in the shape of the documented API."""

    def __init__(self, page, launch_error=None, executable=None):
        self.page = page
        self.launch_error = launch_error
        self.closed = False
        self.launch_args = None
        self.launch_kwargs = None
        self.viewport = None
        self.context_kwargs = None
        fake = self
        # an executable that exists unless the test names another path
        self.chromium = SimpleNamespace(launch=fake._launch, executable_path=str(executable or Path(__file__)))

    def _launch(self, headless, args, **kwargs):
        if self.launch_error:
            raise RuntimeError(self.launch_error)
        self.launch_args = (headless, args)
        self.launch_kwargs = kwargs
        fake = self
        context = SimpleNamespace(new_page=lambda: fake.page)

        def new_context(viewport, **context_kwargs):
            fake.viewport = viewport
            fake.context_kwargs = context_kwargs
            return context

        return SimpleNamespace(new_context=new_context, close=lambda: setattr(fake, "closed", True))

    def sync_playwright(self):
        @contextlib.contextmanager
        def manager():
            yield self
        return manager()


def options(**changes) -> web_smoke.Options:
    values = dict(dist=None, selftest=True, click=None, viewport=(640, 480), timeout=1.0, settle=0.0,
                  allow_console=[], chromium_args=["--extra"], selftest_fault="none", saves_check=False)
    values.update(changes)
    return web_smoke.Options(**values)


class BrowserFlowTest(unittest.TestCase):
    """run_browser and run_smoke against the fake: the calls come in the order the checks need, and the result is judged."""

    def setUp(self):
        # The waits of the polls are real time: shorten them
        for name, value in (("CLICK_TIMEOUT_SECONDS", 0.05), ("POLL_INTERVAL_SECONDS", 0.01)):
            patcher = mock.patch.object(web_smoke, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def run_browser(self, shots, box=None, timeout=1.0, after_click=(OTHER_STRIPES,), settle=0.0, **page_args):
        """`shots` are the shots before the click; after it the canvas shows `after_click`."""
        canvas = FakeCanvas(shots, box, after_click)
        page = FakePage(canvas, **page_args)
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://127.0.0.1:1/",
                                    options(timeout=timeout, settle=settle))
        return run, page, fake, canvas

    def test_a_good_page_gives_a_run_that_passes(self):
        run, page, fake, canvas = self.run_browser([STRIPES])
        self.assertEqual(run.status, 200)
        self.assertEqual(web_smoke.evaluate(run), [])
        self.assertEqual(page.selector, web_smoke.CANVAS_SELECTOR)
        self.assertEqual(page.urls, ["http://127.0.0.1:1/?fullscreen=0"])  # the shell is told not to ask for fullscreen on the first tap
        self.assertTrue(fake.closed)
        self.assertEqual(fake.viewport, {"width": 640, "height": 480})
        self.assertEqual(fake.launch_args[0], True)
        self.assertEqual(fake.launch_args[1][-1], "--extra")

    def test_the_context_gets_the_pixel_ratio_and_touch_and_the_launch_gets_the_named_browser(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options())
        self.assertEqual(fake.context_kwargs, {"device_scale_factor": 1.0, "has_touch": False})
        self.assertEqual(fake.launch_kwargs, {})
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/",
                              options(device_scale_factor=2.0, touch=True, browser_channel="msedge"))
        self.assertEqual(fake.context_kwargs, {"device_scale_factor": 2.0, "has_touch": True})
        self.assertEqual(fake.launch_kwargs, {"channel": "msedge"})
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(executable_path=Path(__file__)))
        self.assertEqual(fake.launch_kwargs, {"executable_path": str(Path(__file__))})

    def test_a_channel_that_is_not_installed_is_browser_missing_and_another_launch_error_is_not(self):
        missing = "BrowserType.launch: Chromium distribution 'msedge' is not found at C:\\x\\msedge.exe\nRun playwright install msedge"
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])), launch_error=missing)
        api = SimpleNamespace(sync_playwright=fake.sync_playwright)
        with self.assertRaisesRegex(web_smoke.BrowserMissing, "channel 'msedge'"):
            web_smoke.run_browser(api, "http://h/", options(browser_channel="msedge"))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(web_smoke.run_smoke(options(browser_channel="msedge"), api), web_smoke.EXIT_SKIPPED)
        self.assertIn("SKIP:", out.getvalue())
        crash = FakePlaywright(FakePage(FakeCanvas([STRIPES])), launch_error="Browser closed unexpectedly")
        with self.assertRaises(RuntimeError):
            web_smoke.run_browser(SimpleNamespace(sync_playwright=crash.sync_playwright), "http://h/", options(browser_channel="msedge"))

    def test_an_executable_that_does_not_exist_is_browser_missing(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        missing = Path(tempfile.gettempdir()) / "web_smoke_no_such_browser" / "chrome.exe"
        with self.assertRaisesRegex(web_smoke.BrowserMissing, "no browser at"):
            web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(executable_path=missing))
        self.assertIsNone(fake.launch_args)

    def test_with_touch_the_click_is_a_tap_and_the_pointer_is_not_moved(self):
        canvas = FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])
        page = FakePage(canvas)
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(touch=True))
        self.assertEqual(page.taps, [(210.0, 170.0)])
        self.assertEqual(page.clicks, [])
        self.assertEqual(page.moves, [])
        self.assertTrue(run.touch)
        self.assertEqual(web_smoke.evaluate(run), [])

    def test_a_tap_that_changes_nothing_fails_check_3_like_a_click(self):
        page = FakePage(FakeCanvas([STRIPES], after_click=[STRIPES]))
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(touch=True))
        problems = web_smoke.evaluate(run)
        self.assertEqual(len(problems), 1)
        self.assertIn("(3)", problems[0])

    def test_the_isolation_of_the_page_is_read_when_the_canvas_shows_even_if_nothing_is_ever_drawn(self):
        info = {"crossOriginIsolated": False, "sharedArrayBuffer": "undefined", "devicePixelRatio": 1, "cssWidth": 640, "cssHeight": 480,
                "canvasWidth": 300, "canvasHeight": 150}
        page = FakePage(FakeCanvas([FLAT]), info=info)
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(timeout=0.05))
        self.assertIsNotNone(run.early_info)
        self.assertFalse(run.early_info.cross_origin_isolated)
        self.assertEqual(run.early_info.shared_array_buffer, "undefined")
        problems = web_smoke.evaluate(run)
        self.assertTrue(any(problem.startswith("(2)") for problem in problems))
        self.assertFalse(any(problem.startswith("(5)") for problem in problems))

    def test_the_page_info_is_read_after_the_first_frame_and_checked_for_a_real_dist_only(self):
        wrong = {"crossOriginIsolated": True, "sharedArrayBuffer": "function", "devicePixelRatio": 2, "cssWidth": 400, "cssHeight": 300,
                 "canvasWidth": 400, "canvasHeight": 300}
        for selftest, expected in ((True, 0), (False, 1)):
            with self.subTest(selftest=selftest):
                page = FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]), info=wrong)
                fake = FakePlaywright(page)
                run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(selftest=selftest))
                self.assertEqual(run.page_info, PageInfo(True, "function", 2.0, 400.0, 300.0, 400, 300))
                self.assertEqual(len(web_smoke.evaluate(run)), expected)

    def test_an_expected_console_message_is_waited_for_and_reported_when_it_never_comes(self):
        page = FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]), console=[ConsoleEntry("log", "[TaskManager] Initialized with 4 worker threads")])
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/",
                                    options(expect_console=[r"Initialized with \d+ worker threads"]))
        self.assertEqual(web_smoke.evaluate(run, None, [r"Initialized with \d+ worker threads"]), [])
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])))
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(expect_console=["never"], timeout=0.05))
        self.assertEqual(len(web_smoke.evaluate(run, None, ["never"])), 1)

    def test_the_default_click_is_the_middle_of_the_canvas(self):
        _, page, _, _ = self.run_browser([STRIPES])
        self.assertEqual(page.clicks, [(10.0 + 200.0, 20.0 + 150.0)])

    def test_a_click_point_is_from_the_top_left_of_the_canvas(self):
        canvas = FakeCanvas([STRIPES])
        page = FakePage(canvas)
        fake = FakePlaywright(page)
        web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(click=(30.0, 40.0)))
        self.assertEqual(page.clicks, [(40.0, 60.0)])

    def test_a_click_point_outside_the_canvas_is_a_problem_and_no_click_is_made(self):
        canvas = FakeCanvas([STRIPES])
        page = FakePage(canvas)
        fake = FakePlaywright(page)
        run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(click=(900.0, 10.0)))
        self.assertEqual(page.clicks, [])
        self.assertEqual(page.moves, [])
        self.assertTrue(any("outside the canvas" in problem for problem in run.problems), run.problems)

    def test_a_click_point_on_the_right_or_bottom_edge_is_outside_the_canvas(self):
        for point in ((400.0, 10.0), (10.0, 300.0)):
            with self.subTest(point):
                canvas = FakeCanvas([STRIPES])
                page = FakePage(canvas)
                fake = FakePlaywright(page)
                run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(click=point))
                self.assertEqual(page.clicks, [])
                self.assertTrue(any("outside the canvas" in problem for problem in run.problems), run.problems)

    def test_the_first_frame_is_waited_for_until_it_is_drawn(self):
        run, _, _, _ = self.run_browser([FLAT, FLAT, STRIPES], timeout=10.0)
        self.assertEqual(run.first_png, STRIPES)
        self.assertEqual(web_smoke.evaluate(run), [])

    def test_a_page_that_never_draws_fails_check_2_and_is_not_clicked(self):
        run, page, _, _ = self.run_browser([FLAT], timeout=0.01)
        self.assertEqual(page.clicks, [])
        problems = web_smoke.evaluate(run)
        self.assertEqual(len(problems), 1)
        self.assertIn("(2)", problems[0])

    def test_console_and_page_errors_reach_the_run(self):
        run, _, _, _ = self.run_browser([STRIPES], console=[ConsoleEntry("error", "boom", "http://h/a.js")],
                                        page_errors=["TypeError: x"])
        self.assertEqual([(entry.kind, entry.text, entry.url) for entry in run.console], [("error", "boom", "http://h/a.js")])
        self.assertEqual(run.page_errors, ["TypeError: x"])
        self.assertEqual(len(web_smoke.evaluate(run)), 2)

    def test_a_chromium_that_is_not_installed_is_browser_missing_and_the_smoke_skips(self):
        missing = Path(tempfile.gettempdir()) / "web_smoke_no_such_chromium" / "chrome.exe"
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])), executable=missing)
        api = SimpleNamespace(sync_playwright=fake.sync_playwright)
        with self.assertRaisesRegex(web_smoke.BrowserMissing, "no Chromium at"):
            web_smoke.run_browser(api, "http://h/", options())
        self.assertIsNone(fake.launch_args)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(web_smoke.run_smoke(options(), api), web_smoke.EXIT_SKIPPED)
        self.assertIn("SKIP:", out.getvalue())

    def test_a_chromium_that_is_there_but_cannot_start_is_a_failure_and_not_a_skip(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])), launch_error="Browser closed unexpectedly\nsandbox")
        api = SimpleNamespace(sync_playwright=fake.sync_playwright)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(web_smoke.run_smoke(options(), api), web_smoke.EXIT_FAILED)
        self.assertIn("FAIL:", out.getvalue())
        self.assertIn("Browser closed unexpectedly", out.getvalue())
        self.assertNotIn("SKIP", out.getvalue())

    def test_the_polls_wait_with_the_page_timer(self):
        _, page, _, _ = self.run_browser([FLAT, FLAT, STRIPES], timeout=10.0)
        self.assertIn(web_smoke.POLL_INTERVAL_SECONDS * 1000, page.waits)

    def test_a_deaf_page_gets_the_click_and_fails_check_3(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.run_smoke(options(selftest_fault="deaf"), SimpleNamespace(sync_playwright=fake.sync_playwright))
        self.assertEqual(code, 1)
        self.assertEqual(len(fake.page.clicks), 1)
        self.assertIn("FAIL: (3) the canvas looks the same after the click", out.getvalue())

    def smoke_output(self, canvas):
        fake = FakePlaywright(FakePage(canvas))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.run_smoke(options(), SimpleNamespace(sync_playwright=fake.sync_playwright))
        return code, out.getvalue(), fake.page

    def test_a_canvas_that_changes_by_itself_is_cannot_judge_and_gets_no_click(self):
        run, page, _, _ = self.run_browser([STRIPES, STRIPES, OTHER_STRIPES])
        self.assertEqual(page.clicks, [])
        self.assertIsNone(run.second_png)
        problems = web_smoke.evaluate(run)
        self.assertEqual(len(problems), 1)
        self.assertIn("(3) cannot judge", problems[0])
        code, out, page = self.smoke_output(FakeCanvas([STRIPES, STRIPES, OTHER_STRIPES], after_click=[OTHER_STRIPES]))
        self.assertEqual(code, 1)
        self.assertIn("FAIL: (3) cannot judge", out)
        self.assertNotIn("PASS", out)
        self.assertEqual(page.clicks, [])

    def test_a_canvas_that_changes_only_on_a_later_idle_shot_is_cannot_judge(self):
        # a change slower than one screenshot round trip: the first frame, the first idle shot and two more are equal
        with mock.patch.object(web_smoke, "CLICK_TIMEOUT_SECONDS", 0.5):
            run, page, _, canvas = self.run_browser([STRIPES, STRIPES, STRIPES, STRIPES, OTHER_STRIPES])
            self.assertGreaterEqual(canvas.taken, 5)
            self.assertEqual(page.clicks, [])
            self.assertIsNone(run.second_png)
            problems = web_smoke.evaluate(run)
            self.assertEqual(len(problems), 1)
            self.assertIn("(3) cannot judge: the canvas changes by itself", problems[0])
            code, out, page = self.smoke_output(FakeCanvas([STRIPES, STRIPES, STRIPES, STRIPES, OTHER_STRIPES], after_click=[OTHER_STRIPES]))
            self.assertEqual(code, 1)
            self.assertIn("FAIL: (3) cannot judge", out)
            self.assertEqual(page.clicks, [])

    def test_a_still_canvas_over_the_whole_idle_window_is_clicked_once_and_passes(self):
        with mock.patch.object(web_smoke, "CLICK_TIMEOUT_SECONDS", 0.2):
            run, page, _, canvas = self.run_browser([STRIPES])
            self.assertEqual(len(page.clicks), 1)
            self.assertGreater(canvas.taken, 5)  # the first frame, the idle shots over the window, the shot after the click
            self.assertEqual(run.before_png, STRIPES)
            self.assertEqual(run.second_png, OTHER_STRIPES)
            self.assertEqual(web_smoke.evaluate(run), [])

    def test_the_idle_window_is_as_long_as_the_window_after_the_click(self):
        with mock.patch.object(web_smoke, "poll", wraps=web_smoke.poll) as poll:
            self.run_browser([STRIPES], settle=0.02)
        timeouts = [call.args[2] for call in poll.call_args_list]
        # the first frame; the idle shots after one settle wait; the shot after the click, which also comes after one settle wait
        self.assertEqual(timeouts, [1.0, 0.02 + web_smoke.CLICK_TIMEOUT_SECONDS, web_smoke.CLICK_TIMEOUT_SECONDS])

    def test_the_pointer_is_on_the_click_point_before_the_idle_shots(self):
        _, page, _, _ = self.run_browser([STRIPES], settle=0.02)
        events = page.events
        move = events.index(("move", 210.0, 170.0))
        click = events.index(("click", 210.0, 170.0))
        before_move = [event[0] for event in events[:move]]
        self.assertEqual(before_move, ["shot", "wait"])  # only the first frame and its settle wait
        self.assertEqual(events[move + 1], ("wait", 20.0))  # the settle wait after the move, then the idle shots
        self.assertEqual(events[move + 2], ("shot",))
        between = {event[0] for event in events[move + 2:click]}
        self.assertEqual(between, {"shot", "wait"})  # no second move, no click
        self.assertEqual(events[click - 1], ("shot",))  # the last idle shot is right before the click
        self.assertEqual(events[click + 1], ("wait", 20.0))
        self.assertEqual(page.moves, [(210.0, 170.0)])

    def test_an_unreadable_shot_before_the_click_is_reported_as_such_and_not_as_a_moving_canvas(self):
        for shots in ([STRIPES, b"garbage"], [STRIPES, STRIPES, b"garbage"]):
            with self.subTest(shots=len(shots)):
                run, page, _, _ = self.run_browser(shots)
                self.assertEqual(page.clicks, [])
                problems = web_smoke.evaluate(run)
                self.assertEqual(len(problems), 1, problems)
                self.assertIn("(3) the shot before the click: the screenshot is not a readable PNG", problems[0])
                self.assertNotIn("changes by itself", problems[0])

    def test_run_smoke_serves_the_page_prints_ok_lines_and_returns_0(self):
        code, out, _ = self.smoke_output(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]))
        self.assertEqual(code, 0, out)
        self.assertIn("PASS", out)
        self.assertEqual(out.count("ok: ("), 6)  # (1) to (4), (6) and (7); (5) is skipped for the selftest page
        self.assertIn("ok: (6) manifest.webmanifest", out)
        self.assertIn("ok: (7)", out)
        self.assertIn("skip: (5)", out)
        self.assertIn("input: mouse (a click)", out)
        self.assertIn("page: crossOriginIsolated=false, typeof SharedArrayBuffer=undefined", out)

    def test_check_7_posts_one_start_piece_through_the_page_and_a_page_without_the_call_fails_it(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])))
        with contextlib.redirect_stdout(io.StringIO()):
            run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options())
        self.assertIsNone(run.upload_problems)  # no store given: not made
        answer = {"error": "the page has no window.bagiWebPost"}
        page = FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]), post_answer=answer)
        fake = FakePlaywright(page)
        out = io.StringIO()
        with contextlib.redirect_stdout(out), tempfile.TemporaryDirectory() as store:
            run = web_smoke.run_browser(SimpleNamespace(sync_playwright=fake.sync_playwright), "http://h/", options(), Path(store))
        self.assertEqual(len(page.posted), 1)
        self.assertEqual(len(run.upload_problems), 1)
        self.assertEqual(web_smoke.evaluate(run), run.upload_problems)
        self.assertIn("window.bagiWebPost", run.upload_problems[0])

    def test_the_fault_upload_fails_check_7_only_and_no_upload_check_leaves_it_out(self):
        for kwargs, code, marker in ((dict(selftest_fault="upload"), 1, "FAIL: (7)"), (dict(upload_check=False), 0, "skip: (7)"), ({}, 0, "ok: (7)")):
            with self.subTest(kwargs):
                fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES])))
                out = io.StringIO()
                with contextlib.redirect_stdout(out):
                    result = web_smoke.run_smoke(options(**kwargs), SimpleNamespace(sync_playwright=fake.sync_playwright))
                self.assertEqual(result, code, out.getvalue())
                self.assertIn(marker, out.getvalue())
                self.assertEqual(len(fake.page.posted), 0 if kwargs.get("upload_check") is False else 1)
                if code:
                    self.assertEqual(out.getvalue().count("FAIL:"), 1)
                    self.assertIn("HTTP 405", out.getvalue())

    def test_run_smoke_of_a_dist_checks_the_canvas_size_and_prints_the_ratio(self):
        with tempfile.TemporaryDirectory() as temp:
            dist = package_fake_dist(Path(temp))
            good = {"crossOriginIsolated": True, "sharedArrayBuffer": "function", "devicePixelRatio": 2, "cssWidth": 400, "cssHeight": 300,
                    "canvasWidth": 800, "canvasHeight": 600}
            for info, code in ((good, 0), (dict(good, canvasWidth=400), 1)):
                with self.subTest(width=info["canvasWidth"]):
                    fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]), info=info))
                    out = io.StringIO()
                    with contextlib.redirect_stdout(out):
                        result = web_smoke.run_smoke(options(selftest=False, dist=dist, device_scale_factor=2.0),
                                                     SimpleNamespace(sync_playwright=fake.sync_playwright))
                    self.assertEqual(result, code, out.getvalue())
                    self.assertIn("devicePixelRatio=2, CSS size 400x300", out.getvalue())
                    self.assertIn("FAIL: (5)" if code else "ok: (5)", out.getvalue())

    def test_run_smoke_of_a_dist_without_a_web_manifest_fails_check_6_only(self):
        with tempfile.TemporaryDirectory() as temp:
            (Path(temp) / "index.html").write_text("<!doctype html>", encoding="utf-8")
            good = {"crossOriginIsolated": True, "sharedArrayBuffer": "function", "devicePixelRatio": 1, "cssWidth": 400, "cssHeight": 300,
                    "canvasWidth": 400, "canvasHeight": 300}
            fake = FakePlaywright(FakePage(FakeCanvas([STRIPES], after_click=[OTHER_STRIPES]), info=good))
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                result = web_smoke.run_smoke(options(selftest=False, dist=Path(temp)), SimpleNamespace(sync_playwright=fake.sync_playwright))
            self.assertEqual(result, 1, out.getvalue())
            self.assertIn("FAIL: (6) manifest.webmanifest answered HTTP 404", out.getvalue())
            self.assertEqual(out.getvalue().count("FAIL:"), 1)

    def test_run_smoke_prints_each_reason_and_returns_1(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES]), console=[ConsoleEntry("error", "boom")]))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.run_smoke(options(), SimpleNamespace(sync_playwright=fake.sync_playwright))
        self.assertEqual(code, 1)
        self.assertIn("FAIL: (3)", out.getvalue())
        self.assertIn("FAIL: (4) console error: boom", out.getvalue())
        self.assertNotIn("PASS", out.getvalue())

    def test_a_dist_folder_without_a_page_fails_before_the_browser_starts(self):
        fake = FakePlaywright(FakePage(FakeCanvas([STRIPES])))
        with tempfile.TemporaryDirectory() as temp:
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = web_smoke.run_smoke(options(selftest=False, dist=Path(temp)), SimpleNamespace(sync_playwright=fake.sync_playwright))
        self.assertEqual(code, 1)
        self.assertIn("index.html not found", out.getvalue())
        self.assertFalse(fake.closed)
        self.assertIsNone(fake.launch_args)


class MainTest(unittest.TestCase):
    def test_without_playwright_the_smoke_is_skipped_with_exit_77(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.main(["--selftest"], load=lambda: None)
        self.assertEqual(code, 77)
        self.assertEqual(out.getvalue().strip(), "SKIP: playwright is not installed")

    def test_a_wrong_command_line_is_exit_2_also_without_playwright(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
            web_smoke.main([], load=lambda: None)
        self.assertEqual(caught.exception.code, 2)

    def test_load_playwright_answers_none_or_a_module(self):
        loaded = web_smoke.load_playwright()
        self.assertTrue(loaded is None or hasattr(loaded, "sync_playwright"))

    def test_the_script_runs_as_a_program_and_skips_or_passes_here(self):
        result = subprocess.run([web_smoke.sys.executable, web_smoke.__file__, "--selftest", "--timeout", "30"], capture_output=True, text=True,
                                timeout=300)
        if web_smoke.load_playwright() is None:
            self.assertEqual(result.returncode, 77, result.stdout + result.stderr)
            self.assertIn("SKIP: playwright is not installed", result.stdout)
        else:
            self.assertIn(result.returncode, (0, 77), result.stdout + result.stderr)


@unittest.skipUnless(web_smoke.load_playwright(), "playwright is not installed")
class RealBrowserTest(unittest.TestCase):
    """The generated page in headless Chromium: the good page passes, each fault is found. Skipped when Chromium is missing too."""

    def run_selftest(self, *argv) -> tuple[int, str]:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = web_smoke.main(["--selftest", "--timeout", "10", "--settle", "0.3", *argv])
        if code == web_smoke.EXIT_SKIPPED:
            self.skipTest(out.getvalue().strip())
        return code, out.getvalue()

    def test_the_good_page_passes(self):
        code, output = self.run_selftest()
        self.assertEqual(code, 0, output)

    def test_the_page_posts_a_piece_through_its_own_script_and_the_check_can_be_left_out(self):
        code, output = self.run_selftest()
        self.assertEqual(code, 0, output)
        self.assertIn("ok: (7)", output)
        self.assertIn("skip: (7)", self.run_selftest("--no-upload-check")[1])

    def test_a_tap_and_a_pixel_ratio_of_two_are_accepted(self):
        code, output = self.run_selftest("--touch", "--device-scale-factor", "2")
        self.assertEqual(code, 0, output)
        self.assertIn("input: touch (a tap)", output)
        self.assertIn("devicePixelRatio=2", output)

    def test_an_expected_console_message_is_found_and_a_missing_one_fails(self):
        code, output = self.run_selftest("--expect-console", "selftest page started")
        self.assertEqual(code, 0, output)
        code, output = self.run_selftest("--expect-console", "no such message", "--timeout", "2")
        self.assertEqual(code, 1, output)
        self.assertIn("FAIL: (4) no console message matched", output)

    def test_each_fault_is_found(self):
        expected = {"flat": "(2)", "console-error": "(4)", "page-error": "(1)", "deaf": "(3)", "moving": "(3) cannot judge",
                    "upload": "(7)"}
        for fault, check in expected.items():
            with self.subTest(fault):
                code, output = self.run_selftest("--selftest-fault", fault, "--timeout", "3")
                self.assertEqual(code, 1, output)
                self.assertIn(f"FAIL: {check}", output)


if __name__ == "__main__":
    unittest.main()
