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
from artgen_generators import FakeGenerator, GeneratorState, tiny_png  # noqa: E402
from test_artgen import AlwaysCut, CutOutGenerator  # noqa: E402

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

    def request(self, preset, subject, count=2, seed=5, **extra):
        self.run = {"id": (self.run or {"id": 0})["id"] + 1, "state": "Queued", "preset": preset, "subject": subject, "count": count,
                    "seed": seed, "variants": [], "steps": [], "error": "", **extra}
        self.cancel_requested = False

    def call(self, method, params=None):
        self.calls.append((method, params))
        if method == "artgen_poll":
            self.polls.append(params)
            answer = {"gameRoot": self.game_root}
            if self.run and self.run["state"] == "Queued":
                answer["request"] = {k: self.run[k] for k in ("id", "preset", "presetName", "subject", "count", "seed", "outDir", "name", "reference", "minPass", "extra") if k in self.run}
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
    assert presets["wood"]["title"] == "Wood" and presets["wood"]["source"] == "game"
    assert presets["wood"]["preset"]["prompt"] == "a {subject}"
    assert "no workflow 'nope'" in presets["broken"]["error"] and presets["broken"]["preset"]["workflow"] == "nope"
    assert presets["sprite"]["source"] == "engine", "the engine's starters come after the game's"
    workflows = {w["name"]: w for w in editor.polls[1]["workflows"]}
    assert workflows["sdxl_basic"]["inputs"]["steps"] == {"default": 25}
    assert workflows["sdxl_basic"]["inputs"]["sampler"]["choices"] == ["euler", "dpmpp_2m"], "the fake generator's choices"


def test_a_draft_runs_with_its_content_in_the_provenance(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    draft = {"preset": 1, "workflow": "sdxl_basic", "prompt": "a draft {subject}", "params": {"steps": 9}}
    editor.request(draft, "knight", count=1, seed=11)
    editor.run["presetName"] = "wood"
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    provenance = json.loads(Path(editor.run["variants"][0]["provenance"]).read_text(encoding="utf-8"))
    assert provenance["preset"] == "draft" and provenance["presetSource"] == "draft"
    assert provenance["presetData"] == draft and provenance["prompt"] == "a draft knight"


def test_a_broken_draft_fails_the_run_with_the_reason(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request({"preset": 1, "workflow": "sdxl_basic", "prompt": "x", "params": {"stepz": 2}}, "knight", count=1)
    editor.run["presetName"] = "wood"
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert editor.run["error"].startswith("the draft of 'wood':") and "stepz" in editor.run["error"]


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


def test_a_request_with_a_folder_and_a_name_writes_there(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "white knight", count=2, seed=7, outDir="set_1/white-knight", name="white-knight")
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    folder = game / "artgen" / "out" / "set_1" / "white-knight"
    assert [Path(v["path"]) for v in editor.run["variants"]] == [folder / "white-knight_7.png", folder / "white-knight_8.png"]
    for variant in editor.run["variants"]:
        assert Path(variant["path"]).is_file() and Path(variant["provenance"]) == folder / (Path(variant["path"]).stem + ".artgen.json")
        assert Path(variant["provenance"]).is_file()
    assert [p.name for p in (game / "artgen" / "out").iterdir()] == ["set_1"], "nothing beside the set's folder"


def test_a_name_alone_keeps_the_default_folder(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1, seed=3, name="piece")
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")
    assert Path(editor.run["variants"][0]["path"]) == game / "artgen" / "out" / "piece_3.png"


BAD_OUT_DIRS = ["..", "a/../b", "../out2", "/abs", "C:/x", "C:\\x", "a\\b", "a//b", "a/", "", "a b", "a.b", "x" * 65, 5]
BAD_NAMES = ["", "a b", "../x", "a/b", "a\\b", "x" * 65, "knight.png", 7]


@pytest.mark.parametrize("bad", BAD_OUT_DIRS)
def test_a_bad_out_dir_fails_the_run_and_writes_nothing(game, bad):
    editor = FakeEditor(game)
    generator = FakeGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1, outDir=bad)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "outDir" in editor.run["error"]
    assert generator.graphs == [] and not (game / "artgen" / "out").exists()
    assert not (game.parent / "out2").exists()


@pytest.mark.parametrize("bad", BAD_NAMES)
def test_a_bad_name_fails_the_run_and_writes_nothing(game, bad):
    editor = FakeEditor(game)
    generator = FakeGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1, outDir="set/job", name=bad)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "name" in editor.run["error"]
    assert generator.graphs == [] and not (game / "artgen" / "out").exists()


def test_a_bad_request_does_not_stop_the_next_one(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=1, outDir="..")
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    editor.request("wood", "knight", count=1, outDir="ok/job", name="job")
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")


def test_a_request_with_a_reference_reads_the_picture_from_the_game_root(game):
    picture = tiny_png(4, 4, (9, 9, 200))
    (game / "assets").mkdir()
    (game / "assets" / "rook.png").write_bytes(picture)
    editor = FakeEditor(game)
    generator = FakeGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "rook", count=1, seed=4, reference={"mode": "init", "image": "assets/rook.png", "weight": 0.3})
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    assert [data for _, data in generator.uploads] == [picture]
    assert generator.graphs[0]["3"]["inputs"]["denoise"] == pytest.approx(0.7)
    provenance = json.loads(Path(editor.run["variants"][0]["provenance"]).read_text(encoding="utf-8"))
    assert provenance["reference"]["image"] == "assets/rook.png" and provenance["reference"]["workflow"] == "sdxl_img2img"


@pytest.mark.parametrize("reference, message", [
    ({"mode": "init", "image": "../outside.png"}, "'../outside.png'"),
    ({"mode": "init", "image": "assets/missing.png"}, "not a file"),
    ({"mode": "warp", "image": "assets/rook.png"}, "reference.mode"),
    ("assets/rook.png", "must be an object"),
])
def test_a_bad_reference_fails_the_run_with_the_reason_and_writes_nothing(game, reference, message):
    editor = FakeEditor(game)
    generator = FakeGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "rook", count=1, reference=reference)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert message in editor.run["error"]
    assert generator.graphs == [] and generator.uploads == [] and not (game / "artgen" / "out").exists()
    editor.request("wood", "rook", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")


def test_a_style_run_on_a_generator_without_the_style_kind_fails_with_the_install_command(game):
    (game / "assets").mkdir()
    (game / "assets" / "rook.png").write_bytes(tiny_png(4, 4, (9, 9, 200)))
    editor = FakeEditor(game)
    generator = FakeGenerator()
    generator.kinds = ["audio", "image"]
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "rook", count=1, reference={"mode": "style", "image": "assets/rook.png"})
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "install --style" in editor.run["error"]
    assert generator.graphs == [] and generator.uploads == [] and not (game / "artgen" / "out").exists()


def test_a_structure_run_on_a_generator_without_the_structure_kind_fails_with_the_install_command(game):
    (game / "assets").mkdir()
    (game / "assets" / "rook.png").write_bytes(tiny_png(4, 4, (9, 9, 200)))
    editor = FakeEditor(game)
    generator = FakeGenerator()
    generator.kinds = ["audio", "image", "style"]
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "rook", count=1, reference={"mode": "structure", "image": "assets/rook.png"})
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert "install --structure" in editor.run["error"]
    assert generator.graphs == [] and generator.uploads == [] and not (game / "artgen" / "out").exists()


def test_a_structure_reference_of_a_set_job_reaches_the_generator_unchanged(game):
    (game / "assets").mkdir()
    (game / "assets" / "rook.png").write_bytes(tiny_png(4, 4, (9, 9, 200)))
    editor = FakeEditor(game)
    generator = FakeGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    reference = {"mode": "structure", "image": "assets/rook.png", "weight": 1.2}
    editor.request("wood", "white rook", count=1, seed=3, outDir="set_1/white-rook", name="white-rook", reference=reference)
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    assert generator.graphs[0]["42"]["inputs"]["strength"] == 1.2
    provenance = json.loads(Path(editor.run["variants"][0]["provenance"]).read_text(encoding="utf-8"))
    assert provenance["reference"]["mode"] == "structure" and provenance["reference"]["weight"] == 1.2 and provenance["workflow"] == "sdxl_controlnet"


def test_a_request_with_min_pass_and_extra_regenerates_and_the_editor_sees_the_extra_images(game):
    editor = FakeEditor(game)
    generator = CutOutGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=2, seed=5, minPass=2, extra=3)
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    assert [v["seed"] for v in editor.run["variants"]] == [5, 6, 7, 8], "5 and 7 are cut, 6 and 8 pass"
    assert [v["count"] for v in editor.run["variants"]] == [2, 2, 3, 4], "the label never reads past its total"
    assert all(Path(v["path"]).is_file() for v in editor.run["variants"])
    assert (3, 1, 3) in editor.run["steps"]


def test_a_request_without_the_keys_makes_what_it_asked_for_and_the_preset_may_say_the_rest(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, AlwaysCut(), log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=2, seed=5)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")
    assert [v["seed"] for v in editor.run["variants"]] == [5, 6]
    draft = {"preset": 1, "workflow": "sdxl_basic", "prompt": "a {subject}", "regenerate": {"extra": 2}}
    editor.request(draft, "rook", count=1, seed=9)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")
    assert [v["seed"] for v in editor.run["variants"]] == [9, 10, 11], "the draft's own regenerate"
    editor.request(draft, "bishop", count=1, seed=9, extra=0)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")
    assert [v["seed"] for v in editor.run["variants"]] == [9], "the request's extra 0 wins"


@pytest.mark.parametrize("keys, message", [
    ({"extra": True}, "extra True is not an integer"),
    ({"extra": "3"}, "extra '3' is not an integer"),
    ({"extra": 2.5}, "extra 2.5 is not an integer"),
    ({"minPass": True}, "minPass True is not an integer"),
    ({"minPass": 1.0}, "minPass 1.0 is not an integer"),
    ({"extra": 17}, "extra must be 0 to 16"),
    ({"minPass": 3}, "min_pass must be 1 to the count"),
])
def test_a_bad_min_pass_or_extra_fails_the_run_and_the_next_request_still_runs(game, keys, message):
    editor = FakeEditor(game)
    generator = CutOutGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=2, **keys)
    run_until(bridge, editor, lambda: editor.run["state"] == "Failed")
    assert message in editor.run["error"]
    assert generator.graphs == [] and not (game / "artgen" / "out").exists()
    editor.request("wood", "knight", count=1)
    run_until(bridge, editor, lambda: editor.run["state"] == "Done")


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
    assert "no preset 'nope'" in editor.run["error"]
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


def test_the_editor_can_tell_the_bridge_to_exit(game, monkeypatch):
    class ShuttingEditor(FakeEditor):
        def call(self, method, params=None):
            answer = super().call(method, params)
            if method == "artgen_poll" and len(self.polls) >= 2:
                answer["shutdown"] = True
            return answer

        def connect(self):
            pass

        def close(self):
            self.closed = True

    editor = ShuttingEditor(game)
    monkeypatch.setattr(artgen_bridge, "BagiControlClient", lambda host, port: editor)
    monkeypatch.setattr(artgen_bridge.time, "sleep", lambda s: None)
    lines = []
    artgen_bridge.serve("127.0.0.1", 1, FakeGenerator(), log=lines.append)  # returns: no reconnect after a shutdown
    assert len(editor.polls) == 2 and editor.closed
    assert any("asked the service to stop" in line for line in lines)


def test_a_refresh_from_the_editor_reads_the_presets_again_at_once(game):
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    bridge.tick()
    (game / "artgen" / "presets" / "saved.json").write_text(json.dumps({"preset": 1, "workflow": "sdxl_basic", "prompt": "x"}), encoding="utf-8")
    bridge.tick()
    assert "saved" not in {p["name"] for p in editor.polls[-1]["presets"]}, "the list is read every few seconds"
    original = editor.call

    def refreshing(method, params=None):
        answer = original(method, params)
        return {**answer, "refresh": True} if method == "artgen_poll" else answer

    editor.call = refreshing
    bridge.tick()
    editor.call = original
    bridge.tick()
    assert "saved" in {p["name"] for p in editor.polls[-1]["presets"]}


def test_a_sound_reaches_the_editor_as_audio(game):
    (game / "artgen" / "presets" / "knock.json").write_text(json.dumps(
        {"preset": 1, "workflow": "sao_sfx", "prompt": "a {subject}", "params": {"duration": 1.0}}), encoding="utf-8")
    editor = FakeEditor(game)
    bridge = artgen_bridge.Bridge(editor, FakeGenerator(), log=lambda *a: None)
    bridge.tick()
    bridge.tick()
    presets = {p["name"]: p for p in editor.polls[-1]["presets"]}
    assert presets["knock"]["kind"] == "audio" and presets["wood"]["kind"] == "image"
    assert {w["name"]: w["kind"] for w in editor.polls[-1]["workflows"]}["sao_sfx"] == "audio"
    editor.request("knock", "pawn", count=1, seed=2)
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    variant = editor.run["variants"][0]
    assert variant["kind"] == "audio" and variant["path"].endswith("pawn_2.flac")


def test_a_style_request_with_min_pass_and_extra_writes_the_verdicts_the_editor_reads(game):
    (game / "assets").mkdir()
    (game / "assets" / "rook.png").write_bytes(tiny_png(4, 4, (9, 9, 200)))
    editor = FakeEditor(game)
    generator = CutOutGenerator()
    bridge = artgen_bridge.Bridge(editor, generator, log=lambda *a: None)
    bridge.tick()
    editor.request("wood", "knight", count=2, seed=5, minPass=2, extra=3, reference={"mode": "style", "image": "assets/rook.png"})
    run_until(bridge, editor, lambda: editor.run["state"] not in ("Queued", "Running"))
    assert editor.run["state"] == "Done", editor.run["error"]
    assert [v["seed"] for v in editor.run["variants"]] == [5, 6, 7, 8], "5 and 7 are cut, 6 and 8 pass"
    provenances = [json.loads(Path(v["provenance"]).read_text(encoding="utf-8")) for v in editor.run["variants"]]
    assert [p["checks"]["passed"] for p in provenances] == [False, True, False, True], "the editor reads the verdict from these files"
    assert {p["reference"]["mode"] for p in provenances} == {"style"}
    assert len(generator.uploads) == 1
