"""The setup pipeline: platform detection, the plan, the install over a local HTTP server (resume, hashes, idempotence, never
touching what is not ours), the source install with a fake runner, start and stop of a fake ComfyUI, the command line."""

import hashlib
import json
import shutil
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen_setup  # noqa: E402
import comfyui_check  # noqa: E402
from artgen_setup import Installer, Probe, SetupError  # noqa: E402

COMMIT = "a" * 40


@pytest.mark.parametrize("probe, key", [
    (Probe("win32", "AMD64", ["Intel(R) UHD Graphics 770", "NVIDIA GeForce RTX 3070"]), "windows-nvidia"),
    (Probe("win32", "ARM64", ["NVIDIA RTX A500"]), "windows-nvidia-arm64"),
    (Probe("win32", "AMD64", ["AMD Radeon RX 7800 XT"]), "windows-amd"),
    (Probe("win32", "AMD64", ["Intel(R) Arc(TM) A770 Graphics"]), "windows-intel"),
    (Probe("darwin", "arm64", []), "macos-arm64"),
    (Probe("linux", "x86_64", ["NVIDIA GeForce RTX 4090"]), "linux-nvidia"),
])
def test_detect_picks_the_platform_of_the_manifest(probe, key):
    assert artgen_setup.detect(probe) == key
    assert artgen_setup.variant(comfyui_check.load_manifest(), key)


@pytest.mark.parametrize("probe", [
    Probe("darwin", "x86_64", []),
    Probe("win32", "AMD64", ["Microsoft Basic Display Adapter"]),
    Probe("linux", "x86_64", []),
    Probe("freebsd14", "amd64", []),
])
def test_detect_says_why_a_machine_has_no_platform(probe):
    with pytest.raises(SetupError):
        artgen_setup.detect(probe)


# --- a manifest of small files served by a local HTTP server -------------------------------------------------------


class Files:
    def __init__(self):
        self.blobs = {}
        self.requests = []
        self.auth = []  # the Authorization header of each request, by path
        self.redirects = {}  # path -> URL answered with 302

    def add(self, path, data):
        self.blobs[path] = data
        return hashlib.sha256(data).hexdigest()


@pytest.fixture
def server():
    files = Files()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            files.requests.append((self.path, self.headers.get("Range")))
            files.auth.append((self.path, self.headers.get("Authorization")))
            if self.path in files.redirects:
                self.send_response(302)
                self.send_header("Location", files.redirects[self.path])
                self.end_headers()
                return
            data = files.blobs.get(self.path)
            if data is None:
                self.send_response(404)
                self.end_headers()
                return
            start = 0
            if self.headers.get("Range"):
                start = int(self.headers["Range"].split("=")[1].split("-")[0])
                self.send_response(206)
            else:
                self.send_response(200)
            self.send_header("Content-Length", str(len(data) - start))
            self.end_headers()
            self.wfile.write(data[start:])

    httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    files.url = f"http://127.0.0.1:{httpd.server_port}"
    yield files
    httpd.shutdown()


@pytest.fixture
def manifest(server):
    archive = b"fake portable archive" * 100
    model_a = b"model a" * 5000
    model_b = b"model b" * 3000
    return {
        "manifest": 2,
        "comfyui": {
            "version": "9.9.9", "repo": "https://example.invalid/ComfyUI.git", "commit": COMMIT, "licence": "GPL-3.0",
            "platforms": {
                "windows-nvidia": {"kind": "portable", "asset": "portable.7z", "url": f"{server.url}/portable.7z",
                                   "size": len(archive), "sha256": server.add("/portable.7z", archive)},
                "macos-arm64": {"kind": "source", "torch": []},
                "linux-nvidia": {"kind": "source", "torch": ["--extra-index-url", "https://torch.invalid/cu"]},
            },
        },
        "models": [
            {"name": "a.safetensors", "folder": "checkpoints", "url": f"{server.url}/a", "size": len(model_a),
             "sha256": server.add("/a", model_a), "licence": "MIT", "source": "x"},
            {"name": "b.safetensors", "folder": "vae", "url": f"{server.url}/b", "size": len(model_b),
             "sha256": server.add("/b", model_b), "licence": "MIT", "source": "x"},
        ],
    }


@pytest.fixture(autouse=True)
def local_settings(tmp_path, monkeypatch):
    settings = tmp_path / "local.json"
    monkeypatch.setattr(comfyui_check, "LOCAL_SETTINGS", settings)
    return settings


def fake_unpack(version="9.9.9"):
    def unpack(archive, target):
        inner = Path(target) / "ComfyUI_windows_portable" / "ComfyUI"
        inner.mkdir(parents=True)
        (inner / "main.py").write_text("", encoding="utf-8")
        (inner / "comfyui_version.py").write_text(f'__version__ = "{version}"\n', encoding="utf-8")
        (inner.parent / "python_embeded").mkdir()
    return unpack


def test_the_plan_of_an_empty_root_downloads_everything(manifest, tmp_path):
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", tmp_path / "root")
    assert [s.state for s in steps] == ["download", "download", "download"]
    assert sum(s.size for s in steps) == manifest["comfyui"]["platforms"]["windows-nvidia"]["size"] + sum(m["size"] for m in manifest["models"])
    assert all(s.licence for s in steps)
    assert "GB" in artgen_setup.describe(steps)


def test_a_portable_install_then_a_second_run_only_checks(manifest, server, tmp_path, local_settings):
    root = tmp_path / "root"
    events = []
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root)
    Installer(manifest, "windows-nvidia", root, emit=events.append, unpack=fake_unpack()).install(steps)
    assert (root / "ComfyUI" / "main.py").is_file() and (root / "python_embeded").is_dir()
    for model in manifest["models"]:
        assert artgen_setup.model_path(root, model).read_bytes() == server.blobs["/" + model["url"].rsplit("/", 1)[1]]
    assert json.loads((root / artgen_setup.MARKER).read_text(encoding="utf-8"))["platform"] == "windows-nvidia"
    assert not (tmp_path / "root.downloads").exists() and not (tmp_path / "root.unpack").exists()
    assert json.loads(local_settings.read_text(encoding="utf-8"))["comfyuiRoot"] == str(root)
    assert any(e["event"] == "progress" for e in events)

    asked = len(server.requests)
    again = artgen_setup.make_plan(manifest, "windows-nvidia", root)
    assert [s.state for s in again] == ["present", "verify", "verify"] and sum(s.size for s in again) == 0
    Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(again)
    assert len(server.requests) == asked


def test_an_interrupted_download_resumes_with_a_range(manifest, server, tmp_path):
    model = manifest["models"][0]
    dest = tmp_path / "m" / model["name"]
    dest.parent.mkdir()
    data = server.blobs["/a"]
    (dest.parent / (model["name"] + ".part")).write_bytes(data[:1000])
    artgen_setup.download(model["url"], dest, model["size"], model["sha256"])
    assert dest.read_bytes() == data
    assert server.requests[-1] == ("/a", "bytes=1000-")


def test_a_wrong_hash_stays_a_part_and_is_never_used(manifest, tmp_path):
    model = dict(manifest["models"][0], sha256="0" * 64)
    dest = tmp_path / model["name"]
    with pytest.raises(SetupError, match="SHA256"):
        artgen_setup.download(model["url"], dest, model["size"], model["sha256"])
    assert not dest.exists() and (tmp_path / (model["name"] + ".part")).exists()


def test_what_is_not_ours_stops_the_install_untouched(manifest, tmp_path):
    other = tmp_path / "other"
    fake_unpack("0.1.0")(None, tmp_path / "x")
    (tmp_path / "x" / "ComfyUI_windows_portable").rename(other)
    with pytest.raises(SetupError, match="0.1.0"):
        artgen_setup.make_plan(manifest, "windows-nvidia", other)

    foreign = tmp_path / "foreign"
    foreign.mkdir()
    (foreign / "notes.txt").write_text("mine", encoding="utf-8")
    with pytest.raises(SetupError, match="not empty"):
        artgen_setup.make_plan(manifest, "windows-nvidia", foreign)

    root = tmp_path / "root"
    Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(artgen_setup.make_plan(manifest, "windows-nvidia", root, skip_models=True))
    mine = artgen_setup.model_path(root, manifest["models"][0])
    mine.parent.mkdir(parents=True)
    mine.write_bytes(b"a model of the owner")
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root)
    with pytest.raises(SetupError, match="not this tool's"):
        Installer(manifest, "windows-nvidia", root).install(steps)
    assert mine.read_bytes() == b"a model of the owner"


class FakeRunner:
    def __init__(self, version="9.9.9"):
        self.commands = []
        self.version = version

    def __call__(self, command, cwd=None):
        self.commands.append([str(c) for c in command])
        if command[:2] == ["git", "clone"]:
            repo = Path(command[-1])
            repo.mkdir(parents=True)
            (repo / "main.py").write_text("", encoding="utf-8")
            (repo / "comfyui_version.py").write_text(f'__version__ = "{self.version}"\n', encoding="utf-8")
            (repo / "requirements.txt").write_text("", encoding="utf-8")
        elif command[1:3] == ["-m", "venv"]:
            python = artgen_setup.venv_python(Path(command[3]).parent)
            python.parent.mkdir(parents=True)
            python.write_text("", encoding="utf-8")


def test_a_source_install_clones_the_pinned_commit_and_builds_a_venv(manifest, tmp_path):
    root = tmp_path / "root"
    run = FakeRunner()
    steps = artgen_setup.make_plan(manifest, "linux-nvidia", root, skip_models=True)
    assert [s.state for s in steps] == ["install"]
    Installer(manifest, "linux-nvidia", root, run=run, git_head=lambda repo: COMMIT).install(steps)
    assert run.commands[0][:6] == ["git", "clone", "--depth", "1", "--branch", "v9.9.9"]
    pip = [c for c in run.commands if "pip" in c]
    assert pip[0][-2:] == ["--extra-index-url", "https://torch.invalid/cu"] and pip[1][-2] == "-r"
    assert (root / "ComfyUI" / "main.py").is_file()
    assert [s.state for s in artgen_setup.make_plan(manifest, "linux-nvidia", root, skip_models=True)] == ["present"]


def test_a_moved_tag_stops_the_source_install(manifest, tmp_path):
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "macos-arm64", root, skip_models=True)
    with pytest.raises(SetupError, match="tag moved"):
        Installer(manifest, "macos-arm64", root, run=FakeRunner(), git_head=lambda repo: "b" * 40).install(steps)
    assert not (root / "ComfyUI").exists()


def test_the_launch_command_of_each_layout(tmp_path):
    portable = tmp_path / "portable"
    (portable / "python_embeded").mkdir(parents=True)
    (portable / "python_embeded" / "python.exe").write_text("", encoding="utf-8")
    command = artgen_setup.launch_command(portable, "http://127.0.0.1:9000")
    assert command[0].endswith("python.exe") and "--windows-standalone-build" in command
    assert command[-5:] == ["--listen", "127.0.0.1", "--port", "9000", "--disable-auto-launch"]
    source = artgen_setup.launch_command(tmp_path / "src", "http://127.0.0.1:8188", extra=["--lowvram"])
    assert "venv" in source[0] and "--windows-standalone-build" not in source and source[-1] == "--lowvram"


FAKE_MAIN = '''
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
port = int(sys.argv[sys.argv.index("--port") + 1])
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        self.send_response(200); self.end_headers(); self.wfile.write(b"{}")
HTTPServer(("127.0.0.1", port), H).serve_forever()
'''


def test_start_and_stop_a_local_comfyui(tmp_path):
    root = tmp_path / "fakecomfy"
    (root / "ComfyUI").mkdir(parents=True)
    (root / "ComfyUI" / "main.py").write_text(FAKE_MAIN, encoding="utf-8")
    import socket
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    url = f"http://127.0.0.1:{port}"
    pid = artgen_setup.start(root, url, wait_s=60, python=sys.executable)
    try:
        assert pid and artgen_setup._reachable(url)
        assert artgen_setup.start(root, url, python=sys.executable) is None  # already running: nothing started
    finally:
        assert artgen_setup.stop(root)
    assert not (root / artgen_setup.PID_FILE).exists()
    assert not artgen_setup.stop(root)


def test_stop_leaves_a_comfyui_started_by_hand(tmp_path):
    root = tmp_path / "root"
    root.mkdir()
    (root / artgen_setup.PID_FILE).write_text(json.dumps({"pid": 999999999}), encoding="utf-8")
    assert not artgen_setup.stop(root)


def test_the_command_line_asks_before_it_downloads(manifest, tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(comfyui_check, "load_manifest", lambda path=None: manifest)
    root = tmp_path / "root"
    probe = lambda: Probe("win32", "AMD64", ["NVIDIA GeForce RTX 3070"])  # noqa: E731
    assert artgen_setup.main(["install", "--root", str(root)], probe=probe, ask=lambda prompt: "n") == artgen_setup.EXIT_DECLINED
    assert not root.exists()
    assert artgen_setup.main(["install", "--root", str(root), "--json"], probe=probe) == artgen_setup.EXIT_DECLINED
    events = [json.loads(line) for line in capsys.readouterr().out.splitlines() if line.startswith("{")]
    assert events[0]["event"] == "plan" and events[0]["platform"] == "windows-nvidia" and events[0]["downloadBytes"] > 0
    assert not root.exists()


def test_the_panel_actions_plan_and_install_without_asking(manifest, tmp_path):
    root = tmp_path / "root"
    probe = lambda: Probe("win32", "AMD64", ["NVIDIA GeForce RTX 3070"])  # noqa: E731
    events = []
    setup = artgen_setup.LocalSetup("http://127.0.0.1:1", root, probe, manifest,
                                    installer=lambda *a, **k: Installer(*a, unpack=fake_unpack(), **k))
    setup.run("Plan", events.append)
    assert events[-1]["event"] == "plan" and events[-1]["downloadBytes"] > 0 and not root.exists()
    events.clear()
    setup.run("Install", events.append)
    assert (root / "ComfyUI" / "main.py").is_file()
    assert events[0]["event"] == "plan" and any(e["event"] == "step_done" for e in events)


def test_a_cancelled_install_stops_and_keeps_what_it_downloaded(manifest, tmp_path):
    import threading
    cancel = threading.Event()
    cancel.set()
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root)
    with pytest.raises(SetupError, match="cancelled"):
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack(), cancel=cancel).install(steps)
    assert not root.exists()


def test_a_comfyui_that_exits_while_starting_says_why(tmp_path):
    root = tmp_path / "fakecomfy"
    (root / "ComfyUI").mkdir(parents=True)
    (root / "ComfyUI" / "main.py").write_text("import sys\nprint('Another ComfyUI is already using this database')\nsys.exit(1)\n", encoding="utf-8")
    with pytest.raises(SetupError, match="already using this database"):
        artgen_setup.start(root, "http://127.0.0.1:9", wait_s=60, python=sys.executable)
    assert not (root / artgen_setup.PID_FILE).exists()


def test_started_here_is_only_a_comfyui_this_tool_started(tmp_path, monkeypatch):
    root = tmp_path / "root"
    root.mkdir()
    setup = artgen_setup.LocalSetup("http://127.0.0.1:1", root)
    assert not setup.started_here()  # no pid file: started by hand, or not at all
    (root / artgen_setup.PID_FILE).write_text(json.dumps({"pid": 4242}), encoding="utf-8")
    asked = []
    monkeypatch.setattr(artgen_setup, "_ours", lambda r, pid: asked.append(pid) or True)
    assert setup.started_here() and setup.started_here()
    assert asked == [4242]  # checked once per pid


# --- the audio models (gated) -------------------------------------------------------------------------------------------


def gated(manifest, server, sha=True):
    data = b"gated audio model" * 4000
    model = {"name": "sao.safetensors", "folder": "checkpoints", "kind": "audio", "gated": True, "url": f"{server.url}/sao",
             "size": len(data), "sha256": server.add("/sao", data) if sha else None, "licence": "Community", "source": "https://hf.invalid/sao"}
    if not sha:
        server.add("/sao", data)
    manifest["models"].append(model)
    return model, data


def test_the_audio_models_are_planned_only_when_asked(manifest, server, tmp_path):
    model, _ = gated(manifest, server)
    names = lambda steps: [s.what for s in steps if s.kind == "model"]  # noqa: E731
    assert "checkpoints/sao.safetensors" not in names(artgen_setup.make_plan(manifest, "windows-nvidia", tmp_path / "r"))
    both = artgen_setup.make_plan(manifest, "windows-nvidia", tmp_path / "r", kinds=("image", "audio"))
    assert "checkpoints/sao.safetensors" in names(both)


def test_a_gated_model_needs_a_token_and_says_how(manifest, server, tmp_path):
    gated(manifest, server)
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, skip_models=False, kinds=("audio",))
    with pytest.raises(SetupError, match="HF_TOKEN"):
        # The server serves it without a token: the refusal is ours, before a byte, when the manifest has no hash
        manifest["models"][-1]["sha256"] = None
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(steps)


def test_a_gated_model_is_fetched_with_the_token_and_its_hash_read_from_hugging_face(manifest, server, tmp_path):
    model, data = gated(manifest, server, sha=False)
    root = tmp_path / "root"
    events = []
    asked = []

    def hash_of(m, token):
        asked.append((m["name"], token))
        return hashlib.sha256(data).hexdigest()

    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("audio",))
    Installer(manifest, "windows-nvidia", root, emit=events.append, unpack=fake_unpack(), token="secret", hash_of_gated=hash_of).install(steps)
    assert artgen_setup.model_path(root, model).read_bytes() == data
    assert asked == [("sao.safetensors", "secret")]
    assert ("/sao", "Bearer secret") in server.auth
    assert ("/portable.7z", None) in server.auth, "the token goes only with a gated model"
    assert any("pin it" in e.get("text", "") for e in events)


def test_the_token_never_follows_a_redirect_to_another_host(manifest, server, tmp_path):
    model, data = gated(manifest, server)
    # 127.0.0.1 and localhost are one server but two hosts, as huggingface.co and its CDN are
    server.redirects["/sao"] = server.url.replace("127.0.0.1", "localhost") + "/cdn"
    server.add("/cdn", data)
    artgen_setup.download(model["url"], tmp_path / "sao.safetensors", model["size"], model["sha256"], token="secret")
    assert (tmp_path / "sao.safetensors").read_bytes() == data
    assert ("/sao", "Bearer secret") in server.auth and ("/cdn", None) in server.auth


def test_a_refused_gated_download_says_what_to_do(manifest, server, tmp_path):
    with pytest.raises(SetupError, match="HTTP 404"):
        artgen_setup.download(f"{server.url}/missing", tmp_path / "x", 10, "0" * 64, token="secret")


def test_a_gated_model_placed_by_hand_is_checked_by_size_without_a_token(manifest, server, tmp_path):
    model, data = gated(manifest, server, sha=False)
    root = tmp_path / "root"
    events = []
    Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(artgen_setup.make_plan(manifest, "windows-nvidia", root, skip_models=True))
    dest = artgen_setup.model_path(root, model)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("audio",))
    assert [s.state for s in steps if s.kind == "model"] == ["verify"]
    Installer(manifest, "windows-nvidia", root, emit=events.append, unpack=fake_unpack()).install(steps)
    assert any("placed by hand" in e.get("text", "") for e in events)
    dest.write_bytes(data[:-1])
    with pytest.raises(SetupError, match="bytes"):
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(steps)


def test_the_token_comes_from_the_environment_or_local_json(local_settings):
    assert artgen_setup.hf_token(env={"HF_TOKEN": "a"}) == "a"
    assert artgen_setup.hf_token(env={}) is None
    local_settings.write_text(json.dumps({"hfToken": "b"}), encoding="utf-8")
    assert artgen_setup.hf_token(env={}) == "b"
    assert artgen_setup.hf_token(env={"HF_TOKEN": "a"}) == "a"


def test_the_hash_of_a_gated_file_is_read_at_the_pinned_commit():
    seen = {}

    class Answer:
        def __init__(self, body):
            self.body = body

        def __enter__(self):
            return self

        def __exit__(self, *args):
            return False

        def read(self):
            return self.body

    def opener(request, timeout):
        seen["url"], seen["auth"], seen["data"] = request.full_url, request.get_header("Authorization"), request.data
        return Answer(json.dumps([{"path": "model.safetensors", "lfs": {"oid": "a" * 64}}]).encode())

    model = {"name": "m", "url": "https://huggingface.co/org/repo/resolve/" + "c" * 40 + "/model.safetensors", "source": "x"}
    assert artgen_setup.gated_sha256(model, "tok", opener) == "a" * 64
    assert seen["url"] == "https://huggingface.co/api/models/org/repo/paths-info/" + "c" * 40
    assert seen["auth"] == "Bearer tok" and seen["data"] == b"paths=model.safetensors"

    def masked(request, timeout):
        return Answer(json.dumps([{"path": "model.safetensors", "lfs": {"oid": "*" * 64}}]).encode())

    with pytest.raises(SetupError, match="accept its licence"):
        artgen_setup.gated_sha256(model, "tok", masked)


# --- the style parts: two models and a custom node fetched by git at a pinned commit ------------------------------------------

needs_git = pytest.mark.skipif(shutil.which("git") is None, reason="git is not installed")
GIT_IDENTITY = ["-c", "user.name=test", "-c", "user.email=test@invalid"]


def make_node_repo(path, text="the node"):
    """A local git repository with one commit (the custom node's origin); returns its commit."""
    path.mkdir()
    (path / "__init__.py").write_text(text, encoding="utf-8")
    git = lambda *args: subprocess.run(["git", "-C", str(path), *GIT_IDENTITY, *args], check=True, capture_output=True, text=True)  # noqa: E731
    git("init", "--quiet")
    git("config", "uploadpack.allowAnySHA1InWant", "true")
    git("add", ".")
    git("commit", "--quiet", "-m", "node")
    return git("rev-parse", "HEAD").stdout.strip()


def with_style(manifest, server, repo=None, commit=COMMIT):
    data = b"style model" * 3000
    manifest["models"].append({"name": "ip.safetensors", "folder": "ipadapter", "kind": "style", "url": f"{server.url}/ip", "size": len(data),
                               "sha256": server.add("/ip", data), "licence": "Apache 2.0", "source": "x"})
    manifest["customNodes"] = [{"name": "Node_plus", "kind": "style", "repo": str(repo or "https://example.invalid/Node_plus.git"),
                                "commit": commit, "licence": "GPL-3.0 (loaded by ComfyUI)", "source": "x", "nodes": ["NodeA"]}]
    return manifest["customNodes"][0]


def test_the_style_parts_are_planned_only_with_style_after_comfyui_and_before_the_models(manifest, server, tmp_path):
    with_style(manifest, server)
    root = tmp_path / "root"
    assert not [s for s in artgen_setup.make_plan(manifest, "windows-nvidia", root) if s.kind == "customnode" or "ipadapter" in s.what]
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    assert [s.kind for s in steps] == ["comfyui", "customnode", "model", "model", "model"]
    node = steps[1]
    assert node.state == "install" and node.size == 0 and node.licence.startswith("GPL-3.0") and node.url == "https://example.invalid/Node_plus.git"
    assert Path(node.dest) == root / "ComfyUI" / "custom_nodes" / "Node_plus"
    assert "ipadapter/ip.safetensors" in [s.what for s in steps]
    both = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "audio", "style"))
    assert len(both) == len(steps)  # the fixture has no audio model
    text = artgen_setup.describe(steps)
    assert "GPL-3.0" in text and text.count("restart ComfyUI") == 1


@needs_git
def test_the_installer_checks_out_the_pinned_commit_and_a_second_plan_says_present(manifest, server, tmp_path):
    commit = make_node_repo(tmp_path / "origin")
    with_style(manifest, server, tmp_path / "origin", commit)
    root = tmp_path / "root"
    events = []
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    Installer(manifest, "windows-nvidia", root, emit=events.append, unpack=fake_unpack()).install(steps)
    node = root / "ComfyUI" / "custom_nodes" / "Node_plus"
    assert (node / "__init__.py").read_text(encoding="utf-8") == "the node"
    assert artgen_setup.read_git_head(node) == commit
    assert not (tmp_path / "root.unpack").exists()
    assert sum("restart ComfyUI" in e.get("text", "") for e in events) == 1

    again = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    assert [s.state for s in again if s.kind == "customnode"] == ["present"]
    assert "restart ComfyUI" not in artgen_setup.describe(again)
    events.clear()
    Installer(manifest, "windows-nvidia", root, emit=events.append, unpack=fake_unpack()).install(again)
    assert not any("restart ComfyUI" in e.get("text", "") for e in events)


@needs_git
def test_a_wrong_head_after_the_fetch_stops_the_install_and_leaves_nothing(manifest, server, tmp_path):
    commit = make_node_repo(tmp_path / "origin")
    with_style(manifest, server, tmp_path / "origin", commit)
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, skip_models=True, kinds=("image", "style"))
    with pytest.raises(SetupError, match="commit moved"):
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack(), git_head=lambda folder: "b" * 40).install(steps)
    assert not (root / "ComfyUI" / "custom_nodes" / "Node_plus").exists()


@needs_git
def test_a_folder_that_is_not_this_tools_stops_the_plan_untouched(manifest, server, tmp_path):
    commit = make_node_repo(tmp_path / "origin")
    with_style(manifest, server, tmp_path / "origin", commit)
    root = tmp_path / "root"
    Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(artgen_setup.make_plan(manifest, "windows-nvidia", root, skip_models=True))
    node = root / "ComfyUI" / "custom_nodes" / "Node_plus"
    node.mkdir(parents=True)
    (node / "mine.py").write_text("a file of the owner", encoding="utf-8")
    with pytest.raises(SetupError, match="not a git checkout"):
        artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    assert (node / "mine.py").is_file()
    shutil.rmtree(node)
    make_node_repo(node, "a checkout of the owner")
    with pytest.raises(SetupError, match="manifest pins"):
        artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    assert (node / "__init__.py").read_text(encoding="utf-8") == "a checkout of the owner"


def test_a_folder_without_a_git_of_its_own_has_no_head(tmp_path):
    # inside another work tree, `git rev-parse` would answer with that tree's HEAD
    assert artgen_setup.read_git_head(tmp_path) is None


def test_a_missing_git_stops_the_install_before_the_first_download(manifest, server, tmp_path, monkeypatch):
    with_style(manifest, server)
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    monkeypatch.setattr(shutil, "which", lambda name: None)
    with pytest.raises(SetupError, match="git is not installed"):
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(steps)
    assert not root.exists() and not server.requests


def test_the_style_flag_adds_the_parts_to_the_plan_and_dry_run_prints_it(manifest, server, tmp_path, monkeypatch, capsys):
    with_style(manifest, server)
    monkeypatch.setattr(comfyui_check, "load_manifest", lambda path=None: manifest)
    root = tmp_path / "root"
    probe = lambda: Probe("win32", "AMD64", ["NVIDIA GeForce RTX 3070"])  # noqa: E731
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run"], probe=probe) == artgen_setup.EXIT_OK
    assert "Node_plus" not in capsys.readouterr().out
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run", "--style"], probe=probe) == artgen_setup.EXIT_OK
    out = capsys.readouterr().out
    assert "Node_plus" in out and "ipadapter/ip.safetensors" in out and "restart ComfyUI" in out
    assert not root.exists() and not server.requests
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run", "--audio", "--style"], probe=probe) == artgen_setup.EXIT_OK


# --- the structure model: a ControlNet file, no custom node ---------------------------------------------------------------------


def with_structure(manifest, server):
    data = b"controlnet model" * 3000
    model = {"name": "canny.safetensors", "folder": "controlnet", "kind": "structure", "url": f"{server.url}/canny", "size": len(data),
             "sha256": server.add("/canny", data), "licence": "Apache 2.0", "source": "x"}
    manifest["models"].append(model)
    return model, data


def test_the_structure_model_is_planned_only_with_structure_and_needs_no_custom_node(manifest, server, tmp_path):
    model, _ = with_structure(manifest, server)
    root = tmp_path / "root"
    assert "controlnet/canny.safetensors" not in [s.what for s in artgen_setup.make_plan(manifest, "windows-nvidia", root)]
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "structure"))
    assert [s.kind for s in steps] == ["comfyui", "model", "model", "model"]
    step = next(s for s in steps if s.what == "controlnet/canny.safetensors")
    assert step.state == "download" and step.size == model["size"] and step.licence == "Apache 2.0"
    assert Path(step.dest) == root / "ComfyUI" / "models" / "controlnet" / "canny.safetensors"
    assert artgen_setup.RESTART_NOTE not in artgen_setup.describe(steps)


def test_the_structure_and_style_kinds_are_planned_together(manifest, server, tmp_path):
    with_structure(manifest, server)
    with_style(manifest, server)
    root = tmp_path / "root"
    both = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style", "structure"))
    assert sorted(s.what for s in both if s.kind == "model") == ["checkpoints/a.safetensors", "controlnet/canny.safetensors",
                                                                 "ipadapter/ip.safetensors", "vae/b.safetensors"]
    assert [s.kind for s in both].count("customnode") == 1
    only_style = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "style"))
    assert "controlnet/canny.safetensors" not in [s.what for s in only_style]


def test_the_structure_model_is_downloaded_and_hash_checked_like_the_style_models(manifest, server, tmp_path):
    model, data = with_structure(manifest, server)
    root = tmp_path / "root"
    steps = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "structure"))
    Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(steps)
    assert artgen_setup.model_path(root, model).read_bytes() == data
    again = artgen_setup.make_plan(manifest, "windows-nvidia", root, kinds=("image", "structure"))
    assert [s.state for s in again if s.what == "controlnet/canny.safetensors"] == ["verify"]
    artgen_setup.model_path(root, model).write_bytes(b"x" * len(data))
    with pytest.raises(SetupError):
        Installer(manifest, "windows-nvidia", root, unpack=fake_unpack()).install(again)


def test_the_structure_flag_adds_the_model_to_the_plan_and_dry_run_prints_it(manifest, server, tmp_path, monkeypatch, capsys):
    with_structure(manifest, server)
    with_style(manifest, server)
    monkeypatch.setattr(comfyui_check, "load_manifest", lambda path=None: manifest)
    root = tmp_path / "root"
    probe = lambda: Probe("win32", "AMD64", ["NVIDIA GeForce RTX 3070"])  # noqa: E731
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run"], probe=probe) == artgen_setup.EXIT_OK
    assert "controlnet/canny" not in capsys.readouterr().out
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run", "--structure"], probe=probe) == artgen_setup.EXIT_OK
    out = capsys.readouterr().out
    assert "controlnet/canny.safetensors" in out and "Node_plus" not in out and "restart ComfyUI" not in out
    assert artgen_setup.main(["install", "--root", str(root), "--dry-run", "--style", "--structure"], probe=probe) == artgen_setup.EXIT_OK
    out = capsys.readouterr().out
    assert "controlnet/canny.safetensors" in out and "ipadapter/ip.safetensors" in out and "restart ComfyUI" in out
    assert not root.exists() and not server.requests
