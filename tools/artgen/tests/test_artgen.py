"""The generation service: presets, the service over the fake generator, the ComfyUI adapter against a fake ComfyUI with a
real WebSocket progress stream, the generator states and the command line."""

import base64
import hashlib
import json
import queue
import shutil
import socket
import struct
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen  # noqa: E402
import artgen_generators  # noqa: E402
import artgen_preset  # noqa: E402
import artgen_service  # noqa: E402
import comfyui_check  # noqa: E402
from artgen_generators import (  # noqa: E402
    Cancelled, ComfyUIGenerator, FakeGenerator, GenerationError, GeneratorState, tiny_png,
)

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PRESET = {
    "preset": 1,
    "title": "Test",
    "workflow": "sdxl_basic",
    "prompt": "a {subject}, carved wood",
    "negative": "blurry",
    "size": [512, 768],
    "params": {"steps": 12, "cfg": 5.5},
}


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "artgen" / "presets").mkdir(parents=True)
    write_preset(root, "wood", PRESET)
    return root


def write_preset(root, name, data):
    path = root / "artgen" / "presets" / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def preset_of(game, name="wood"):
    return artgen_preset.load_preset(game / "artgen" / "presets" / f"{name}.json", game)


# --- presets


def test_preset_loads_with_the_engine_workflow(game):
    preset = preset_of(game)
    assert preset.workflow.name == "sdxl_basic" and preset.workflow.path.parent == artgen_preset.ENGINE_WORKFLOWS
    assert (preset.width, preset.height) == (512, 768)
    assert len(preset.file_hash) == 64


def test_graph_takes_the_values_and_leaves_the_template_alone(game):
    preset = preset_of(game)
    template = json.dumps(preset.workflow.graph, sort_keys=True)
    graph = artgen_preset.build_graph(preset, "knight", 7, file_prefix="artgen_x")
    assert graph["6"]["inputs"]["text"] == "a knight, carved wood"
    assert graph["7"]["inputs"]["text"] == "blurry"
    assert graph["3"]["inputs"]["seed"] == 7 and graph["3"]["inputs"]["steps"] == 12 and graph["3"]["inputs"]["cfg"] == 5.5
    assert (graph["5"]["inputs"]["width"], graph["5"]["inputs"]["height"]) == (512, 768)
    assert graph["9"]["inputs"]["filename_prefix"] == "artgen_x"
    assert json.dumps(preset.workflow.graph, sort_keys=True) == template
    assert artgen_preset.models_of(graph) == {"ckpt_name": ["sd_xl_base_1.0.safetensors"], "vae_name": ["sdxl.vae.safetensors"]}


def test_prompt_without_the_subject_token_starts_with_the_subject(game):
    write_preset(game, "plain", {**PRESET, "prompt": "pixel art"})
    assert artgen_preset.final_prompt(preset_of(game, "plain"), "a cat") == "a cat, pixel art"


@pytest.mark.parametrize("change, message", [
    ({"preset": 2}, "format version"),
    ({"workflow": "nope"}, "no workflow 'nope'"),
    ({"params": {"denoise": 1}}, "no input 'denoise'"),
    ({"params": {"seed": 1}}, "set by the service"),
    ({"size": [512]}, "[width, height]"),
    ({"prompt": ""}, "\"prompt\" is required"),
])
def test_broken_presets_are_refused_by_name(game, change, message):
    path = write_preset(game, "bad", {**PRESET, **change})
    with pytest.raises(artgen_preset.PresetError, match=message.replace("[", r"\[").replace("]", r"\]")) as error:
        artgen_preset.load_preset(path, game)
    assert "bad.json" in str(error.value)


def test_a_game_workflow_wins_and_its_inputs_are_checked(game):
    folder = game / "artgen" / "workflows"
    folder.mkdir()
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.json", folder / "sdxl_basic.json")
    inputs = json.loads((artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.inputs.json").read_text(encoding="utf-8"))
    (folder / "sdxl_basic.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    assert preset_of(game).workflow.path.parent == folder
    inputs["steps"] = ["3", "nothing"]
    (folder / "sdxl_basic.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    with pytest.raises(artgen_preset.PresetError, match="no input 'nothing'"):
        preset_of(game)


def test_find_presets_reports_a_broken_one(game):
    write_preset(game, "zbroken", {**PRESET, "workflow": "nope"})
    found = artgen_preset.find_presets(game)
    assert isinstance(found["wood"], artgen_preset.Preset)
    assert isinstance(found["zbroken"], artgen_preset.PresetError)
    assert set(artgen_preset.find_presets(game / "missing")) == {"icon", "sprite"}, "a game without presets gets the engine's starters"
    assert found["wood"].source == "game" and found["sprite"].source == "engine"
    assert found["zbroken"].source == "game" and found["zbroken"].data["workflow"] == "nope"


def test_every_engine_starter_preset_loads():
    for name, preset in artgen_preset.find_presets(None).items():
        assert isinstance(preset, artgen_preset.Preset), f"{name}: {preset}"
        assert preset.source == "engine" and artgen_preset.SUBJECT_TOKEN in preset.prompt


def test_a_game_preset_wins_over_the_engine_starter_of_its_name(game):
    write_preset(game, "sprite", {**PRESET, "title": "Ours"})
    found = artgen_preset.find_presets(game)
    assert found["sprite"].title == "Ours" and found["sprite"].source == "game"
    assert artgen_preset.find_preset("sprite", game).title == "Ours"
    assert artgen_preset.find_preset("icon", game).source == "engine"
    with pytest.raises(artgen_preset.PresetError, match="no preset 'nope'"):
        artgen_preset.find_preset("nope", game)
    with pytest.raises(artgen_preset.PresetError, match="no preset"):
        artgen_preset.find_preset("../wood", game)


def test_a_draft_is_checked_like_a_file_and_hashed_by_content(game):
    draft = artgen_preset.parse_preset(dict(PRESET), artgen_preset.DRAFT, game, label="the draft", source=artgen_preset.DRAFT)
    from_file = preset_of(game)
    assert draft.path is None and draft.source == "draft" and draft.data == PRESET
    assert draft.file_hash == artgen_preset.content_hash(PRESET)
    assert artgen_preset.build_graph(draft, "knight", 7) == artgen_preset.build_graph(from_file, "knight", 7)
    cases = (
        ({**PRESET, "params": {"stepz": 3}}, "has no input 'stepz'"),
        ({**PRESET, "preset": 2}, "format version"),
        ({**PRESET, "params": []}, "must be an object"),
        ({**PRESET, "size": [True, 4]}, "width, height"),
        ([], "JSON object"),
    )
    for broken, message in cases:
        with pytest.raises(artgen_preset.PresetError, match=message) as error:
            artgen_preset.parse_preset(broken, "draft", game, label="the draft")
        assert str(error.value).startswith("the draft:")


def test_a_workflow_describes_its_inputs_with_the_graph_defaults_and_the_choices(game):
    found = artgen_preset.find_workflows(game)
    assert {"sdxl_basic", "sdxl_cutout"} <= set(found)
    choices = {"KSampler": {"sampler_name": ["euler", "dpmpp_2m"]}, "CheckpointLoaderSimple": {"ckpt_name": ["a.safetensors"]}}
    described = found["sdxl_cutout"].describe(choices)
    assert described["name"] == "sdxl_cutout" and described["source"] == "engine"
    inputs = described["inputs"]
    assert "output" not in inputs
    assert inputs["steps"] == {"default": 25} and inputs["cfg"] == {"default": 7.0}
    assert inputs["width"] == {"default": 1024} and inputs["seed"] == {"default": 42}
    assert inputs["sampler"] == {"default": "dpmpp_2m", "choices": ["euler", "dpmpp_2m"]}
    assert inputs["checkpoint"]["choices"] == ["a.safetensors"]
    assert "choices" not in inputs["vae"]


def test_a_game_workflow_is_listed_as_the_games_and_a_broken_one_as_its_error(game):
    folder = game / "artgen" / "workflows"
    folder.mkdir()
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.json", folder / "sdxl_basic.json")
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.inputs.json", folder / "sdxl_basic.inputs.json")
    (folder / "lonely.json").write_text("{}", encoding="utf-8")
    found = artgen_preset.find_workflows(game)
    assert found["sdxl_basic"].source == "game" and found["sdxl_cutout"].source == "engine"
    assert isinstance(found["lonely"], artgen_preset.PresetError)


def test_combo_choices_reads_both_forms_of_object_info():
    info = {
        "KSampler": {"input": {"required": {
            "sampler_name": [["euler", "ddim"]],
            "steps": ["INT", {"default": 20}],
            "scheduler": ["COMBO", {"options": ["normal", "karras"]}],
        }}},
        "Broken": None,
    }
    assert artgen_generators.combo_choices(info) == {"KSampler": {"sampler_name": ["euler", "ddim"], "scheduler": ["normal", "karras"]}}


# --- the service over the fake generator


def test_generate_writes_images_and_provenance_with_consecutive_seeds(game, tmp_path):
    events = []
    generator = FakeGenerator(steps=2)
    request = artgen_service.Request(preset_of(game), "White Knight!", count=3, seed=100, out_dir=tmp_path / "out")
    results = artgen_service.generate(request, generator, events.append)
    assert [r.seed for r in results] == [100, 101, 102]
    assert [r.path.name for r in results] == ["white_knight_100.png", "white_knight_101.png", "white_knight_102.png"]
    assert all(r.path.read_bytes().startswith(b"\x89PNG") for r in results)
    provenance = json.loads(results[1].provenance_path.read_text(encoding="utf-8"))
    assert provenance["seed"] == 101 and provenance["preset"] == "wood" and provenance["subject"] == "White Knight!"
    assert provenance["prompt"] == "a White Knight!, carved wood"
    assert provenance["workflowHash"] == preset_of(game).workflow.hash
    assert provenance["generator"] == {"name": "fake", "version": "1"}
    assert provenance["models"]["ckpt_name"] == ["sd_xl_base_1.0.safetensors"]
    assert [g["3"]["inputs"]["seed"] for g in generator.graphs] == [100, 101, 102]
    kinds = [(e["kind"], e["image"]) for e in events]
    assert kinds == [("step", 0), ("step", 0), ("image", 0), ("step", 1), ("step", 1), ("image", 1), ("step", 2), ("step", 2), ("image", 2)]


def test_random_seed_when_none_is_given(game, tmp_path):
    class Rng:
        def randrange(self, n):
            return 5

    results = artgen_service.generate(artgen_service.Request(preset_of(game), "x", 2, None, tmp_path), FakeGenerator(), rng=Rng())
    assert [r.seed for r in results] == [5, 6]


@pytest.mark.parametrize("count", [0, artgen_service.MAX_COUNT + 1])
def test_count_out_of_range_is_refused(game, tmp_path, count):
    with pytest.raises(artgen_service.RequestError, match="count"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", count, 1, tmp_path), FakeGenerator())


def test_a_generator_that_is_not_ready_is_refused_before_running(game, tmp_path):
    generator = FakeGenerator(state=GeneratorState.NOT_RUNNING)
    with pytest.raises(artgen_service.RequestError, match="not_running"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path), generator)
    assert generator.graphs == []


def test_cancel_keeps_the_images_already_written(game, tmp_path):
    cancel = threading.Event()

    def progress(event):
        if event["kind"] == "image" and event["image"] == 0:
            cancel.set()

    with pytest.raises(Cancelled):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 3, 1, tmp_path / "o"), FakeGenerator(), progress, cancel)
    assert sorted(p.name for p in (tmp_path / "o").iterdir()) == ["x_1.artgen.json", "x_1.png"]


def test_a_failure_of_the_generator_passes_through(game, tmp_path):
    with pytest.raises(GenerationError, match="boom"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path), FakeGenerator(fail="boom"))


# --- the ComfyUI adapter against a fake ComfyUI


class FakeComfy:
    """/system_stats, /models, /prompt, /history, /view, /queue, /interrupt and a WebSocket /ws that streams the events of
    each prompt to the client that queued it. `mode`: "ok", "error" (execution_error), "hang" (one step, then nothing until
    a cancel), "refuse" (node_errors); `ws` False answers /ws with 404."""

    def __init__(self):
        self.mode = "ok"
        self.ws = True
        self.models = {m["folder"]: [m["name"]] for m in comfyui_check.load_manifest()["models"]}
        self.prompts = []
        self.cancels = []
        self.streams = {}
        self.history = {}

    def events_for(self, prompt_id):
        progress = [{"type": "progress", "data": {"value": v, "max": 3, "prompt_id": prompt_id}} for v in (1, 2, 3)]
        if self.mode == "error":
            return progress[:1] + [{"type": "execution_error", "data": {"prompt_id": prompt_id, "exception_message": "out of memory"}}]
        if self.mode == "hang":
            return progress[:1]
        return ([{"type": "status", "data": {}}, {"type": "progress", "data": {"value": 9, "max": 9, "prompt_id": "other"}}]
                + progress + [{"type": "executing", "data": {"node": None, "prompt_id": prompt_id}}])

    def queue_prompt(self, body):
        if self.mode == "refuse":
            return {"prompt_id": "p0", "node_errors": {"3": {"errors": ["bad"]}}}
        prompt_id = f"p{len(self.prompts) + 1}"
        self.prompts.append(body["prompt"])
        if self.mode in ("ok",):
            self.history[prompt_id] = {
                "status": {"status_str": "success", "completed": True},
                "outputs": {"9": {"images": [{"filename": f"{prompt_id}.png", "subfolder": "", "type": "output"},
                                             {"filename": "preview.png", "subfolder": "", "type": "temp"}]}},
            }
        stream = self.streams.get(body.get("client_id"))
        if stream is not None:
            for event in self.events_for(prompt_id):
                stream.put(json.dumps(event))
        return {"prompt_id": prompt_id, "number": 0, "node_errors": {}}


@pytest.fixture
def comfy(monkeypatch):
    state = FakeComfy()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _json(self, payload, code=200, raw=None):
            data = raw if raw is not None else json.dumps(payload).encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path
            if path.startswith("/ws"):
                return self._websocket()
            if path == "/system_stats":
                return self._json({"system": {"comfyui_version": comfyui_check.load_manifest()["comfyui"]["version"]},
                                   "devices": [{"name": "gpu", "type": "cuda", "vram_total": 1, "vram_free": 1}]})
            if path.startswith("/models/"):
                return self._json(state.models.get(path[len("/models/"):], []))
            if path.startswith("/history/"):
                prompt_id = path[len("/history/"):]
                return self._json({prompt_id: state.history[prompt_id]} if prompt_id in state.history else {})
            if path.startswith("/view?"):
                return self._json(None, raw=tiny_png(4, 4, (1, 2, 3)))
            return self._json({"error": path}, 404)

        def do_POST(self):
            length = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(length)) if length else {}
            if self.path == "/prompt":
                return self._json(state.queue_prompt(body))
            if self.path in ("/queue", "/interrupt"):
                state.cancels.append((self.path, body))
                return self._json(None, raw=b"")
            return self._json({"error": self.path}, 404)

        def _websocket(self):
            if not state.ws:
                return self._json({"error": "no ws"}, 404)
            client_id = self.path.split("clientId=", 1)[1]
            accept = base64.b64encode(hashlib.sha1((self.headers["Sec-WebSocket-Key"] + WS_GUID).encode()).digest()).decode()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            self.wfile.flush()
            stream = queue.Queue()
            state.streams[client_id] = stream
            self.wfile.write(b"\x82\x02\x00\x00")  # a binary preview frame, ignored by the client
            while True:
                text = stream.get()
                if text is None:
                    break
                data = text.encode()
                header = b"\x81" + (bytes([len(data)]) if len(data) < 126 else b"\x7e" + struct.pack("!H", len(data)))
                try:
                    self.wfile.write(header + data)
                    self.wfile.flush()
                except OSError:
                    break
            self.close_connection = True

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    monkeypatch.setattr(comfyui_check, "POLL_INTERVAL_S", 0.0)
    state.url = f"http://127.0.0.1:{server.server_address[1]}"
    yield state
    for stream in state.streams.values():
        stream.put(None)
    server.shutdown()
    server.server_close()


def test_comfyui_streams_progress_and_returns_only_output_images(comfy):
    steps = []
    images = ComfyUIGenerator(comfy.url).run({"1": {"inputs": {}}}, on_progress=lambda v, m: steps.append((v, m)), timeout_s=10)
    assert steps == [(1, 3), (2, 3), (3, 3)]
    assert [i.name for i in images] == ["p1.png"] and images[0].data.startswith(b"\x89PNG")


def test_comfyui_without_a_progress_stream_polls_the_history(comfy):
    comfy.ws = False
    images = ComfyUIGenerator(comfy.url).run({"1": {"inputs": {}}}, timeout_s=10)
    assert [i.name for i in images] == ["p1.png"]


def test_comfyui_errors_are_generation_errors(comfy):
    comfy.mode = "error"
    with pytest.raises(GenerationError, match="out of memory"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10)
    comfy.mode = "refuse"
    with pytest.raises(GenerationError, match="refused the graph"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10)


def test_comfyui_cancel_deletes_and_interrupts_its_own_prompt(comfy):
    comfy.mode = "hang"
    cancel = threading.Event()
    with pytest.raises(Cancelled):
        ComfyUIGenerator(comfy.url).run({}, on_progress=lambda v, m: cancel.set(), cancel=cancel, timeout_s=10)
    assert comfy.cancels == [("/queue", {"delete": ["p1"]}), ("/interrupt", {"prompt_id": "p1"})]


def test_comfyui_timeout_cancels_too(comfy):
    comfy.mode = "hang"
    calls = []

    def clock():
        calls.append(1)
        return 0 if len(calls) < 4 else 100

    with pytest.raises(GenerationError, match="no image"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10, clock=clock)
    assert [path for path, _ in comfy.cancels] == ["/queue", "/interrupt"]


def test_status_ready_and_missing_model(comfy):
    generator = ComfyUIGenerator(comfy.url)
    assert generator.status().state == GeneratorState.READY
    assert generator.info()["version"] == comfyui_check.load_manifest()["comfyui"]["version"]
    comfy.models["vae"] = []
    status = generator.status()
    assert status.state == GeneratorState.NOT_INSTALLED and "sdxl.vae.safetensors" in status.reason


def closed_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_status_when_nothing_answers(tmp_path, monkeypatch):
    monkeypatch.setattr(comfyui_check, "REQUEST_TIMEOUT_S", 0.5)
    url = f"http://127.0.0.1:{closed_port()}"
    assert ComfyUIGenerator(url, install_root=tmp_path).status().state == GeneratorState.NOT_INSTALLED
    (tmp_path / "ComfyUI").mkdir()
    (tmp_path / "ComfyUI" / "main.py").write_text("", encoding="utf-8")
    status = ComfyUIGenerator(url, install_root=tmp_path).status()
    assert status.state == GeneratorState.NOT_RUNNING and str(tmp_path) in status.reason
    # Another machine: whether it is installed there cannot be known from here.
    assert ComfyUIGenerator(f"http://192.0.2.1:{closed_port()}").status().state == GeneratorState.NOT_RUNNING


def test_service_over_the_comfyui_adapter(comfy, game, tmp_path):
    results = artgen_service.generate(artgen_service.Request(preset_of(game), "rook", 2, 40, tmp_path), ComfyUIGenerator(comfy.url))
    assert [r.seed for r in results] == [40, 41]
    assert [p["3"]["inputs"]["seed"] for p in comfy.prompts] == [40, 41]
    assert results[0].provenance["generator"]["name"] == "comfyui"
    assert results[0].provenance["generatorFile"] == "p1.png"


# --- the command line


def test_cli_generate_with_the_fake_generator(game, tmp_path, capsys):
    code = artgen.main(["generate", "--generator", "fake", "--game-root", str(game), "--preset", "wood", "--subject", "pawn",
                        "--count", "2", "--seed", "3", "--out", str(tmp_path / "o"), "--json"])
    assert code == artgen.EXIT_OK
    lines = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert lines[-1]["kind"] == "done" and [i["seed"] for i in lines[-1]["images"]] == [3, 4]
    assert (tmp_path / "o" / "pawn_4.png").is_file()


def test_cli_presets_and_errors(game, capsys):
    assert artgen.main(["presets", "--game-root", str(game), "--json"]) == artgen.EXIT_OK
    assert json.loads(capsys.readouterr().out)["wood"]["workflow"] == "sdxl_basic"
    assert artgen.main(["generate", "--generator", "fake", "--game-root", str(game), "--preset", "nope", "--subject", "x"]) == artgen.EXIT_FAILED
    assert "no preset 'nope'" in capsys.readouterr().err


def test_cli_status_exit_codes(tmp_path, capsys):
    assert artgen.main(["status", "--generator", "fake"]) == artgen.EXIT_OK
    assert artgen.main(["status", "--url", f"http://127.0.0.1:{closed_port()}", "--json"]) in (artgen.EXIT_NOT_INSTALLED, artgen.EXIT_FAILED)
