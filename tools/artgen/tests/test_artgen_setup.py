"""The setup pipeline: platform detection, the plan, the install over a local HTTP server (resume, hashes, idempotence, never
touching what is not ours), the source install with a fake runner, start and stop of a fake ComfyUI, the command line."""

import hashlib
import json
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
