"""Tests of tools/web/web_game.py that need no browser: the flags a game executable takes, the address of the page, when the
stand-in is skipped and how a console line ends it.
Run: python -m unittest discover tools/web/tests"""

import contextlib
import io
import os
import sys
import tempfile
import unittest
import urllib.parse
from pathlib import Path
from unittest import mock

import web_fixtures  # noqa: F401  (puts tools/web on sys.path)

import web_game

GAME_FLAGS = ["--control", "--control-port=8800", "--headless", "--manual-clock"]


def run_main(argv: list[str], environ: dict, venv: Path) -> tuple[int, str]:
    output = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
        code = web_game.main(argv, environ, venv)
    return code, output.getvalue()


class WebGameFlagsTest(unittest.TestCase):
    def test_the_flags_of_a_harness_set_the_port_the_clock_and_the_viewport(self):
        flags = web_game.parse_flags([*GAME_FLAGS, "--window=480x320", "--density=2"])
        self.assertEqual((flags.control_port, flags.manual_clock, flags.viewport, flags.density), (8800, True, (480, 320), 2.0))

    def test_without_them_a_page_has_the_default_port_the_real_clock_and_the_default_viewport(self):
        flags = web_game.parse_flags(["--control"])
        self.assertEqual((flags.control_port, flags.manual_clock, flags.viewport, flags.density),
                         (web_game.DEFAULT_CONTROL_PORT, False, web_game.DEFAULT_VIEWPORT, 1.0))

    def test_a_flag_given_twice_keeps_its_first_value(self):
        self.assertEqual(web_game.parse_flags(["--control", "--control-port=8800", "--control-port=8801"]).control_port, 8800)

    def test_a_flag_the_page_cannot_honour_is_refused(self):
        for argument in ("--editor", "--backend=software", "--replay=a.jsonl", "--content-overlay=/tmp/x", "--manual-clock=false",
                         "--control-port", "--control-port=70000", "--control-port=-1", "--window=480", "--window=0x10",
                         "--density=0", "--density=nan", "--density=big", "scene.jsonl"):
            with self.subTest(argument), self.assertRaises(web_game.UsageError):
                web_game.parse_flags(["--control", argument])

    def test_a_run_without_the_control_api_is_refused(self):
        with self.assertRaises(web_game.UsageError):
            web_game.parse_flags(["--headless"])


class WebGamePageTest(unittest.TestCase):
    def test_the_page_is_told_the_token_the_page_port_and_the_manual_clock(self):
        url = web_game.page_url("http://127.0.0.1:5000/", web_game.Flags(manual_clock=True), "t0ken_-", 5001)
        address = urllib.parse.urlsplit(url)
        self.assertEqual(address.netloc, "127.0.0.1:5000")
        self.assertEqual(urllib.parse.parse_qs(address.query),
                         {"fullscreen": ["0"], "controlToken": ["t0ken_-"], "control": ["5001"], "controlClock": ["manual"]})

    def test_the_real_clock_asks_for_no_clock(self):
        self.assertNotIn("controlClock", web_game.page_url("http://127.0.0.1:5000/", web_game.Flags(), "t", 5001))

    def test_the_shells_end_lines_decide_the_exit_code(self):
        self.assertEqual(web_game.console_outcome("The game ended"), web_game.EXIT_ENDED)
        self.assertEqual(web_game.console_outcome("The game ended (exit status 3)"), web_game.EXIT_FAILED)
        self.assertEqual(web_game.console_outcome("The game aborted: unreachable"), web_game.EXIT_FAILED)
        self.assertIsNone(web_game.console_outcome("[Info] [12:00:00.000] The game ended the round"))

    def test_the_lines_are_the_ones_the_shell_prints(self):
        shell = (web_game.ENGINE_ROOT / "web" / "shell.html").read_text(encoding="utf-8")
        self.assertIn('console.log("The game ended" + (failedExit ? " (exit status " + status + ")" : ""));', shell)
        self.assertIn(f'console.error("{web_game.ABORTED_PREFIX}" + text);', shell)


class WebGameMainTest(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = Path(folder.name)
        self.dist = self.folder / "dist"
        self.dist.mkdir()
        (self.dist / web_game.PAGE_NAME).write_text("<!doctype html>", encoding="utf-8")
        self.no_venv = self.folder / "web-venv"

    def test_a_wrong_command_line_exits_2_before_anything_is_looked_for(self):
        code, output = run_main(["--control", "--editor"], {}, self.no_venv)
        self.assertEqual(code, web_game.EXIT_USAGE)
        self.assertIn("--editor", output)

    def test_an_unknown_browser_exits_2(self):
        environ = {web_game.DIST_VARIABLE: str(self.dist), web_game.BROWSER_VARIABLE: "lynx"}
        self.assertEqual(run_main(["--control"], environ, self.no_venv)[0], web_game.EXIT_USAGE)

    def test_without_a_dist_the_run_is_skipped(self):
        for environ in ({}, {web_game.DIST_VARIABLE: str(self.folder / "nothing")}, {web_game.DIST_VARIABLE: str(self.folder)}):
            with self.subTest(environ=environ):
                code, output = run_main(["--control"], environ, self.no_venv)
                self.assertEqual(code, web_game.EXIT_SKIPPED)
                self.assertIn(web_game.DIST_VARIABLE, output)

    def test_a_relative_dist_is_taken_from_the_engine_root(self):
        # A harness starts the executable in its own folder, so the current directory says nothing
        relative = os.path.relpath(self.dist, web_game.ENGINE_ROOT)
        self.assertEqual(web_game.find_dist(relative).resolve(), self.dist.resolve())

    def test_without_playwright_and_a_venv_the_run_is_skipped(self):
        with mock.patch.object(web_game, "load_playwright", return_value=None):
            code, output = run_main(["--control"], {web_game.DIST_VARIABLE: str(self.dist)}, self.no_venv)
        self.assertEqual(code, web_game.EXIT_SKIPPED)
        self.assertIn("Playwright", output)

    @unittest.skipIf(sys.platform == "win32", "Windows runs a second process instead of exec")
    def test_without_playwright_the_script_starts_again_under_the_venv_with_its_arguments(self):
        python = web_game.venv_python(self.no_venv)
        python.parent.mkdir(parents=True)
        python.touch()
        with mock.patch.object(web_game.os, "execv") as execv:
            web_game.restart_under_venv(self.no_venv, ["--control", "--headless"])
        execv.assert_called_once_with(str(python), [str(python), str(Path(web_game.__file__).resolve()), "--control", "--headless"])

    def test_the_venv_that_has_no_playwright_is_not_started_again(self):
        # The interpreter that runs the tests stands for the venv's own
        with mock.patch.object(web_game.os, "execv") as execv, mock.patch.object(web_game.Path, "is_file", return_value=True):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(web_game.restart_under_venv(Path(sys.prefix), ["--control"]), web_game.EXIT_SKIPPED)
        execv.assert_not_called()


if __name__ == "__main__":
    unittest.main()
