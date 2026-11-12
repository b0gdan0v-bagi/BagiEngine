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


POST_SCRIPT_ID = "bagi-web-post"


def inline_scripts(html: str) -> list:
    """The bodies of the <script> elements without a src and without an id: the shell's own script (the template that holds the
    emcc script is not one of them; the post script has an id, `post_script`)."""
    bodies = []
    for attributes, body in re.findall(r"<script([^>]*)>(.*?)</script>", html, flags=re.DOTALL):
        if "src=" not in attributes and "id=" not in attributes:
            bodies.append(body)
    return bodies


def post_script(html: str) -> str:
    """The body of the script that defines window.bagiWebPost, found by its id."""
    found = re.findall(rf'<script id="{POST_SCRIPT_ID}">(.*?)</script>', html, flags=re.DOTALL)
    assert len(found) == 1, "the shell has exactly one post script"
    return found[0]


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
        # the two relative links of the home-screen app are the only references: the manifest and its icon, no URL of another origin
        urls = re.findall(r"""(?:src|href)=["']([^"']+)["']|url\(([^)]+)\)|@import""", self.markup)
        self.assertEqual(sorted(urls), [("icons/apple-touch-icon.png", ""), ("manifest.webmanifest", "")])
        self.assertNotRegex(self.markup, r"https?://")
        self.assertNotRegex(self.markup, r"""(?:src|href)=["'](?:/|[a-z]+:)""")

    def test_the_manifest_is_linked_with_credentials_and_the_apple_icon_by_a_relative_path(self):
        # a closed service uses Basic auth: a manifest request without credentials would get 401
        self.assertRegex(self.markup, r'<link rel="manifest" href="manifest\.webmanifest" crossorigin="use-credentials">')
        self.assertRegex(self.markup, r'<link rel="apple-touch-icon" href="icons/apple-touch-icon\.png">')
        self.assertNotIn("manifest.json", self.markup)  # that name is the package manifest of package.py

    def test_the_home_screen_meta_tags_are_there(self):
        for name, content in (("apple-mobile-web-app-capable", "yes"), ("mobile-web-app-capable", "yes"),
                              ("apple-mobile-web-app-status-bar-style", "black-translucent")):
            with self.subTest(name):
                self.assertRegex(self.markup, rf'<meta name="{name}" content="{content}">')
        self.assertRegex(self.markup, r'<meta name="theme-color" content="#[0-9a-fA-F]{6}">')

    def test_the_viewport_covers_the_notch(self):
        self.assertRegex(self.markup, r'name="viewport"[^>]*viewport-fit=cover')

    def test_the_toggle_button_is_hidden_until_the_script_shows_it_and_has_an_inline_glyph(self):
        button = re.search(r'<button id="fullscreen-toggle"([^>]*)>(.*?)</button>', self.markup, flags=re.DOTALL)
        self.assertIsNotNone(button, "no fullscreen toggle button")
        self.assertRegex(button.group(1), r"\bhidden\b")
        self.assertIn('type="button"', button.group(1))
        self.assertIn("aria-label=", button.group(1))
        self.assertIn("<svg", button.group(2))


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

    def test_the_toggle_button_is_small_above_the_canvas_and_off_the_notch(self):
        button = self.rule("button#fullscreen-toggle")
        self.assertRegex(button, r"position:\s*fixed")
        self.assertRegex(button, r"z-index:\s*\d+")
        self.assertRegex(button, r"env\(safe-area-inset-top")
        self.assertRegex(button, r"env\(safe-area-inset-right")
        self.assertRegex(button, r"width:\s*36px")
        self.assertRegex(self.rule("button#fullscreen-toggle[hidden]"), r"display:\s*none")

    def test_no_selector_starts_with_a_hash(self):
        # a line that starts with # can be taken for a preprocessor directive by emcc's shell handling
        self.assertNotRegex(self.css, r"(?m)^\s*#")


class ShellScriptTest(unittest.TestCase):
    def setUp(self):
        self.html = read_shell()
        scripts = inline_scripts(self.html)
        self.assertEqual(len(scripts), 1, "the shell has exactly one inline script")
        self.script = scripts[0]

    def test_the_module_object_has_the_hooks_of_the_loading_bar_and_of_the_errors(self):
        for hook in ("canvas:", "setStatus:", "monitorRunDependencies:", "onAbort:", "onExit:", "print:", "printErr:"):
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

    def test_the_webgl_test_frees_its_context_and_a_failure_reloads_a_bounded_number_of_times(self):
        self.assertIn('getExtension("WEBGL_lose_context")', self.script)
        self.assertIn("loseContext()", self.script)
        failure = re.search(r"if \(!hasWebGL\(\)\) \{(.*?)\n\s{12}\}", self.script, flags=re.DOTALL)
        self.assertIsNotNone(failure)
        block = failure.group(1)
        # a reload only while the per-tab budget lasts, then the message; the budget is reset on success and on the message
        self.assertIn("reloads < kWebGLReloads", block)
        self.assertIn("window.location.reload()", block)
        self.assertLess(block.index("window.location.reload()"), block.index("showError("))
        self.assertIn("setWebGLReloads(0);", block)
        self.assertRegex(self.script, r"\n\s{12}\}\n\s{12}setWebGLReloads\(0\);")
        self.assertRegex(self.script, r"var kWebGLReloads = [1-3];")

    def test_the_thread_flag_placeholder_is_there_once_and_is_the_only_pair_cmake_would_replace(self):
        # configure_file(@ONLY) in be_web_generate_shell replaces every @name@; the CSS has "@supports" and must stay alone
        self.assertEqual(self.html.count("@BAGI_WEB_THREADS@"), 1)
        self.assertEqual(re.findall(r"@[A-Za-z0-9_.+/-]+@", self.html), ["@BAGI_WEB_THREADS@"])
        self.assertRegex(self.script, r'var threaded = "@BAGI_WEB_THREADS@" === "1";')

    def test_a_threaded_page_without_cross_origin_isolation_shows_a_message_and_does_not_start_the_module(self):
        branch = re.search(r"if \(threaded && (.*?)\) \{\n(.*?)\n\s{12}\}", self.script, flags=re.DOTALL)
        self.assertIsNotNone(branch, "no branch for a threaded page that is not isolated")
        condition, body = branch.group(1), branch.group(2)
        self.assertIn("crossOriginIsolated", condition)
        self.assertIn("SharedArrayBuffer", condition)
        self.assertIn("showError(", body)
        self.assertIn("This page needs cross-origin isolation", body)
        self.assertIn("Cross-Origin-Opener-Policy", body)
        self.assertIn("Cross-Origin-Embedder-Policy", body)
        self.assertRegex(body, r"\breturn;")
        # The test comes before the code that sets Module up and before the one that starts the script of emcc
        start = self.script.index("if (threaded &&")
        self.assertLess(start, self.script.index("window.Module"))
        self.assertLess(start, self.script.index("document.body.appendChild(script)"))

    def test_the_message_is_text_not_html(self):
        self.assertNotIn("innerHTML", self.script)
        self.assertNotIn("document.write", self.script)

    def test_an_error_of_the_page_shows_a_message(self):
        self.assertRegex(self.script, r'addEventListener\("error"')

    def test_a_package_that_does_not_download_shows_a_message(self):
        # emcc's loader of the .data package is an async function nobody awaits: its failure is an unhandled rejection
        match = re.search(r'addEventListener\("unhandledrejection".*?\n\s{12}\}\);', self.script, flags=re.DOTALL)
        self.assertIsNotNone(match, "no handler for an unhandled rejection")
        self.assertIn("showError(", match.group(0))

    def test_a_rejection_after_the_game_runs_is_only_logged(self):
        match = re.search(r'addEventListener\("unhandledrejection".*?\n\s{12}\}\);', self.script, flags=re.DOTALL)
        self.assertIsNotNone(match)
        handler = match.group(0)
        self.assertRegex(handler, re.compile(r"if \(running\) \{\s*console\.error\(.*?return;", re.DOTALL))
        self.assertLess(handler.index("if (running)"), handler.index("showError("))
        self.assertRegex(self.script, r"if \(!text\) \{\s*running = true;")

    def test_a_download_total_that_the_count_has_passed_does_not_drive_the_bar(self):
        # emcc divides the decoded bytes by the Content-Length, which is the compressed size when the server sends .br / .gz
        match = re.search(r"setStatus:\s*function.*?\n\s{16}\},", self.script, flags=re.DOTALL)
        self.assertIsNotNone(match)
        self.assertRegex(match.group(0), r"done > total")
        self.assertIn("totalIsEncoded", match.group(0))

    def test_a_game_script_that_does_not_download_shows_a_message(self):
        self.assertIn("script.onerror", self.script)

    def fullscreen_script(self) -> str:
        start = self.script.index("function setUpFullscreen()")
        return self.script[start:self.script.index("loadingPanel.hidden = false;", start)]

    def test_fullscreen_is_set_up_after_the_webgl_test_and_before_the_game_starts_and_its_failure_is_swallowed(self):
        self.assertIn("setUpFullscreen();", self.script)
        call = self.script.index("setUpFullscreen();")
        self.assertGreater(call, self.script.index("if (!hasWebGL())"))
        self.assertLess(call, self.script.index("document.body.appendChild(script)"))
        self.assertRegex(self.script, r"try \{\s*setUpFullscreen\(\);\s*\} catch \(error\)")

    def test_the_fullscreen_api_is_asked_before_it_is_used_with_the_webkit_forms(self):
        text = self.fullscreen_script()
        for name in ("document.fullscreenEnabled", "webkitFullscreenEnabled", "root.requestFullscreen", "webkitRequestFullscreen",
                     "document.exitFullscreen", "webkitExitFullscreen", "webkitFullscreenElement", '"webkitfullscreenchange"',
                     '"fullscreenchange"'):
            with self.subTest(name):
                self.assertIn(name, text)
        self.assertRegex(text, r"if \(!toggle \|\| !enabled \|\| installed\) \{\s*return;")

    def test_an_installed_app_is_not_offered_fullscreen_and_the_display_mode_is_read_before_the_request(self):
        text = self.fullscreen_script()
        for name in ("navigator.standalone", "(display-mode: standalone)", "(display-mode: fullscreen)"):
            with self.subTest(name):
                self.assertIn(name, text)
        self.assertLess(text.index("var installed"), text.index("requestFn.call"))

    def test_the_first_touch_asks_once_and_only_where_the_pointer_is_coarse_and_the_switch_is_not_off(self):
        text = self.fullscreen_script()
        self.assertIn("fullscreen=0", text)
        self.assertIn("(pointer: coarse)", text)
        self.assertRegex(text, r'addEventListener\("pointerup", onFirstTap, true\)')
        self.assertRegex(text, r'event\.pointerType !== "touch"')
        self.assertRegex(text, r"stopAskingOnTap\(\);\s*if \(fullscreenElement\(\) === null\)")

    def test_the_orientation_comes_from_the_manifest_and_is_locked_after_fullscreen_with_errors_ignored(self):
        text = self.fullscreen_script()
        self.assertIn('fetch("manifest.webmanifest")', text)
        self.assertIn("manifest.orientation", text)
        self.assertIn("screenOrientation.lock(orientation)", text)
        self.assertRegex(text, r'orientation === "any"')
        self.assertRegex(text, r"lock\(orientation\);\s*if \(result && typeof result\.catch === \"function\"\) \{\s*result\.catch")

    def test_every_promise_of_the_fullscreen_code_has_a_rejection_handler(self):
        # the page's handler of "unhandledrejection" would show "The game could not be loaded" for one without
        text = self.fullscreen_script()
        self.assertRegex(text, r"\}\)\.catch\(function \(\) \{\}\);")  # the manifest read
        self.assertIn("result.then(function () {}, logRefusal)", text)
        self.assertEqual(text.count("result.then(function () {}, logRefusal)"), 2)  # enter and leave
        self.assertRegex(text, re.compile(r"manifestRead\.then\(function \(\) \{.*?\}\)\.catch\(function \(\) \{\}\);", re.DOTALL))

    def test_the_toggle_button_takes_its_own_touches(self):
        text = self.fullscreen_script()
        for name in ("pointerdown", "touchstart", "mousedown", "click"):
            with self.subTest(name):
                self.assertIn(f'"{name}"', text)
        self.assertIn("event.stopPropagation()", text)
        self.assertIn("toggle.contains(event.target)", text)  # its own tap is not the "first tap" that asks for fullscreen

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_inline_script_is_valid_javascript(self):
        with tempfile.TemporaryDirectory(prefix="bagi_shell_") as scratch:
            path = Path(scratch) / "shell_script.js"
            path.write_text(self.script, encoding="utf-8")
            result = subprocess.run([NODE, "--check", str(path)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)


# The prelude of the node cases: the script runs as the body of a function whose parameters shadow the globals it uses, so
# `fetch`, `Date` and `TextEncoder` are the test's. `fetch` records the call and returns a promise that the case settles by hand.
NODE_PRELUDE = r"""
const assert = require("assert");
const fs = require("fs");
const source = fs.readFileSync(process.argv[2], "utf8");
function load(fetchOverride) {
    const world = {window: {}, calls: [], clock: {t: 1000000, now() { return this.t; }}};
    world.fetch = (path, options) => {
        const call = {path, options, resolve: null, reject: null};
        call.promise = new Promise((resolve, reject) => { call.resolve = resolve; call.reject = reject; });
        world.calls.push(call);
        return call.promise;
    };
    new Function("window", "fetch", "Date", "TextEncoder", source)(world.window, fetchOverride || world.fetch, world.clock, TextEncoder);
    world.post = world.window.bagiWebPost;
    return world;
}
const tick = () => new Promise((resolve) => setImmediate(resolve));
(async () => {
"""
NODE_EPILOGUE = "\n})().catch((error) => { console.error(error); process.exit(1); });\n"


class ShellWebPostTest(unittest.TestCase):
    def setUp(self):
        self.html = read_shell()
        self.markup = strip_comments(self.html)
        self.script = post_script(self.html)

    def test_the_post_script_is_outside_the_template_and_before_the_webgl_gate(self):
        self.assertEqual(self.html.count("{{{"), 1)
        self.assertEqual(self.markup.count("{{{ SCRIPT }}}"), 1)
        template_end = self.markup.index("</template>")
        self.assertLess(template_end, self.markup.index(f'<script id="{POST_SCRIPT_ID}">'))
        # a script of its own that comes before the shell's, whose first act is the WebGL test
        self.assertLess(self.markup.index(f'<script id="{POST_SCRIPT_ID}">'), self.markup.index("hasWebGL()"))
        self.assertNotIn("template", self.script)

    def test_it_is_a_property_of_window_with_start_and_poll_and_names_no_game(self):
        self.assertRegex(self.script, r"window\.bagiWebPost\s*=\s*\{start:\s*start,\s*poll:\s*poll\}")
        self.assertNotRegex(self.script, r"(?i)mahjong|match3")
        self.assertNotIn("console.", self.script)  # neither paths nor bodies are logged

    def test_the_numbers_of_the_contract(self):
        for text in ("1048576", "2048", "= 64;", "60000", "x-ndjson", 'credentials: "same-origin"', 'method: "POST"'):
            with self.subTest(text):
                self.assertIn(text, self.script)

    def run_node(self, cases: str):
        with tempfile.TemporaryDirectory(prefix="bagi_post_") as scratch:
            script_path = Path(scratch) / "post_script.js"
            script_path.write_text(self.script, encoding="utf-8")
            driver = Path(scratch) / "driver.js"
            driver.write_text(NODE_PRELUDE + cases + NODE_EPILOGUE, encoding="utf-8")
            result = subprocess.run([NODE, str(driver), str(script_path)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_post_sends_method_path_body_content_type_and_credentials(self):
        self.run_node(r"""
            const w = load();
            const id = w.post.start("/matches/abc/0", "{\"a\":1}\n", "text/plain");
            assert.ok(id > 0);
            assert.strictEqual(w.calls.length, 1);
            const call = w.calls[0];
            assert.strictEqual(call.path, "/matches/abc/0");
            assert.strictEqual(call.options.method, "POST");
            assert.strictEqual(call.options.body, "{\"a\":1}\n");
            assert.strictEqual(call.options.headers["Content-Type"], "text/plain");
            assert.strictEqual(call.options.credentials, "same-origin");
            assert.strictEqual(call.options.cache, "no-store");
            assert.strictEqual(call.options.keepalive, true);
            w.post.start("/x", "", undefined);
            assert.strictEqual(w.calls[1].options.headers["Content-Type"], "application/x-ndjson");
            assert.strictEqual(w.calls[1].options.body, "");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_keepalive_is_set_up_to_60000_bytes_and_a_non_ascii_body_counts_in_bytes(self):
        self.run_node(r"""
            const w = load();
            w.post.start("/a", "x".repeat(60000), "t");
            w.post.start("/a", "x".repeat(60001), "t");
            assert.strictEqual(w.calls[0].options.keepalive, true);
            assert.strictEqual(w.calls[1].options.keepalive, false);
            // 30000 two-byte characters are 60000 bytes, 30001 are 60002
            w.post.start("/a", "\u00e9".repeat(30000), "t");
            w.post.start("/a", "\u00e9".repeat(30001), "t");
            assert.strictEqual(w.calls[2].options.keepalive, true);
            assert.strictEqual(w.calls[3].options.keepalive, false);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_poll_is_pending_then_the_status_once_then_unknown(self):
        self.run_node(r"""
            const w = load();
            const id = w.post.start("/a", "x", "t");
            assert.strictEqual(w.post.poll(id), -1);
            w.calls[0].resolve({status: 409});
            assert.strictEqual(w.post.poll(id), -1);  // the promise has not run its handlers yet
            await tick();
            assert.strictEqual(w.post.poll(id), 409);
            assert.strictEqual(w.post.poll(id), 0);
            assert.strictEqual(w.post.poll(99999), 0);
            assert.strictEqual(w.post.poll(0), 0);
            assert.strictEqual(w.post.poll("__proto__"), 0);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_rejecting_fetch_or_one_that_throws_polls_as_zero_and_nothing_is_unhandled(self):
        self.run_node(r"""
            process.on("unhandledRejection", (reason) => { throw reason; });
            const w = load();
            const id = w.post.start("/a", "x", "t");
            w.calls[0].reject(new TypeError("Failed to fetch"));
            await tick();
            assert.strictEqual(w.post.poll(id), 0);
            // a fetch that throws at once (a bad header value) is a refusal, and its slot is free again
            const throwing = load(() => { throw new TypeError("bad header"); });
            for (let i = 0; i < 100; ++i) {
                assert.strictEqual(throwing.post.start("/a", "x", "bad\\nvalue"), 0);
            }
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_path_that_is_not_of_this_origin_is_refused(self):
        self.run_node(r"""
            const w = load();
            const refused = ["http://example.com/x", "https://example.com/x", "//host/x", "matches/x", "", "/a b", "/a\tb", "/a\nb",
                             "/a\\b", "/a/../b", "/..", "/\u00e9", "/a\u007f", "javascript:alert(1)", 42, null, undefined, "/" + "a".repeat(2048)];
            for (const path of refused) {
                assert.strictEqual(w.post.start(path, "x", "t"), 0, String(path));
            }
            assert.strictEqual(w.calls.length, 0);
            assert.ok(w.post.start("/" + "a".repeat(2047), "x", "t") > 0);
            assert.ok(w.post.start("/matches/0123456789abcdef0123456789abcdef/0", "x", "t") > 0);
            assert.ok(w.post.start("/a.b/c?d=1", "x", "t") > 0);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_body_above_one_mebibyte_or_not_a_string_is_refused(self):
        self.run_node(r"""
            const w = load();
            assert.ok(w.post.start("/a", "x".repeat(1048576), "t") > 0);
            assert.strictEqual(w.post.start("/a", "x".repeat(1048577), "t"), 0);
            // under the limit in characters, over it in bytes
            assert.strictEqual(w.post.start("/a", "\u00e9".repeat(524289), "t"), 0);
            assert.ok(w.post.start("/a", "\u00e9".repeat(524288), "t") > 0);
            assert.strictEqual(w.post.start("/a", 5, "t"), 0);
            assert.strictEqual(w.post.start("/a", null, "t"), 0);
            assert.strictEqual(w.calls.length, 2);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_65th_concurrent_post_is_refused_and_a_finished_one_frees_its_slot(self):
        self.run_node(r"""
            const w = load();
            const ids = [];
            for (let i = 0; i < 64; ++i) {
                ids.push(w.post.start("/a", "x", "t"));
                assert.ok(ids[i] > 0);
            }
            assert.strictEqual(w.post.start("/a", "x", "t"), 0);
            w.calls[0].resolve({status: 200});
            await tick();
            assert.ok(w.post.start("/a", "x", "t") > 0);   // finished, though not yet polled
            assert.strictEqual(w.post.start("/a", "x", "t"), 0);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_an_id_nobody_polls_expires_after_a_minute_and_a_polled_one_does_not(self):
        self.run_node(r"""
            const w = load();
            const lost = w.post.start("/a", "x", "t");
            const kept = w.post.start("/a", "x", "t");
            w.clock.t += 50000;
            assert.strictEqual(w.post.poll(kept), -1);       // touching it keeps it
            w.clock.t += 50000;
            w.post.start("/a", "x", "t");                    // the purge runs on use
            assert.strictEqual(w.post.poll(lost), 0);
            assert.strictEqual(w.post.poll(kept), -1);
            // a lost pending post also frees its slot
            const w2 = load();
            for (let i = 0; i < 64; ++i) { w2.post.start("/a", "x", "t"); }
            assert.strictEqual(w2.post.start("/a", "x", "t"), 0);
            w2.clock.t += 60001;
            assert.ok(w2.post.start("/a", "x", "t") > 0);
        """)


if __name__ == "__main__":
    unittest.main()
