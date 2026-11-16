#!/usr/bin/env python3
"""Sets up, starts and stops a local ComfyUI on any supported machine (tools/artgen/CLAUDE.md, "Setup").

One pipeline for every platform: it detects the platform and the GPU, picks that platform's variant of
`comfyui/manifest.json` (a pinned portable release on Windows, a pinned git commit and a venv on macOS and Linux), says what
it would download and where (size, licence), asks, then installs with progress and resume. Idempotent: what is there and
matches the manifest is left alone, so a second run only checks. It never overwrites or deletes what it did not create.

    python tools/artgen/artgen_setup.py detect            the platform key and the GPUs seen
    python tools/artgen/artgen_setup.py install [--root DIR] [--yes] [--dry-run] [--json] [--skip-models]
    python tools/artgen/artgen_setup.py start [--wait S] [-- ARGS]  ComfyUI in the background on the address of local.json
    python tools/artgen/artgen_setup.py stop              only the ComfyUI this tool started
    python tools/artgen/artgen_setup.py status            ready / not_running / not_installed and why

Exit codes: 0 done, 1 failed or refused, 2 the owner said no (or `--json` without `--yes`). Standard library only.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

import comfyui_check

ARTGEN_DIR = comfyui_check.ARTGEN_DIR
START_BAT = ARTGEN_DIR / "comfyui" / "start-local.bat"
MARKER = ".artgen-setup.json"
PID_FILE = "artgen-comfyui.pid"
LOG_FILE = "artgen-comfyui.log"
CHUNK = 1 << 20
PROGRESS_EVERY_S = 0.5

EXIT_OK = 0
EXIT_FAILED = 1
EXIT_DECLINED = 2


class SetupError(Exception):
    """The install cannot go on; the message says what to do."""


CANCELLED = "cancelled: what is downloaded stays, the next install resumes"


def _check_cancel(cancel):
    if cancel is not None and cancel.is_set():
        raise SetupError(CANCELLED)


# --- the platform -----------------------------------------------------------------------------------------------------


@dataclass
class Probe:
    """What the machine is: `system` as `sys.platform`, `machine` as `platform.machine()`, the names of its GPUs."""

    system: str
    machine: str
    gpus: list = field(default_factory=list)


def _run_text(command):
    try:
        return subprocess.run(command, capture_output=True, text=True, timeout=30, check=False).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def probe_machine():
    gpus = []
    if sys.platform == "win32":
        out = _run_text(["powershell", "-NoProfile", "-Command", "(Get-CimInstance Win32_VideoController).Name"])
        gpus = [line.strip() for line in out.splitlines() if line.strip()]
    elif sys.platform.startswith("linux"):
        if shutil.which("nvidia-smi"):
            out = _run_text(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"])
            gpus = [f"NVIDIA {line.strip()}" if "nvidia" not in line.lower() else line.strip() for line in out.splitlines() if line.strip()]
    return Probe(sys.platform, platform.machine(), gpus)


def _vendor(gpus):
    """The GPU to set up for: a discrete NVIDIA or AMD card wins over the Intel graphics beside it."""
    names = " | ".join(gpus).lower()
    for vendor, words in (("nvidia", ("nvidia", "geforce", "quadro", "rtx")), ("amd", ("amd", "radeon")), ("intel", ("intel", "arc"))):
        if any(word in names for word in words):
            return vendor
    return None


def detect(probe):
    """The platform key of the manifest, or SetupError saying why this machine has none."""
    arm = probe.machine.lower() in ("arm64", "aarch64")
    vendor = _vendor(probe.gpus)
    seen = ", ".join(probe.gpus) or "no GPU found"
    if probe.system == "darwin":
        if arm:
            return "macos-arm64"
        raise SetupError("an Intel Mac has no GPU ComfyUI can use; use a generator on another machine")
    if probe.system == "win32":
        if vendor == "nvidia":
            return "windows-nvidia-arm64" if arm else "windows-nvidia"
        if arm:
            raise SetupError(f"Windows on ARM without an NVIDIA GPU ({seen}) is not supported")
        if vendor in ("amd", "intel"):
            return f"windows-{vendor}"
        raise SetupError(f"no NVIDIA, AMD or Intel GPU ({seen}); use a generator on another machine")
    if probe.system.startswith("linux"):
        if vendor == "nvidia" and not arm:
            return "linux-nvidia"
        raise SetupError(f"on Linux only an x86-64 machine with an NVIDIA GPU is supported ({seen})")
    raise SetupError(f"{probe.system} is not supported")


def variant(manifest, key):
    platforms = manifest["comfyui"]["platforms"]
    if key not in platforms:
        raise SetupError(f"the manifest has no platform {key} (it has {', '.join(sorted(platforms))})")
    return platforms[key]


# --- the plan -----------------------------------------------------------------------------------------------------------


@dataclass
class Step:
    """One thing to install: `state` is `present` (nothing to do), `verify` (there: its hash is checked), `download`
    (missing: `size` bytes come from `url`) or `install` (built on this machine: a clone and a venv)."""

    kind: str
    what: str
    dest: str
    state: str
    size: int = 0
    licence: str = ""
    url: str = ""

    def to_json(self):
        return {"kind": self.kind, "what": self.what, "dest": self.dest, "state": self.state, "size": self.size,
                "licence": self.licence, "url": self.url}


def installed_version(root):
    file = Path(root) / "ComfyUI" / "comfyui_version.py"
    if not file.is_file():
        return None
    for line in file.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip().startswith("__version__"):
            return line.split("=", 1)[1].strip().strip("\"'")
    return "unknown"


def read_marker(root):
    file = Path(root) / MARKER
    try:
        return json.loads(file.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def model_path(root, model):
    return Path(root) / "ComfyUI" / "models" / model["folder"] / model["name"]


KIND_IMAGE = "image"
KIND_AUDIO = "audio"
HF_API = "https://huggingface.co/api/models"


def model_kind(model):
    """What a model of the manifest is for: `image` (the default) or `audio`."""
    return model.get("kind", KIND_IMAGE)


def hf_token(env=None, settings_path=None):
    """The owner's Hugging Face token for a gated model: `HF_TOKEN`, else `hfToken` of the git-ignored local.json; never in git."""
    env = os.environ if env is None else env
    if env.get("HF_TOKEN"):
        return env["HF_TOKEN"]
    settings_path = Path(settings_path or comfyui_check.LOCAL_SETTINGS)
    try:
        return json.loads(settings_path.read_text(encoding="utf-8")).get("hfToken") or None
    except (OSError, ValueError):
        return None


def _hf_repo_and_revision(url):
    """`https://huggingface.co/<owner>/<repo>/resolve/<commit>/<path>` -> (owner/repo, commit, path)."""
    parts = urllib.parse.urlparse(url).path.strip("/").split("/")
    if len(parts) < 5 or parts[2] != "resolve":
        raise SetupError(f"{url} is not a Hugging Face file URL with a pinned commit")
    return f"{parts[0]}/{parts[1]}", parts[3], "/".join(parts[4:])


def gated_sha256(model, token, opener=None):
    """The SHA256 of a gated model's file at the manifest's commit, from the Hugging Face API (it shows it only to an account with access):
    the same source as every other hash of the manifest. The download is checked against it; the install prints it to be pinned."""
    repo, revision, path = _hf_repo_and_revision(model["url"])
    request = urllib.request.Request(f"{HF_API}/{repo}/paths-info/{revision}", data=urllib.parse.urlencode({"paths": path}).encode("ascii"),
                                     headers={"Authorization": f"Bearer {token}"})
    try:
        with (opener or urllib.request.urlopen)(request, timeout=60) as response:
            entries = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, ConnectionError, TimeoutError, ValueError) as error:
        raise SetupError(f"could not read the hash of {model['name']} from Hugging Face ({getattr(error, 'reason', error)})") from error
    oid = next((e.get("lfs", {}).get("oid") for e in entries if e.get("path") == path), None)
    if not (isinstance(oid, str) and len(oid) == 64 and all(c in "0123456789abcdef" for c in oid)):
        raise SetupError(f"Hugging Face shows no hash of {model['name']} to this token: accept its licence at {model['source']} first")
    return oid


def make_plan(manifest, key, root, skip_models=False, kinds=(KIND_IMAGE,)):
    """The steps of an install of platform `key` into `root`, or SetupError when something there is not this tool's. `kinds` picks the
    models: the image ones by default, `audio` adds the sound effects model (gated: it needs a token, `hf_token`)."""
    root = Path(root)
    comfy = manifest["comfyui"]
    var = variant(manifest, key)
    steps = []
    version = installed_version(root)
    marker = read_marker(root)
    what = f"ComfyUI {comfy['version']} ({key})"
    if version is not None and version != comfy["version"]:
        raise SetupError(f"{root} holds ComfyUI {version}, the manifest pins {comfy['version']}: move that folder away or choose another root")
    if version is None and root.exists() and any(p.name not in (MARKER,) for p in root.iterdir()):
        raise SetupError(f"{root} is not empty and holds no ComfyUI: choose another root")
    if var["kind"] == "portable":
        state = "present" if version else "download"
        steps.append(Step("comfyui", what, str(root), state, 0 if version else var["size"], comfy["licence"], var["url"]))
    else:
        done = version is not None and marker is not None and marker.get("platform") == key and marker.get("version") == comfy["version"]
        steps.append(Step("comfyui", f"{what}: git {comfy['commit'][:12]}, a venv, PyTorch and the requirements from PyPI", str(root),
                          "present" if done else "install", 0, comfy["licence"], comfy["repo"]))
    if not skip_models:
        for model in manifest["models"]:
            if model_kind(model) not in kinds:
                continue
            dest = model_path(root, model)
            if dest.is_file():
                steps.append(Step("model", f"{model['folder']}/{model['name']}", str(dest), "verify", 0, model["licence"], model["url"]))
            else:
                steps.append(Step("model", f"{model['folder']}/{model['name']}", str(dest), "download", model["size"], model["licence"], model["url"]))
    return steps


def describe(steps):
    lines = []
    for step in steps:
        size = f"{step.size / 1e9:.2f} GB" if step.size else ""
        lines.append(f"  {step.state:9} {step.what}  {size}".rstrip())
        if step.state != "present":
            lines.append(f"            -> {step.dest}")
            if step.licence:
                lines.append(f"            licence: {step.licence}")
    total = sum(step.size for step in steps)
    todo = [s for s in steps if s.state != "present"]
    if not todo:
        lines.append("everything is installed")
    else:
        lines.append(f"to download: {total / 1e9:.2f} GB" + ("; plus PyTorch and the requirements from PyPI (a few GB)" if any(s.state == "install" for s in steps) else ""))
    return "\n".join(lines)


# --- downloads ----------------------------------------------------------------------------------------------------------


def sha256_of(path, on_bytes=None):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while chunk := file.read(CHUNK):
            digest.update(chunk)
            if on_bytes:
                on_bytes(len(chunk))
    return digest.hexdigest()


class _DropAuthOnRedirect(urllib.request.HTTPRedirectHandler):
    """Hugging Face answers a file with a redirect to its CDN: the token goes to huggingface.co only, never to another host."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new is not None and urllib.parse.urlparse(newurl).hostname != urllib.parse.urlparse(req.full_url).hostname:
            new.remove_header("Authorization")
        return new


_AUTH_OPENER = urllib.request.build_opener(_DropAuthOnRedirect)


def download(url, dest, size, sha256, on_progress=None, clock=time.monotonic, cancel=None, token=None):
    """`url` into `dest` through `dest.part` (resumed by a Range request), checked for size and hash before the rename. A `token` is sent
    to the URL's own host only."""
    dest = Path(dest)
    part = dest.with_name(dest.name + ".part")
    dest.parent.mkdir(parents=True, exist_ok=True)
    have = part.stat().st_size if part.is_file() else 0
    if have < size:
        headers = {"Range": f"bytes={have}-"} if have else {}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        request = urllib.request.Request(url, headers=headers)
        try:
            with (_AUTH_OPENER.open if token else urllib.request.urlopen)(request, timeout=60) as response:
                if have and response.status != 206:
                    have = 0  # the server ignored the range: start over
                last = 0.0
                with open(part, "ab" if have else "wb") as file:
                    while chunk := response.read(CHUNK):
                        _check_cancel(cancel)
                        file.write(chunk)
                        have += len(chunk)
                        if on_progress and clock() - last >= PROGRESS_EVERY_S:
                            last = clock()
                            on_progress(have, size)
        except urllib.error.HTTPError as error:
            if error.code in (401, 403):
                raise SetupError(f"{url} refused the download (HTTP {error.code}): a gated model needs its licence accepted on Hugging Face "
                                 "and a token in HF_TOKEN or hfToken of tools/artgen/local.json") from error
            raise SetupError(f"the download of {url} stopped (HTTP {error.code}); run again to resume") from error
        except (urllib.error.URLError, ConnectionError, TimeoutError) as error:
            raise SetupError(f"the download of {url} stopped ({getattr(error, 'reason', error)}); run again to resume") from error
    if on_progress:
        on_progress(have, size)
    actual = part.stat().st_size
    if actual != size:
        raise SetupError(f"{part} has {actual} bytes, the manifest says {size}; delete it and run again")
    actual_sha = sha256_of(part)
    if actual_sha != sha256:
        raise SetupError(f"{part} has SHA256 {actual_sha}, the manifest says {sha256}; delete it and run again")
    os.replace(part, dest)


# --- the install --------------------------------------------------------------------------------------------------------


def find_7z():
    found = shutil.which("7z") or shutil.which("7z.exe")
    program_files = os.environ.get("ProgramFiles")
    if not found and program_files and Path(program_files, "7-Zip", "7z.exe").is_file():
        found = str(Path(program_files, "7-Zip", "7z.exe"))
    return found


def unpack_7z(archive, target):
    Path(target).mkdir(parents=True, exist_ok=True)
    seven = find_7z()
    command = [seven, "x", "-y", f"-o{target}", str(archive)] if seven else ["tar", "-xf", str(archive), "-C", str(target)]
    if subprocess.run(command, stdout=subprocess.DEVNULL, check=False).returncode != 0:
        raise SetupError(f"could not unpack {archive} (install 7-Zip and run again)")


def run_checked(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, check=False)
    if result.returncode != 0:
        raise SetupError(f"{' '.join(str(c) for c in command)} failed ({result.returncode}); run again to continue")


def venv_python(root):
    venv = Path(root) / "venv"
    return venv / "Scripts" / "python.exe" if os.name == "nt" else venv / "bin" / "python"


class Installer:
    """Carries out a plan. `unpack`, `run` and `git_head` are the outside world; tests replace them."""

    def __init__(self, manifest, key, root, downloads=None, emit=None, unpack=unpack_7z, run=run_checked, git_head=None, python=sys.executable,
                 cancel=None, clock=time.monotonic, token=None, hash_of_gated=gated_sha256):
        self.manifest = manifest
        self.key = key
        self.root = Path(root)
        self.downloads = Path(downloads) if downloads else Path(str(self.root) + ".downloads")
        self.emit = emit or (lambda event: None)
        self.unpack = unpack
        self.run = run
        self.git_head = git_head or (lambda repo: subprocess.run(["git", "-C", str(repo), "rev-parse", "HEAD"], capture_output=True, text=True, check=False).stdout.strip())
        self.python = python
        self.cancel = cancel  # a threading.Event: set, the install stops between chunks and steps
        self.clock = clock
        self.token = token  # for a gated model (hf_token)
        self.hash_of_gated = hash_of_gated

    def install(self, steps, keep_downloads=False):
        for index, step in enumerate(steps):
            _check_cancel(self.cancel)
            self.emit({"event": "step", "index": index, "what": step.what, "state": step.state})
            if step.kind == "comfyui":
                if step.state == "download":
                    self._portable(index, keep_downloads)
                elif step.state == "install":
                    self._source()
            elif step.state == "verify":
                self._verify_model(index, step)
            elif step.state == "download":
                model = self._model(step)
                download(model["url"], step.dest, model["size"], self._expected_sha(model, step.dest), self._progress(index), cancel=self.cancel,
                         token=self.token if model.get("gated") else None)
            self.emit({"event": "step_done", "index": index, "what": step.what})
        var = variant(self.manifest, self.key)
        if var["kind"] == "portable" and START_BAT.is_file() and installed_version(self.root):
            shutil.copyfile(START_BAT, self.root / START_BAT.name)
        remember_root(self.root)

    def _progress(self, index):
        """At most one progress event per PROGRESS_EVERY_S, and the last one: a hash check reads a megabyte at a time."""
        last = [None]

        def report(done, total):
            now = self.clock()
            if done >= total or last[0] is None or now - last[0] >= PROGRESS_EVERY_S:
                last[0] = now
                self.emit({"event": "progress", "index": index, "done": done, "total": total})

        return report

    def _model(self, step):
        return next(m for m in self.manifest["models"] if str(model_path(self.root, m)) == step.dest)

    def _expected_sha(self, model, dest):
        """The manifest's hash; for a gated model whose hash the manifest cannot hold yet (Hugging Face hides it without access), the
        one Hugging Face shows to the owner's token, printed so it can be pinned."""
        if model.get("sha256"):
            return model["sha256"]
        if not model.get("gated"):
            raise SetupError(f"the manifest has no sha256 of {model['name']}")
        if not self.token:
            raise SetupError(f"{model['name']} is gated: accept its licence at {model['source']}, then set HF_TOKEN (or hfToken in "
                             f"tools/artgen/local.json) and run again; or put the file at {dest} by hand")
        sha = self.hash_of_gated(model, self.token)
        self.emit({"event": "log", "text": f"{model['name']}: SHA256 {sha} (from Hugging Face; pin it in comfyui/manifest.json)"})
        return sha

    def _verify_model(self, index, step):
        model = self._model(step)
        done = [0]

        progress = self._progress(index)

        def on_bytes(n):
            _check_cancel(self.cancel)
            done[0] += n
            progress(done[0], model["size"])

        actual = sha256_of(step.dest, on_bytes)
        if not model.get("sha256") and model.get("gated") and not self.token:
            # A file the owner placed by hand, with no hash to compare yet: its size is checked, its hash said
            if Path(step.dest).stat().st_size != model["size"]:
                raise SetupError(f"{step.dest} has {Path(step.dest).stat().st_size} bytes, the manifest says {model['size']}: not the file of {model['source']}")
            self.emit({"event": "log", "text": f"{model['name']}: placed by hand, SHA256 {actual} (not checked: the manifest pins none yet)"})
            return
        expected = self._expected_sha(model, step.dest)
        if actual != expected:
            raise SetupError(f"{step.dest} has SHA256 {actual}, the manifest says {expected}: it is not this tool's file; move it away and run again")

    def _portable(self, index, keep_downloads):
        var = variant(self.manifest, self.key)
        archive = self.downloads / var["asset"]
        if not archive.is_file():
            download(var["url"], archive, var["size"], var["sha256"], self._progress(index), cancel=self.cancel)
        staging = Path(str(self.root) + ".unpack")
        if staging.exists():
            shutil.rmtree(staging)  # this tool's own staging folder, left by an interrupted unpack
        self.emit({"event": "log", "text": f"unpacking {archive}"})
        self.unpack(archive, staging)
        inner = next((d for d in staging.iterdir() if (d / "ComfyUI" / "main.py").is_file()), None)
        if inner is None:
            raise SetupError(f"{archive} holds no ComfyUI portable folder")
        if self.root.exists():
            (self.root / MARKER).unlink(missing_ok=True)
            self.root.rmdir()  # checked empty by the plan
        shutil.move(str(inner), str(self.root))
        shutil.rmtree(staging)
        self._write_marker()
        if not keep_downloads:
            archive.unlink()
            if self.downloads.is_dir() and not any(self.downloads.iterdir()):
                self.downloads.rmdir()

    def _source(self):
        comfy = self.manifest["comfyui"]
        var = variant(self.manifest, self.key)
        repo = self.root / "ComfyUI"
        if installed_version(self.root) is None:
            if sys.version_info < (3, 10):
                raise SetupError("ComfyUI needs Python 3.10 or newer to run this install")
            if not shutil.which("git") and self.run is run_checked:
                raise SetupError("git is not installed (macOS: xcode-select --install)")
            staging = Path(str(self.root) + ".unpack")
            if staging.exists():
                shutil.rmtree(staging)
            self.emit({"event": "log", "text": f"cloning ComfyUI v{comfy['version']}"})
            self.run(["git", "clone", "--depth", "1", "--branch", f"v{comfy['version']}", comfy["repo"], str(staging / "ComfyUI")])
            head = self.git_head(staging / "ComfyUI")
            if head != comfy["commit"]:
                raise SetupError(f"the tag v{comfy['version']} is commit {head}, the manifest pins {comfy['commit']}: the tag moved; ask the owner")
            self.root.mkdir(parents=True, exist_ok=True)
            shutil.move(str(staging / "ComfyUI"), str(repo))
            shutil.rmtree(staging)
        python = venv_python(self.root)
        if not python.exists():
            self.emit({"event": "log", "text": "creating the venv"})
            self.run([self.python, "-m", "venv", str(self.root / "venv")])
        self.emit({"event": "log", "text": "installing PyTorch and the requirements (pip)"})
        self.run([str(python), "-m", "pip", "install", "torch", "torchvision", "torchaudio", *var.get("torch", [])])
        self.run([str(python), "-m", "pip", "install", "-r", str(repo / "requirements.txt")])
        self._write_marker()

    def _write_marker(self):
        marker = {"platform": self.key, "version": self.manifest["comfyui"]["version"], "kind": variant(self.manifest, self.key)["kind"]}
        (self.root / MARKER).write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")


def remember_root(root, settings_path=None):
    """Writes `comfyuiRoot` into local.json, keeping its other keys, so the status and `start` find this install."""
    settings_path = Path(settings_path or comfyui_check.LOCAL_SETTINGS)
    settings = {}
    if settings_path.is_file():
        settings = json.loads(settings_path.read_text(encoding="utf-8"))
    if settings.get("comfyuiRoot") == str(root):
        return
    settings["comfyuiRoot"] = str(root)
    settings_path.write_text(json.dumps(settings, indent=2) + "\n", encoding="utf-8")


def default_root(settings_path=None):
    """`comfyuiRoot` of local.json, else `D:\\ComfyUI` on a Windows PC with a D: drive, else `~/ComfyUI`."""
    settings_path = Path(settings_path or comfyui_check.LOCAL_SETTINGS)
    if settings_path.is_file():
        root = json.loads(settings_path.read_text(encoding="utf-8")).get("comfyuiRoot")
        if root:
            return Path(root)
    if os.name == "nt" and Path("D:/").exists():
        return Path("D:/ComfyUI")
    return Path.home() / "ComfyUI"


# --- start and stop -----------------------------------------------------------------------------------------------------


def launch_command(root, url, python=None, extra=()):
    """How ComfyUI of `root` is started on the host and port of `url` (portable: its embedded Python; else the venv)."""
    root = Path(root)
    parsed = urllib.parse.urlparse(url)
    host, port = parsed.hostname or "127.0.0.1", parsed.port or 8188
    portable = root / "python_embeded" / "python.exe"
    if python is None:
        python = portable if portable.is_file() else venv_python(root)
    command = [str(python), "-s", str(root / "ComfyUI" / "main.py")]
    if portable.is_file():
        command.append("--windows-standalone-build")
    return command + ["--listen", host, "--port", str(port), "--disable-auto-launch", *extra]


def _reachable(url):
    try:
        with urllib.request.urlopen(f"{url}/system_stats", timeout=2) as response:
            return response.status == 200
    except (urllib.error.URLError, ConnectionError, TimeoutError, OSError):
        return False


def _process_command_line(pid):
    if os.name == "nt":
        return _run_text(["powershell", "-NoProfile", "-Command", f"(Get-CimInstance Win32_Process -Filter 'ProcessId={int(pid)}').CommandLine"]).strip()
    return _run_text(["ps", "-p", str(int(pid)), "-o", "command="]).strip()


def _ours(root, pid):
    """True when `pid` still is the ComfyUI of `root` this tool started (a pid file outlives a reboot, pids are reused)."""
    line = _process_command_line(pid)
    return "main.py" in line and str(Path(root).name) in line


def _log_tail(path, lines=4):
    """The last lines of ComfyUI's log, which hold its reason for exiting."""
    try:
        text = Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return "no log"
    return " ".join(line.strip() for line in text.strip().splitlines()[-lines:])


def start(root, url, wait_s=180.0, python=None, extra=(), emit=None, clock=time.monotonic, sleep=time.sleep):
    emit = emit or (lambda event: None)
    root = Path(root)
    if _reachable(url):
        emit({"event": "log", "text": f"ComfyUI already answers at {url}"})
        return None
    if not (root / "ComfyUI" / "main.py").is_file():
        raise SetupError(f"no ComfyUI installed in {root}: run `artgen_setup.py install` first")
    command = launch_command(root, url, python, extra)
    log = open(root / LOG_FILE, "ab")
    kwargs = {"cwd": str(root), "stdout": log, "stderr": subprocess.STDOUT, "stdin": subprocess.DEVNULL}
    if os.name == "nt":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_NO_WINDOW
    else:
        kwargs["start_new_session"] = True
    process = subprocess.Popen(command, **kwargs)
    log.close()
    (root / PID_FILE).write_text(json.dumps({"pid": process.pid, "url": url}) + "\n", encoding="utf-8")
    emit({"event": "log", "text": f"started ComfyUI (pid {process.pid}), log {root / LOG_FILE}"})
    deadline = clock() + wait_s
    while clock() < deadline:
        if process.poll() is not None:
            (root / PID_FILE).unlink(missing_ok=True)
            raise SetupError(f"ComfyUI exited with {process.returncode} while starting: {_log_tail(root / LOG_FILE)} (log {root / LOG_FILE})")
        if _reachable(url):
            emit({"event": "log", "text": f"ComfyUI answers at {url}"})
            return process.pid
        sleep(0.5)
    raise SetupError(f"ComfyUI (pid {process.pid}) does not answer at {url} after {wait_s:.0f} s: see {root / LOG_FILE}")


def stop(root, emit=None, clock=time.monotonic, sleep=time.sleep):
    """Stops the ComfyUI `start` left in `root`; one started by hand is not touched. True when one was stopped."""
    emit = emit or (lambda event: None)
    pid_file = Path(root) / PID_FILE
    try:
        pid = json.loads(pid_file.read_text(encoding="utf-8"))["pid"]
    except (OSError, ValueError, KeyError):
        emit({"event": "log", "text": "no ComfyUI started by artgen_setup (one started by hand is closed by hand)"})
        return False
    if not _ours(root, pid):
        pid_file.unlink(missing_ok=True)
        emit({"event": "log", "text": f"pid {pid} is no longer ComfyUI: nothing to stop"})
        return False
    os.kill(pid, signal.SIGTERM)  # on Windows: TerminateProcess
    deadline = clock() + 15
    while clock() < deadline and _process_command_line(pid):
        sleep(0.3)
    if _process_command_line(pid) and os.name != "nt":
        os.kill(pid, signal.SIGKILL)
    pid_file.unlink(missing_ok=True)
    emit({"event": "log", "text": f"stopped ComfyUI (pid {pid})"})
    return True


class LocalSetup:
    """The setup actions of the editor's panel on this machine (`Plan`, `Install`, `Start`, `Stop`), for the bridge: each reports through
    `emit` (the events of `install --json`) and raises SetupError with the reason. Install takes no consent: the panel showed the plan."""

    def __init__(self, url, root=None, probe=probe_machine, manifest=None, installer=Installer):
        self.url = url
        self.root = Path(root) if root else None
        self.probe = probe
        self.manifest = manifest
        self.installer = installer
        self._checked_pid = None
        self._checked_ours = False

    def started_here(self):
        """True when the running ComfyUI is the one `start` left (its pid file names a live ComfyUI of the root), so `Stop` can end it;
        a ComfyUI started by hand is not. Checked once per pid: the bridge asks every few seconds."""
        root = self._context()[1]
        try:
            pid = json.loads((root / PID_FILE).read_text(encoding="utf-8"))["pid"]
        except (OSError, ValueError, KeyError):
            return False
        if pid != self._checked_pid:
            self._checked_pid = pid
            self._checked_ours = _ours(root, pid)
        return self._checked_ours

    def _context(self):
        manifest = self.manifest or comfyui_check.load_manifest()
        root = self.root or default_root()
        return manifest, root

    def _plan(self, emit):
        manifest, root = self._context()
        key = detect(self.probe())
        steps = make_plan(manifest, key, root)
        emit({"event": "plan", "platform": key, "note": variant(manifest, key).get("note", ""), "root": str(root),
              "steps": [s.to_json() for s in steps], "downloadBytes": sum(s.size for s in steps)})
        return manifest, key, root, steps

    def run(self, action, emit, cancel=None):
        if action == "Plan":
            self._plan(emit)
        elif action == "Install":
            manifest, key, root, steps = self._plan(emit)
            self.installer(manifest, key, root, emit=emit, cancel=cancel).install(steps)
        elif action == "Start":
            start(self._context()[1], self.url, emit=emit)
        elif action == "Stop":
            stop(self._context()[1], emit=emit)
        else:
            raise SetupError(f"unknown setup action {action}")


# --- the command line ---------------------------------------------------------------------------------------------------


def _printer(as_json):
    if as_json:
        return lambda event: print(json.dumps(event), flush=True)

    def human(event):
        kind = event["event"]
        if kind == "step":
            print(f"[{event['index'] + 1}] {event['what']}: {event['state']}", flush=True)
        elif kind == "progress":
            total = event["total"] or 1
            print(f"\r    {event['done'] / 1e6:,.0f} / {total / 1e6:,.0f} MB ({100 * event['done'] / total:.0f}%)", end="", flush=True)
        elif kind == "step_done":
            print("\r    ok" + " " * 40, flush=True)
        elif kind == "log":
            print(event["text"], flush=True)

    return human


def main(argv=None, probe=probe_machine, ask=input):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("detect")
    install = sub.add_parser("install")
    install.add_argument("--root", type=Path)
    install.add_argument("--downloads", type=Path, help="where the release archive goes (default <root>.downloads)")
    install.add_argument("--platform", help="a platform key of the manifest instead of the detected one")
    install.add_argument("--yes", action="store_true", help="do not ask (the editor asked)")
    install.add_argument("--dry-run", action="store_true", help="only print the plan")
    install.add_argument("--skip-models", action="store_true")
    install.add_argument("--audio", action="store_true", help="also the sound effects model (Stable Audio Open: gated, needs HF_TOKEN)")
    install.add_argument("--keep-downloads", action="store_true")
    install.add_argument("--json", action="store_true", help="one JSON event per line")
    for name in ("start", "stop", "status"):
        command = sub.add_parser(name)
        command.add_argument("--root", type=Path)
        command.add_argument("--url")
        command.add_argument("--json", action="store_true")
    sub.choices["start"].add_argument("--wait", type=float, default=180.0)
    sub.choices["start"].add_argument("comfy_args", nargs="*", help="after --: arguments of ComfyUI (--lowvram)")
    args = parser.parse_args(argv)
    emit = _printer(getattr(args, "json", False))
    manifest = comfyui_check.load_manifest()
    try:
        if args.command == "detect":
            found = probe()
            try:
                key = detect(found)
                print(json.dumps({"platform": key, "gpus": found.gpus, "note": variant(manifest, key).get("note", "")}, indent=2))
            except SetupError as error:
                print(json.dumps({"platform": None, "gpus": found.gpus, "reason": str(error)}, indent=2))
                return EXIT_FAILED
            return EXIT_OK
        root = args.root or default_root()
        if args.command == "install":
            key = args.platform or detect(probe())
            kinds = (KIND_IMAGE, KIND_AUDIO) if args.audio else (KIND_IMAGE,)
            steps = make_plan(manifest, key, root, args.skip_models, kinds)
            if args.json:
                emit({"event": "plan", "platform": key, "root": str(root), "steps": [s.to_json() for s in steps],
                      "downloadBytes": sum(s.size for s in steps)})
            else:
                print(f"platform {key}: {variant(manifest, key).get('note', '')}\ninstall into {root}:\n{describe(steps)}")
            if args.dry_run or all(s.state == "present" for s in steps):
                return EXIT_OK
            if not args.yes and any(s.state in ("download", "install") for s in steps):  # a check alone needs no consent
                if args.json:
                    emit({"event": "done", "ok": False, "error": "--json needs --yes: the caller asks the owner"})
                    return EXIT_DECLINED
                if ask("go on? [y/N] ").strip().lower() not in ("y", "yes"):
                    print("nothing changed")
                    return EXIT_DECLINED
            Installer(manifest, key, root, args.downloads, emit, token=hf_token()).install(steps, args.keep_downloads)
            emit({"event": "done", "ok": True, "root": str(root)})
            if not args.json:
                print(f"done. Start: python {Path(__file__).name} start")
            return EXIT_OK
        url = comfyui_check.resolve_url(args.url)
        if args.command == "start":
            start(root, url, args.wait, extra=args.comfy_args, emit=emit)
        elif args.command == "stop":
            stop(root, emit=emit)
        else:
            from artgen_generators import ComfyUIGenerator

            status = ComfyUIGenerator(url, root, manifest).status()
            print(json.dumps({"state": status.state, "reason": status.reason, **status.details}, indent=2) if args.json else f"{status.state} {status.reason}".rstrip())
            return EXIT_OK if status.state == "ready" else EXIT_FAILED
        emit({"event": "done", "ok": True})
        return EXIT_OK
    except SetupError as error:
        emit({"event": "done", "ok": False, "error": str(error)}) if getattr(args, "json", False) else print(f"error: {error}", file=sys.stderr)
        return EXIT_FAILED
    except KeyboardInterrupt:
        print("\ninterrupted: run again to resume", file=sys.stderr)
        return EXIT_FAILED


if __name__ == "__main__":
    sys.exit(main())
