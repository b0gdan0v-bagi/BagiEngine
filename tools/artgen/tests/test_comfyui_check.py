"""comfyui_check.py against a fake ComfyUI (http.server on port 0), and the shape of the manifest and the test workflow."""

import json
import re
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import comfyui_check  # noqa: E402

MANIFEST = comfyui_check.load_manifest()


class FakeComfy:
    """The endpoints the check uses, with the state a test sets."""

    def __init__(self):
        self.version = MANIFEST["comfyui"]["version"]
        self.device_type = "cuda"
        self.models = {}
        for m in MANIFEST["models"]:
            self.models.setdefault(m["folder"], []).append(m["name"])
        self.history_polls_before_done = 1
        self.fail_generation = False
        self.prompts = []
        self._polls = 0

    def handle(self, method, path, body):
        if method == "GET" and path == "/system_stats":
            return 200, {"system": {"comfyui_version": self.version},
                         "devices": [{"name": "fake gpu", "type": self.device_type, "vram_total": 8 << 30, "vram_free": 7 << 30}]}
        if method == "GET" and path.startswith("/models/"):
            return 200, self.models.get(path[len("/models/"):], [])
        if method == "POST" and path == "/prompt":
            self.prompts.append(body["prompt"])
            return 200, {"prompt_id": "p1", "number": 0}
        if method == "GET" and path == "/history/p1":
            self._polls += 1
            if self._polls <= self.history_polls_before_done:
                return 200, {}
            if self.fail_generation:
                return 200, {"p1": {"status": {"status_str": "error", "completed": False, "messages": [["execution_error", {}]]}}}
            return 200, {"p1": {"status": {"status_str": "success", "completed": True},
                                "outputs": {"9": {"images": [{"filename": "artgen_check_00001_.png", "type": "output"}]}}}}
        return 404, {"error": path}


@pytest.fixture
def fake(monkeypatch):
    state = FakeComfy()

    class Handler(BaseHTTPRequestHandler):
        def _answer(self, method):
            length = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(length)) if length else None
            code, payload = state.handle(method, self.path, body)
            data = json.dumps(payload).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            self._answer("GET")

        def do_POST(self):
            self._answer("POST")

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    monkeypatch.setattr(comfyui_check, "POLL_INTERVAL_S", 0.0)
    state.url = f"http://127.0.0.1:{server.server_address[1]}"
    yield state
    server.shutdown()
    server.server_close()


def closed_port_url():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return f"http://127.0.0.1:{s.getsockname()[1]}"


def test_ready_when_every_model_is_there(fake):
    report = comfyui_check.check(fake.url, MANIFEST)
    assert report["reachable"] and report["ready"]
    assert report["problems"] == [] and report["warnings"] == []
    assert all(m["present"] for m in report["models"])


def test_missing_model_is_not_ready(fake):
    fake.models["vae"] = []
    report = comfyui_check.check(fake.url, MANIFEST)
    assert report["reachable"] and not report["ready"]
    assert any("sdxl.vae.safetensors" in p for p in report["problems"])


def test_other_version_and_cpu_only_warn(fake):
    fake.version = "0.1.0"
    fake.device_type = "cpu"
    report = comfyui_check.check(fake.url, MANIFEST)
    assert report["ready"]
    assert len(report["warnings"]) == 2


def test_unreachable_exit_code():
    assert comfyui_check.main(["--url", closed_port_url()]) == comfyui_check.EXIT_UNREACHABLE


def test_exit_codes_and_json(fake, capsys):
    assert comfyui_check.main(["--url", fake.url, "--json"]) == comfyui_check.EXIT_READY
    assert json.loads(capsys.readouterr().out)["ready"] is True
    fake.models["checkpoints"] = []
    assert comfyui_check.main(["--url", fake.url]) == comfyui_check.EXIT_NOT_READY


def test_generate_sends_the_workflow_and_returns_the_files(fake, capsys):
    assert comfyui_check.main(["--url", fake.url, "--generate", "--json"]) == comfyui_check.EXIT_READY
    report = json.loads(capsys.readouterr().out)
    assert report["generation"]["files"] == ["artgen_check_00001_.png"]
    sent, = fake.prompts
    original = json.loads(comfyui_check.WORKFLOW.read_text(encoding="utf-8"))
    assert sent["3"]["inputs"]["seed"] != original["3"]["inputs"]["seed"]
    sent["3"]["inputs"]["seed"] = original["3"]["inputs"]["seed"]
    assert sent == original


def test_failed_generation_is_not_ready(fake):
    fake.fail_generation = True
    assert comfyui_check.main(["--url", fake.url, "--generate"]) == comfyui_check.EXIT_NOT_READY


def test_generation_times_out(fake):
    fake.history_polls_before_done = 10**9
    ticks = iter(range(10**6))
    with pytest.raises(RuntimeError, match="no image"):
        comfyui_check.generate(fake.url, {}, timeout_s=3, clock=lambda: next(ticks), sleep=lambda s: None)


def test_url_order(tmp_path):
    settings = tmp_path / "local.json"
    assert comfyui_check.resolve_url(None, {}, settings) == comfyui_check.DEFAULT_URL
    settings.write_text(json.dumps({"comfyuiUrl": "http://from-file:1/"}), encoding="utf-8")
    assert comfyui_check.resolve_url(None, {}, settings) == "http://from-file:1"
    assert comfyui_check.resolve_url(None, {"COMFYUI_URL": "http://from-env:2"}, settings) == "http://from-env:2"
    assert comfyui_check.resolve_url("http://cli:3", {"COMFYUI_URL": "http://from-env:2"}, settings) == "http://cli:3"


SHA256 = re.compile(r"^[0-9a-f]{64}$")


def test_manifest_pins_every_download():
    comfy = MANIFEST["comfyui"]
    assert re.match(r"^[0-9a-f]{40}$", comfy["commit"]) and comfy["repo"].startswith("https://")
    assert comfy["platforms"]
    for key, variant in comfy["platforms"].items():
        assert variant["kind"] in ("portable", "source"), key
        if variant["kind"] == "portable":
            # A release asset of the pinned version, never `latest`: the hash must stay true.
            assert SHA256.match(variant["sha256"]) and variant["size"] > 0, key
            assert f"/v{comfy['version']}/" in variant["url"] and variant["url"].endswith("/" + variant["asset"]), key
    names = set()
    for model in MANIFEST["models"]:
        # A gated model's hash is shown only to an account with access: the install reads it with the owner's token until it is pinned
        assert (SHA256.match(model["sha256"]) if not model.get("gated") or model["sha256"] else True) and model["size"] > 0, model["name"]
        assert model.get("kind", "image") in ("image", "audio", "style"), model["name"]
        # A Hugging Face URL names a commit, never a branch: the hash must stay true.
        assert re.search(r"/resolve/[0-9a-f]{40}/", model["url"]), model["url"]
        assert model["url"].endswith(Path(model["name"]).suffix)
        assert model["licence"] and model["source"]
        assert (model["folder"], model["name"]) not in names
        names.add((model["folder"], model["name"]))
    # A custom node is fetched by git at a pinned commit, never a branch: the commit is its integrity check
    nodes = set()
    for node in MANIFEST["customNodes"]:
        assert node.get("kind", "image") in ("image", "audio", "style"), node["name"]
        assert re.match(r"^[0-9a-f]{40}$", node["commit"]), node["name"]
        assert node["repo"].startswith("https://") and node["licence"] and node["source"], node["name"]
        assert node["nodes"] and all(isinstance(name, str) and name for name in node["nodes"]), node["name"]
        assert node["name"] not in nodes
        nodes.add(node["name"])


LOADERS = {
    "CheckpointLoaderSimple": ("checkpoints", "ckpt_name"),
    "VAELoader": ("vae", "vae_name"),
    "LoadBackgroundRemovalModel": ("background_removal", "bg_removal_name"),
}


@pytest.mark.parametrize("workflow_file", sorted(comfyui_check.WORKFLOW.parent.glob("*.json")), ids=lambda p: p.name)
def test_every_engine_workflow_loads_only_manifest_models(workflow_file):
    if workflow_file.name.endswith(".inputs.json"):
        pytest.skip("an input map, not a graph")
    workflow = json.loads(workflow_file.read_text(encoding="utf-8"))
    available = {(m["folder"], m["name"]) for m in MANIFEST["models"]}
    loaders = [node for node in workflow.values() if node["class_type"] in LOADERS]
    assert loaders
    for node in loaders:
        folder, field = LOADERS[node["class_type"]]
        assert (folder, node["inputs"][field]) in available


def test_missing_audio_models_only_warn_and_say_what_it_makes(fake):
    for model in MANIFEST["models"]:
        if model.get("kind") == "audio":
            fake.models[model["folder"]].remove(model["name"])
    report = comfyui_check.check(fake.url, MANIFEST)
    assert report["ready"] and report["kinds"] == ["image", "style"]
    assert any("install --audio" in w for w in report["warnings"])


def test_with_every_model_it_makes_images_and_sounds(fake):
    assert comfyui_check.check(fake.url, MANIFEST)["kinds"] == ["audio", "image", "style"]


def test_the_sound_workflow_is_a_valid_graph():
    graph = json.loads(comfyui_check.AUDIO_WORKFLOW.read_text(encoding="utf-8"))
    for node_id, node in graph.items():
        for value in node["inputs"].values():
            if isinstance(value, list):
                assert value[0] in graph, f"node {node_id} links to a missing node {value[0]}"
    saves = [n for n in graph.values() if n["class_type"].startswith("SaveAudio")]
    assert len(saves) == 1
    names = {m["name"] for m in MANIFEST["models"] if m.get("kind") == "audio"}
    assert {graph["4"]["inputs"]["ckpt_name"], graph["10"]["inputs"]["clip_name"]} == names
