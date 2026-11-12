"""web/saves.js, the page half of the browser save folder (IndexedDB through Emscripten's IDBFS), run under node with a fake FS, fake
timers and a fake window. Whether IndexedDB really keeps the files across a reload is the browser smoke (web_smoke.py check (8)).

    python -m unittest discover tools/web/tests -p test_saves_js.py

The cases skip without node on PATH.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parents[3]
SAVES_FILE = ENGINE_DIR / "web" / "saves.js"
STORAGE_HEADER = ENGINE_DIR / "src" / "Modules" / "BECore" / "Save" / "SaveStorage.h"
NODE = shutil.which("node")


def read_saves() -> str:
    return SAVES_FILE.read_text(encoding="utf-8")


def named_number(source: str, name: str) -> int:
    found = re.search(rf"\bvar {name} = (\d+);", source)
    assert found, f"{name} is a numeric var of saves.js"
    return int(found.group(1))


# A world of fakes. The page is booted by hand (boot() runs Module.preRun the way emcc does); the start of the game is "no run
# dependency left". Timers run on a virtual clock (advance). FS.syncfs only records the call: the case answers it (settle).
NODE_PRELUDE = r"""
const assert = require("assert");
const fs = require("fs");
const source = fs.readFileSync(process.argv[2], "utf8");
const DELAY = %(delay)d, MAX_WAIT = %(max_wait)d, TIMEOUT = %(timeout)d;

function load(options) {
    options = options || {};
    const world = {
        now: 0, timers: [], nextTimer: 1, deps: new Set(), removed: 0, warnings: [], mkdirs: [], mounts: [], syncs: [],
        windowListeners: {}, documentListeners: {}, document: {visibilityState: "visible"},
    };
    world.Module = options.preRun === undefined ? {} : {preRun: options.preRun};
    world.FS = {
        mkdirTree(path) { world.mkdirs.push(path); },
        mount(type, opts, path) { world.mounts.push({type, opts, path}); },
        syncfs(populate, callback) {
            if (options.syncThrows) { throw new Error("syncfs threw"); }
            world.syncs.push({populate, callback});
        },
    };
    world.IDBFS = {name: "IDBFS"};
    world.setTimeout = (fn, ms) => {
        const id = world.nextTimer++;
        world.timers.push({id, at: world.now + ms, fn});
        return id;
    };
    world.clearTimeout = (id) => { world.timers = world.timers.filter((timer) => timer.id !== id); };
    world.advance = (ms) => {
        const end = world.now + ms;
        for (;;) {
            const due = world.timers.filter((timer) => timer.at <= end).sort((a, b) => a.at - b.at || a.id - b.id)[0];
            if (!due) { break; }
            world.timers = world.timers.filter((timer) => timer !== due);
            world.now = due.at;
            due.fn();
        }
        world.now = end;
    };
    const listen = (table) => (type, fn) => { (table[type] = table[type] || []).push(fn); };
    world.window = options.noWindow ? undefined : {addEventListener: listen(world.windowListeners)};
    world.document.addEventListener = listen(world.documentListeners);
    const console_ = {warn: (text) => world.warnings.push(String(text))};
    new Function("Module", "window", "document", "FS", "IDBFS", "indexedDB", "addRunDependency", "removeRunDependency",
                 "setTimeout", "clearTimeout", "ENVIRONMENT_IS_PTHREAD", "console", source)(
        world.Module, world.window, world.document, options.noFS ? undefined : world.FS, options.noIDBFS ? undefined : world.IDBFS,
        options.noIndexedDB ? undefined : {}, (id) => { world.deps.add(id); },
        (id) => { world.removed++; world.deps.delete(id); }, world.setTimeout, world.clearTimeout,
        options.pthread ? true : undefined, console_);
    world.boot = () => {
        const list = world.Module.preRun;
        while (list && list.length) { list.shift()(); }
    };
    world.fire = (table, type) => (table[type] || []).forEach((fn) => fn());
    world.saves = () => world.Module.bagiSaves;
    // The page comes up with the stored files loaded: persistent, the start released
    world.bootPersistent = () => {
        world.boot();
        world.syncs[0].callback(null);
        assert.strictEqual(world.saves().state, "persistent");
        world.syncs.length = 0;
    };
    world.writes = () => world.syncs.filter((call) => !call.populate);
    return world;
}
(() => {
"""
NODE_EPILOGUE = "\n})();\n"


class SavesJsTest(unittest.TestCase):
    def setUp(self):
        self.script = read_saves()
        self.prelude = NODE_PRELUDE % {
            "delay": named_number(self.script, "kFlushDelayMs"),
            "max_wait": named_number(self.script, "kFlushMaxWaitMs"),
            "timeout": named_number(self.script, "kLoadTimeoutMs"),
        }

    def run_node(self, cases: str):
        with tempfile.TemporaryDirectory(prefix="bagi_saves_") as scratch:
            script_path = Path(scratch) / "saves.js"
            script_path.write_text(self.script, encoding="utf-8")
            driver = Path(scratch) / "driver.js"
            driver.write_text(self.prelude + cases + NODE_EPILOGUE, encoding="utf-8")
            result = subprocess.run([NODE, str(driver), str(script_path)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    # (g), (h): the file itself

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_file_is_valid_javascript(self):
        result = subprocess.run([NODE, "--check", str(SAVES_FILE)], capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_it_names_no_game_and_logs_only_warnings(self):
        self.assertNotRegex(self.script, r"(?i)mahjong|match3")
        self.assertEqual(set(re.findall(r"console\.(\w+)", self.script)), {"warn"})

    def test_the_folder_is_the_one_of_the_cpp_constant(self):
        cpp = re.search(r'kWebSaveDirectory\s*=\s*"([^"]+)"', STORAGE_HEADER.read_text(encoding="utf-8"))
        js = re.search(r'\bvar kSaveDirectory = "([^"]+)";', self.script)
        self.assertIsNotNone(cpp)
        self.assertIsNotNone(js)
        self.assertEqual(js.group(1), cpp.group(1))

    # the load before the start

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_the_start_is_held_until_the_first_sync_is_answered(self):
        self.run_node(r"""
            const w = load();
            assert.strictEqual(w.saves().state, "memory");
            w.boot();
            assert.deepStrictEqual(w.mkdirs, ["/bagi/saves"]);
            assert.strictEqual(w.mounts.length, 1);
            assert.strictEqual(w.mounts[0].type, w.IDBFS);
            assert.strictEqual(w.mounts[0].path, "/bagi/saves");
            assert.strictEqual(w.syncs.length, 1);
            assert.strictEqual(w.syncs[0].populate, true);
            assert.strictEqual(w.deps.size, 1, "the start waits");
            w.advance(TIMEOUT - 1);
            assert.strictEqual(w.deps.size, 1, "not before the time-out");
            assert.strictEqual(w.saves().state, "memory");
            w.syncs[0].callback(null);
            assert.strictEqual(w.deps.size, 0, "the start is released");
            assert.strictEqual(w.removed, 1);
            assert.strictEqual(w.saves().state, "persistent");
            w.advance(TIMEOUT * 3);
            assert.strictEqual(w.removed, 1, "the time-out is gone");
            assert.strictEqual(w.saves().state, "persistent");
            assert.deepStrictEqual(w.warnings, []);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_b_a_failed_first_sync_releases_the_start_with_state_memory(self):
        self.run_node(r"""
            const w = load();
            w.boot();
            w.syncs[0].callback(new Error("boom"));
            assert.strictEqual(w.deps.size, 0);
            assert.strictEqual(w.saves().state, "memory");
            assert.strictEqual(w.warnings.length, 1);
            assert.ok(w.warnings[0].includes("boom"));
            w.saves().markDirty();
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.writes().length, 0, "a memory folder is never written");
            assert.strictEqual(w.documentListeners.visibilitychange, undefined);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_b_a_missing_indexeddb_never_holds_the_start(self):
        self.run_node(r"""
            const w = load({noIndexedDB: true});
            w.boot();
            assert.strictEqual(w.deps.size, 0);
            assert.deepStrictEqual(w.mkdirs, ["/bagi/saves"], "the plain folder exists");
            assert.strictEqual(w.mounts.length, 0);
            assert.strictEqual(w.syncs.length, 0);
            assert.strictEqual(w.saves().state, "memory");
            assert.strictEqual(w.warnings.length, 1);
            w.saves().markDirty();
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.syncs.length, 0);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_b_a_time_out_releases_the_start_and_a_late_answer_changes_nothing(self):
        self.run_node(r"""
            const w = load();
            w.boot();
            w.advance(TIMEOUT);
            assert.strictEqual(w.deps.size, 0);
            assert.strictEqual(w.removed, 1);
            assert.strictEqual(w.saves().state, "memory");
            assert.strictEqual(w.warnings.length, 1);
            w.syncs[0].callback(null);   // late
            assert.strictEqual(w.saves().state, "memory");
            assert.strictEqual(w.removed, 1, "released once");
            w.syncs[0].callback(new Error("late failure"));
            assert.strictEqual(w.warnings.length, 1);
            w.saves().markDirty();
            w.saves().flush();
            w.advance(MAX_WAIT * 2);
            w.document.visibilityState = "hidden";
            w.fire(w.windowListeners, "pagehide");
            w.fire(w.documentListeners, "visibilitychange");
            assert.strictEqual(w.syncs.length, 1, "no sync after the first one");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_b_a_throwing_syncfs_a_missing_fs_and_a_missing_idbfs_still_start(self):
        self.run_node(r"""
            for (const options of [{syncThrows: true}, {noFS: true}, {noIDBFS: true}]) {
                const w = load(options);
                w.boot();
                assert.strictEqual(w.deps.size, 0, JSON.stringify(options));
                assert.strictEqual(w.saves().state, "memory", JSON.stringify(options));
                assert.ok(w.warnings.length >= 1, JSON.stringify(options));
            }
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_the_preRun_of_the_page_is_kept_in_both_shapes_and_ours_comes_last(self):
        self.run_node(r"""
            const order = [];
            for (const preRun of [() => order.push("page"), [() => order.push("page")], undefined]) {
                order.length = 0;
                const w = load({preRun});
                assert.ok(Array.isArray(w.Module.preRun));
                if (preRun) {
                    assert.strictEqual(w.Module.preRun.length, 2);
                    w.Module.preRun[0]();
                    assert.deepStrictEqual(order, ["page"]);
                } else {
                    assert.strictEqual(w.Module.preRun.length, 1);
                }
            }
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_a_thread_worker_and_a_page_without_window_do_nothing(self):
        self.run_node(r"""
            for (const options of [{pthread: true}, {noWindow: true}]) {
                const w = load(options);
                assert.strictEqual(w.saves(), undefined, JSON.stringify(options));
                assert.strictEqual(w.Module.preRun, undefined, JSON.stringify(options));
            }
        """)

    # the flush

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_c_a_burst_of_markDirty_is_one_sync(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            for (let i = 0; i < 5; ++i) { w.saves().markDirty(); w.advance(DELAY / 2); }
            assert.strictEqual(w.writes().length, 0, "the delay moves with each call");
            w.advance(DELAY);
            assert.strictEqual(w.writes().length, 1);
            w.writes()[0].callback(null);
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.writes().length, 1, "nothing is left to write");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_c_a_burst_that_never_pauses_is_written_by_the_upper_bound(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            const step = DELAY / 2;
            for (let t = 0; t < MAX_WAIT - step; t += step) { w.saves().markDirty(); w.advance(step); }
            assert.strictEqual(w.writes().length, 0);
            w.saves().markDirty();
            w.advance(step);
            assert.strictEqual(w.writes().length, 1);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_d_a_markDirty_during_a_sync_gives_exactly_one_more_after_it(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.saves().markDirty();
            w.advance(DELAY);
            assert.strictEqual(w.writes().length, 1);
            w.saves().markDirty();
            w.saves().markDirty();
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.writes().length, 1, "never two syncs at once");
            w.writes()[0].callback(null);
            assert.strictEqual(w.writes().length, 1, "after the delay, not at once");
            w.advance(DELAY);
            assert.strictEqual(w.writes().length, 2);
            w.writes()[1].callback(null);
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.writes().length, 2, "exactly one more");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_e_a_failed_sync_is_warned_once_and_retried_by_the_next_markDirty(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.saves().markDirty();
            w.advance(DELAY);
            w.writes()[0].callback(new Error("quota"));
            assert.strictEqual(w.warnings.length, 1);
            assert.ok(w.warnings[0].includes("quota"));
            w.advance(MAX_WAIT * 3);
            assert.strictEqual(w.writes().length, 1, "no retry by itself");
            w.saves().markDirty();
            w.advance(DELAY);
            assert.strictEqual(w.writes().length, 2, "the next markDirty retries");
            w.writes()[1].callback(new Error("quota"));
            assert.strictEqual(w.warnings.length, 1, "reported once");
            w.saves().markDirty();
            w.advance(DELAY);
            w.writes()[2].callback(null);
            w.saves().markDirty();
            w.advance(DELAY);
            w.writes()[3].callback(new Error("again"));
            assert.strictEqual(w.warnings.length, 2, "a failure after a success is reported again");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_e_the_tab_closing_after_a_failed_sync_tries_once_more(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.saves().markDirty();
            w.advance(DELAY);
            w.writes()[0].callback(new Error("quota"));
            w.fire(w.windowListeners, "pagehide");
            assert.strictEqual(w.writes().length, 2);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_e_a_syncfs_that_throws_is_a_failed_sync(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.FS.syncfs = () => { throw new Error("closed database"); };
            w.saves().markDirty();
            w.advance(DELAY);
            assert.strictEqual(w.warnings.length, 1);
            w.FS.syncfs = (populate, callback) => w.syncs.push({populate, callback});
            w.saves().markDirty();
            w.advance(DELAY);
            assert.strictEqual(w.writes().length, 1, "the state was left usable");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_f_pagehide_and_a_hidden_tab_flush_at_once_and_a_visible_one_does_not(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.saves().markDirty();
            w.document.visibilityState = "visible";
            w.fire(w.documentListeners, "visibilitychange");
            assert.strictEqual(w.writes().length, 0, "visible does not flush");
            w.document.visibilityState = "hidden";
            w.fire(w.documentListeners, "visibilitychange");
            assert.strictEqual(w.writes().length, 1);
            w.writes()[0].callback(null);
            w.advance(MAX_WAIT * 2);
            assert.strictEqual(w.writes().length, 1, "the timers of the flushed write are gone");
            w.fire(w.windowListeners, "pagehide");
            assert.strictEqual(w.writes().length, 1, "nothing changed: nothing to write");
            w.saves().markDirty();
            w.fire(w.windowListeners, "pagehide");
            assert.strictEqual(w.writes().length, 2);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_f_pagehide_during_a_sync_writes_the_rest_right_after_it(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            w.saves().markDirty();
            w.advance(DELAY);
            w.saves().markDirty();
            w.fire(w.windowListeners, "pagehide");
            assert.strictEqual(w.writes().length, 1, "one at a time");
            w.writes()[0].callback(null);
            assert.strictEqual(w.writes().length, 2, "at once, not after the delay");
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_flush_writes_now_and_calls_back_with_the_result(self):
        self.run_node(r"""
            const w = load();
            w.bootPersistent();
            const results = [];
            w.saves().flush((error) => results.push(error));
            assert.strictEqual(w.writes().length, 1);
            w.writes()[0].callback(null);
            assert.deepStrictEqual(results, [null]);
            w.saves().flush((error) => results.push(error));
            w.writes()[1].callback(new Error("quota"));
            assert.strictEqual(results.length, 2);
            assert.ok(String(results[1]).includes("quota"));
            // a flush during a sync waits for the next one, which starts after the first
            w.saves().flush((error) => results.push("a"));
            w.saves().flush((error) => results.push("b"));
            assert.strictEqual(w.writes().length, 3);
            w.writes()[2].callback(null);
            assert.deepStrictEqual(results.slice(2), ["a"]);
            assert.strictEqual(w.writes().length, 4);
            w.writes()[3].callback(null);
            assert.deepStrictEqual(results.slice(2), ["a", "b"]);
        """)

    @unittest.skipUnless(NODE, "node is not on PATH")
    def test_flush_of_a_memory_folder_reports_it_and_writes_nothing(self):
        self.run_node(r"""
            const w = load({noIndexedDB: true});
            w.boot();
            const results = [];
            w.saves().flush((error) => results.push(error));
            w.advance(1);
            assert.strictEqual(results.length, 1);
            assert.ok(results[0]);
            assert.strictEqual(w.syncs.length, 0);
        """)


if __name__ == "__main__":
    unittest.main()
