"""artgen_bridge.Bridge against a fake editor that keeps the editor's side of the contract (src/Widgets/ArtGen/ArtGenSession), and the
fake generator: the request is handed over once, progress, images and the end come back, a cancel reaches the generator."""

import json
import sys
import threading
import time
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen_bridge  # noqa: E402
from artgen_generators import FakeGenerator, GeneratorState  # noqa: E402

WAIT_LIMIT_S = 20.0


class FakeEditor:
    """The editor's methods as ArtGenSession answers them, in Python: one run, handed over by the first poll after the request."""

    def __init__(self, game_root):
        self.game_root = str(game_root)
        self.events = []
        self.polls = []
        self.calls = []
        self.run = None
        self.cancel_requested = False

    def request(self, preset, subject, count=2, seed=5):
        self.run = {"id": (self.run or {"id": 0})["id"] + 1, "state": "Queued", "preset": preset, "subject": subject, "count": count,
                    "seed": seed, "variants": [], "steps": [], "error": ""}
        self.cancel_requested = False

    def call(self, method, params=None):
        self.calls.append((method, params))
        if method == "artgen_poll":
            self.polls.append(params)
            answer = {"gameRoot": self.game_root}
            if self.run and self.run["state"] == "Queued":
                answer["request"] = {k: self.run[k] for k in ("id", "preset", "subject", "count", "seed")}
                self.run["state"] = "Running"
            elif self.run and self.run["state"] == "Running" and self.cancel_requested:
                answer["cancel"] = self.run["id"]
            return answer
        stale = not self.run or params["id"] != self.run["id"]
        if stale:
            return {"stale": True}
        if method == "artgen_progress":
            self.run["steps"].append((params["image"], params["step"], params["steps"]))
        elif method == "artgen_image":
            self.run["variants"].append(params)
        elif method == "artgen_done":
            self.run["state"] = {"ok": "Done", "failed": "Failed", "cancelled": "Cancelled"}[params["status"]]
            self.run["error"] = params.get("error", "")
        return {"stale": False}


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "artgen" / "presets").mkdir(parents=True)
    preset = {"preset": 1, "title": "Wood", "workflow": "sdxl_basic", "prompt": "a {subject}", "size": [64, 64]}
    (root / "artgen" / "presets" / "wood.json").write_text(json.dumps(preset), encoding="utf-8")
    (root / "artgen" / "presets" / "broken.json").write_text(json.dumps({**preset, "workflow": "nope"}), encoding="utf-8")
    return root


def run_until(bridge, editor, done):
    deadline = time.monotonic() + WAIT_LIMIT_S
    while not done() and time.monotonic() < deadline:
        bridge.tick()
        time.sleep(0.01)
    assert done(), f"not done in {WAIT_LIMIT_S} s: {editor.run}"


def test_the_first_poll_learns_the_game_and_the_next_reports_its_presets(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    assert editor.polls[0]["state"] == GeneratorState.READY and editor.polls[0]["presets"] == []
    bridge.tick()
    presets = {p["name"]: p for p in editor.polls[1]["presets"]}
    assert presets["wood"] == {"name": "wood", "title": "Wood"}
    assert "no workflow 'nope'" in presets["broken"]["error"]


def test_a_request_runs_and_reports_back(game):
    editor = FakeEditor(game)
    generator = FakeGenerator(steps=2)
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=2, seed=5)
    run_until(bridge, editor, lambda: editor.run["state"] != "Running" and editor.run["state"] != "Queued")
    assert editor.run["state"] == "Done"
    assert [v["seed"] for v in editor.run["variants"]] == [5, 6]
    for variant in editor.run["variants"]:
        assert Path(variant["path"]).is_file() and Path(variant["provenance"]).is_file()
        assert Path(variant["path"]).parent == game / "artgen" / "out"
    assert (0, 2, 2) in editor.run["steps"] and (1, 1, 2) in editor.run["steps"]
    assert not bridge.busy
    assert len(generator.graphs) == 2


def test_a_cancel_from_the_editor_stops_the_generator(game):
    editor = FakeEditor(game)
    started = threading.Event()
    release = threading.Event()

    class SlowGenerator(FakeGenerator):
        def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
            started.set()
            release.wait(WAIT_LIMIT_S)
            return super().run(graph, on_progress, cancel, timeout_s)

    bridge = artgen_bridge.Bridge(editor, SlowGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=3)
    bridge.tick()
    assert started.wait(WAIT_LIMIT_S)
    editor.cancel_requested = True
    bridge.tick()
    release.set()
    run_until(bridge, editor, lambda: editor.run["state"] == "Cancelled")
    assert editor.run["variants"] == []


def test_a_failure_reaches_the_editor_with_its_reason(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(fail="out of memory"), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "out of memory" in editor.run["error"]


def test_an_unknown_preset_fails_the_run_not_the_bridge(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("nope", "knight", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "nope.json" in editor.run["error"]
    editor.request("wood", "knight", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")


def test_a_generator_that_is_not_ready_is_reported_and_its_run_fails(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(state=GeneratorState.NOT_RUNNING), log=lambda *a: None)
    bridge.tick()
    assert editor.polls[0]["state"] == GeneratorState.NOT_RUNNING and editor.polls[0]["reason"]
    editor.request("wood", "knight", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "not_running" in editor.run["error"]


def test_stop_cancels_a_run_in_work(game):
    editor = FakeEditor(game)
    seen_cancel = threading.Event()

    class WaitingGenerator(FakeGenerator):
        def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
            assert cancel.wait(WAIT_LIMIT_S)
            seen_cancel.set()
            return super().run(graph, on_progress, cancel, timeout_s)

    bridge = artgen_bridge.Bridge(editor, WaitingGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1)
    bridge.tick()
    bridge.stop()
    assert seen_cancel.is_set() and not bridge.busy


class FakeSetupEditor(FakeEditor):
    """The editor with the setup side of the contract: one setup action, handed over by a poll, reported by artgen_setup_report."""

    def __init__(self, game_root):
        super().__init__(game_root)
        self.setup = None
        self.setup_cancel = False

    def ask_setup(self, action):
        self.setup = {"id": (self.setup or {"id": 100})["id"] + 1, "action": action, "state": "Queued", "events": []}
        self.setup_cancel = False

    def call(self, method, params=None):
        if method == "artgen_setup_report":
            self.calls.append((method, params))
            if not self.setup or params["id"] != self.setup["id"]:
                return {"stale": True}
            self.setup["events"].append(params)
            if params["event"] == "done":
                self.setup["state"] = "Done" if params["ok"] else "Failed"
            return {"stale": False}
        answer = super().call(method, params)
        if method == "artgen_poll":
            if self.setup and self.setup["state"] == "Queued":
                answer["setup"] = {"id": self.setup["id"], "action": self.setup["action"]}
                self.setup["state"] = "Running"
            elif self.setup and self.setup["state"] == "Running" and self.setup_cancel:
                answer["setupCancel"] = self.setup["id"]
        return answer


class FakeSetup:
    def __init__(self):
        self.actions = []
        self.release = threading.Event()
        self.ours = False

    def started_here(self):
        return self.ours

    def run(self, action, emit, cancel=None):
        self.actions.append(action)
        emit({"event": "plan", "platform": "windows-nvidia", "root": "D:/ComfyUI", "steps": [], "downloadBytes": 9})
        if action == "Install":
            emit({"event": "step", "index": 0, "what": "checkpoints/x"})
            emit({"event": "progress", "index": 0, "done": 3, "total": 9})
            while not self.release.is_set():
                if cancel.is_set():
                    raise artgen_bridge.artgen_setup.SetupError(artgen_bridge.artgen_setup.CANCELLED)
                time.sleep(0.01)


def test_a_setup_action_is_run_and_reported_and_the_status_is_read_again(game):
    editor = FakeSetupEditor(game)
    fake = FakeSetup()
    fake.release.set()
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), setup=fake)
    bridge.tick()
    status_reads = len(editor.polls)
    editor.ask_setup("Plan")
    run_until(bridge, editor, lambda: editor.setup["state"] != "Running" and editor.setup["state"] != "Queued")
    assert fake.actions == ["Plan"]
    assert editor.setup["state"] == "Done"
    assert [e["event"] for e in editor.setup["events"]] == ["plan", "done"]
    assert editor.setup["events"][0]["platform"] == "windows-nvidia"
    assert not bridge.busy and bridge._status_at is None and len(editor.polls) > status_reads


def test_a_cancel_stops_an_install_and_its_end_says_why(game):
    editor = FakeSetupEditor(game)
    fake = FakeSetup()
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), setup=fake)
    editor.ask_setup("Install")
    run_until(bridge, editor, lambda: any(e["event"] == "progress" for e in (editor.setup or {}).get("events", [])))
    assert bridge.busy
    editor.setup_cancel = True
    run_until(bridge, editor, lambda: editor.setup["state"] == "Failed")
    assert editor.setup["events"][-1]["error"].startswith("cancelled")
    assert not bridge.busy


def test_a_bridge_without_a_local_generator_refuses_setup(game):
    editor = FakeSetupEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator())
    editor.ask_setup("Start")
    run_until(bridge, editor, lambda: editor.setup["state"] == "Failed")
    assert "no local generator" in editor.setup["events"][-1]["error"]


def test_a_bridge_started_by_the_editor_gives_up_without_one(monkeypatch):
    class NoEditor:
        def __init__(self, host, port):
            pass

        def connect(self):
            raise ConnectionRefusedError("no editor")

    now = [0.0]

    def fake_sleep(seconds):
        now[0] += seconds

    monkeypatch.setattr(artgen_bridge, "BagiControlClient", NoEditor)
    monkeypatch.setattr(artgen_bridge.time, "sleep", fake_sleep)
    artgen_bridge.serve("127.0.0.1", 1, FakeGenerator(), log=lambda *_: None, exit_with_editor=True, clock=lambda: now[0])
    assert now[0] >= artgen_bridge.FIRST_CONNECT_S


def test_the_status_says_whether_the_generator_was_started_here(game):
    editor = FakeSetupEditor(game)
    fake = FakeSetup()
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), setup=fake)
    bridge.tick()
    assert editor.polls[-1]["startedHere"] is False
    fake.ours = True
    bridge._status_at = None
    bridge.tick()
    assert editor.polls[-1]["startedHere"] is True
