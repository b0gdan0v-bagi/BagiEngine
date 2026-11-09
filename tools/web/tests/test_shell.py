"""Text checks of web/shell.html, the page around the wasm (emcc --shell-file). Whether it works in a browser is checked with
emsdk; here the file is read as text, and its inline script is syntax-checked with node when node is on PATH.

    python -m unittest discover tools/web/tests
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[3]
SHELL_FILE = ENGINE_DIR / "web" / "shell.html"
NODE = shutil.which("node")


def read_shell() -> str:
    return SHELL_FILE.read_text(encoding="utf-8")


def inline_scripts(html: str) -> list:
    """The bodies of the <script> elements without a src (the template that holds the emcc script is not one of them)."""
    bodies = []
    for attributes, body in re.findall(r"<script([^>]*)>(.*?)</script>", html, flags=re.DOTALL):
        if "src=" not in attributes:
            bodies.append(body)
    return bodies


def strip_comments(html: str) -> str:
    return re.sub(r"<!--.*?-->", "", html, flags=re.DOTALL)


class ShellMarkupTest(unittest.TestCase):
    def setUp(self):
        self.html = read_shell()
        self.markup = strip_comments(self.html)

    def test_the_script_placeholder_is_there_once_and_not_in_a_comment(self):
        self.assertEqual(self.html.count("{{{ SCRIPT }}}"), 1)
        self.assertEqual(self.markup.count("{{{ SCRIPT }}}"), 1)

    def test_no_other_triple_braces_for_emcc_to_substitute(self):
        self.assertEqual(self.html.count("{{{"), 1)

    def test_the_placeholder_is_in_a_template_so_the_webgl_test_decides_whether_it_runs(self):
        self.assertRegex(self.markup, r"<template[^>]*>\s*\{\{\{ SCRIPT \}\}\}\s*</template>")

    def test_the_canvas_has_the_id_sdl_looks_for(self):
        self.assertRegex(self.markup, r'<canvas[^>]*\bid="canvas"')

    def test_there_is_a_progress_element(self):
        self.assertRegex(self.markup, r"<progress\b")

    def test_there_is_a_noscript_message(self):
        self.assertRegex(self.markup, r"(?s)<noscript>.+?</noscript>")

    def test_there_is_a_viewport_meta(self):
        self.assertRegex(self.markup, r'<meta[^>]*name="viewport"[^>]*content="[^"]*width=device-width')

    def test_it_declares_a_charset_and_a_language(self):
        self.assertRegex(self.markup, r'<meta charset="utf-8">')
        self.assertRegex(self.markup, r'<html lang="[a-z-]+">')

    def test_nothing_is_loaded_from_outside(self):
        urls = re.findall(r"""(?:src|href)=["']([^"']+)["']|url\(([^)]+)\)|@import""", self.markup)
        self.assertEqual(urls, [])
        self.assertNotRegex(self.markup, r"https?://")


class ShellStyleTest(unittest.TestCase):
    def setUp(self):
        match = re.search(r"<style>(.*?)</style>", read_shell(), flags=re.DOTALL)
        self.assertIsNotNone(match, "no <style> element")
        self.css = re.sub(r"/\*.*?\*/", "", match.group(1), flags=re.DOTALL)

    def rule(self, selector: str) -> str:
        """The body of the first rule whose selector is exactly `selector`."""
        match = re.search(r"(?:^|\})\s*" + re.escape(selector) + r"\s*\{([^}]*)\}", self.css)
        self.assertIsNotNone(match, f"no rule for {selector}")
        return match.group(1)

    def test_the_canvas_fills_the_window(self):
        canvas = self.rule("canvas#canvas")
        self.assertRegex(canvas, r"width:\s*100vw")
        self.assertRegex(canvas, r"height:\s*100vh")
        self.assertRegex(canvas, r"position:\s*fixed")

    def test_the_page_has_no_scrollbars_and_no_margin(self):
        page = self.rule("html, body")
        self.assertRegex(page, r"overflow:\s*hidden")
        self.assertRegex(page, r"margin:\s*0")
        self.assertRegex(page, r"padding:\s*0")

    def test_the_canvas_gives_the_touches_to_the_game(self):
        self.assertRegex(self.rule("canvas#canvas"), r"touch-action:\s*none")


class ShellScriptTest(unittest.TestCase):
    def setUp(self):
        self.html = read_shell()
        scripts = inline_scripts(self.html)
        self.assertEqual(len(scripts), 1, "the shell has exactly one inline script")
        self.script = scripts[0]

    def test_the_module_object_has_the_hooks_of_the_loading_bar_and_of_the_errors(self):
        for hook in ("canvas:", "setStatus:", "monitorRunDependencies:", "onAbort:", "print:", "printErr:"):
            with self.subTest(hook):
                self.assertIn(hook, self.script)
        self.assertRegex(self.script, r"window\.Module\s*=")

    def test_the_log_goes_to_the_browser_console(self):
        self.assertIn("console.log", self.script)
        self.assertIn("console.error", self.script)

    def test_the_loading_bar_is_fed_by_the_status_and_by_the_run_dependencies(self):
        self.assertIn("loadingBar.value", self.script)
        match = re.search(r"setStatus:\s*function.*?\n\s{16}\},", self.script, flags=re.DOTALL)
        self.assertIsNotNone(match)
        self.assertIn("setProgress", match.group(0))
        match = re.search(r"monitorRunDependencies:\s*function.*?\n\s{16}\},", self.script, flags=re.DOTALL)
        self.assertIsNotNone(match)
        self.assertIn("setProgress", match.group(0))

    def test_webgl_is_tested_with_both_contexts_and_a_failure_stops_before_the_game_starts(self):
        self.assertIn('getContext("webgl2")', self.script)
        self.assertIn('getContext("webgl")', self.script)
        failure = re.search(r"if \(!hasWebGL\(\)\) \{(.*?)\n\s{12}\}", self.script, flags=re.DOTALL)
        self.assertIsNotNone(failure, "no branch for a missing WebGL")
        self.assertIn("showError(", failure.group(1))
        self.assertRegex(failure.group(1), r"\breturn;")
        # The test comes before the code that sets Module up and before the one that starts the script of emcc
        self.assertLess(self.script.index("if (!hasWebGL())"), self.script.index("window.Module"))
        self.assertLess(self.script.index("if (!hasWebGL())"), self.script.index("document.body.appendChild(script)"))

    def test_the_message_is_text_not_html(self):
        self.assertNotIn("innerHTML", self.script)
        self.assertNotIn("document.write", self.script)

    def test_an_error_of_the_page_shows_a_message(self):
        self.assertRegex(self.script, r'addEventListener\("error"')

    def test_a_game_script_that_does_not_download_shows_a_message(self):
        self.assertIn("script.onerror", self.script)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_inline_script_is_valid_javascript(self):
        with tempfile.TemporaryDirectory(prefix="bagi_shell_") as scratch:
            path = Path(scratch) / "shell_script.js"
            path.write_text(self.script, encoding="utf-8")
            result = subprocess.run([NODE, "--check", str(path)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
