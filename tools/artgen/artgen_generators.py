"""
The generators behind the service (tools/artgen/CLAUDE.md, "Generators"): one interface, ComfyUI as the first adapter, a fake
one for tests. A generator runs one finished graph and gives back its images; presets, seeds and files are the service's.
"""

import json
import os
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zlib
from dataclasses import dataclass, field
from pathlib import Path

import artgen_setup
import comfyui_check
from artgen_ws import OP_TEXT, WebSocketClient, WebSocketClosed

REQUEST_TIMEOUT_S = 30.0
WS_POLL_S = 0.5
DEFAULT_RUN_TIMEOUT_S = 600.0
LOCAL_HOSTS = ("127.0.0.1", "localhost", "::1")


class GeneratorState:
    READY = "ready"
    NOT_RUNNING = "not_running"
    NOT_INSTALLED = "not_installed"


@dataclass
class GeneratorStatus:
    state: str
    reason: str = ""
    details: dict = field(default_factory=dict)

    def to_json(self):
        return {"state": self.state, "reason": self.reason, "details": self.details}


@dataclass
class Image:
    data: bytes
    name: str  # the generator's own file name, for the log


class GenerationError(Exception):
    pass


class Cancelled(Exception):
    pass


class Generator:
    """`run` blocks until the images are there; `on_progress(step, steps)` is called from the same thread, `cancel()` of the
    `cancel` object (a threading.Event or anything with `is_set()`) stops it with `Cancelled`."""

    name = "generator"

    def status(self):
        raise NotImplementedError

    def info(self):
        """What goes into the provenance about the generator (name, version, address)."""
        return {"name": self.name}

    def run(self, graph, on_progress=None, cancel=None, timeout_s=DEFAULT_RUN_TIMEOUT_S):
        raise NotImplementedError


def default_install_root(settings_path=comfyui_check.LOCAL_SETTINGS):
    """Where a local ComfyUI is installed (`artgen_setup.default_root`)."""
    return artgen_setup.default_root(settings_path)


class ComfyUIGenerator(Generator):
    name = "comfyui"

    def __init__(self, url, install_root=None, manifest=None):
        self.url = url.rstrip("/")
        self.install_root = Path(install_root) if install_root else None
        self.manifest = manifest or comfyui_check.load_manifest()
        self._version = None

    def _http(self, path, data=None, raw=False):
        body = None if data is None else json.dumps(data).encode("utf-8")
        request = urllib.request.Request(f"{self.url}{path}", data=body, headers={"Content-Type": "application/json"} if body else {})
        try:
            with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_S) as response:
                payload = response.read()
        except urllib.error.HTTPError as error:
            raise GenerationError(f"ComfyUI refused {path}: HTTP {error.code}: {error.read().decode('utf-8', 'replace')[:800]}") from error
        except (urllib.error.URLError, ConnectionError, TimeoutError) as error:
            raise GenerationError(f"ComfyUI at {self.url} does not answer: {getattr(error, 'reason', error)}") from error
        return payload if raw else json.loads(payload.decode("utf-8") or "null")

    def status(self):
        report = comfyui_check.check(self.url, self.manifest)
        details = {"url": self.url, "version": report.get("version"), "warnings": report["warnings"]}
        if report["reachable"]:
            self._version = report.get("version")
            if report["ready"]:
                return GeneratorStatus(GeneratorState.READY, "", details)
            return GeneratorStatus(GeneratorState.NOT_INSTALLED, "; ".join(report["problems"]), details)
        host = urllib.parse.urlparse(self.url).hostname
        if host not in LOCAL_HOSTS:
            return GeneratorStatus(GeneratorState.NOT_RUNNING, f"ComfyUI at {self.url} does not answer", details)
        root = self.install_root
        if root is not None and (root / "ComfyUI" / "main.py").is_file():
            details["installRoot"] = str(root)
            return GeneratorStatus(GeneratorState.NOT_RUNNING, f"ComfyUI is installed in {root} but not started (artgen_setup.py start)", details)
        where = f" in {root}" if root else ""
        return GeneratorStatus(GeneratorState.NOT_INSTALLED, f"no ComfyUI installed{where} (artgen_setup.py install)", details)

    def info(self):
        if self._version is None:
            try:
                self._version = self._http("/system_stats").get("system", {}).get("comfyui_version")
            except GenerationError:
                pass
        return {"name": self.name, "version": self._version, "url": self.url}

    def _open_progress(self, client_id):
        parsed = urllib.parse.urlparse(self.url)
        try:
            return WebSocketClient(parsed.hostname, parsed.port or 80, f"/ws?clientId={client_id}")
        except (OSError, WebSocketClosed):
            return None  # progress is a nicety: the run still finishes through /history

    def run(self, graph, on_progress=None, cancel=None, timeout_s=DEFAULT_RUN_TIMEOUT_S, clock=time.monotonic):
        client_id = uuid.uuid4().hex
        ws = self._open_progress(client_id)
        try:
            answer = self._http("/prompt", {"prompt": graph, "client_id": client_id})
            if answer.get("node_errors"):
                raise GenerationError(f"ComfyUI refused the graph: {json.dumps(answer['node_errors'])[:800]}")
            prompt_id = answer["prompt_id"]
            try:
                return self._wait(prompt_id, ws, on_progress, cancel, timeout_s, clock)
            except KeyboardInterrupt:
                self._cancel(prompt_id)
                raise
        finally:
            if ws is not None:
                ws.close()

    def _wait(self, prompt_id, ws, on_progress, cancel, timeout_s, clock):
        started = clock()
        while True:
            if cancel is not None and cancel.is_set():
                self._cancel(prompt_id)
                raise Cancelled()
            if clock() - started > timeout_s:
                self._cancel(prompt_id)
                raise GenerationError(f"no image after {timeout_s:.0f} s")
            if ws is None:
                entry = self._http(f"/history/{prompt_id}").get(prompt_id)
                if entry:
                    status = entry.get("status", {})
                    if status.get("status_str") == "error":
                        raise GenerationError(f"ComfyUI failed: {json.dumps(status.get('messages', []))[:800]}")
                    if status.get("completed"):
                        return self._images(prompt_id)
                time.sleep(WS_POLL_S)
                continue
            try:
                message = ws.recv(WS_POLL_S)
            except (OSError, WebSocketClosed):
                ws = None  # the stream broke: finish by polling /history
                continue
            if message is None or message[0] != OP_TEXT:
                continue  # binary frames are previews
            event = json.loads(message[1].decode("utf-8"))
            data = event.get("data") or {}
            if data.get("prompt_id") not in (None, prompt_id):
                continue
            kind = event.get("type")
            if kind == "progress" and on_progress:
                on_progress(data.get("value", 0), data.get("max", 0))
            elif kind == "execution_error":
                raise GenerationError(f"ComfyUI failed: {data.get('exception_message', data)}")
            elif kind == "execution_interrupted":
                raise Cancelled()
            elif kind in ("executing", "execution_success") and data.get("prompt_id") == prompt_id and data.get("node") is None:
                return self._images(prompt_id)

    def _images(self, prompt_id):
        entry = self._http(f"/history/{prompt_id}").get(prompt_id) or {}
        images = []
        for output in entry.get("outputs", {}).values():
            for image in output.get("images", []):
                if image.get("type") != "output":
                    continue
                query = urllib.parse.urlencode({k: image.get(k, "") for k in ("filename", "subfolder", "type")})
                images.append(Image(self._http(f"/view?{query}", raw=True), image["filename"]))
        if not images:
            raise GenerationError("ComfyUI finished without an output image (does the workflow end in SaveImage?)")
        return images

    def _cancel(self, prompt_id):
        # A queued prompt is deleted; a running one is interrupted (/interrupt names it, so another client's run stays).
        for path, data in (("/queue", {"delete": [prompt_id]}), ("/interrupt", {"prompt_id": prompt_id})):
            try:
                self._http(path, data, raw=True)
            except GenerationError:
                pass


def tiny_png(width, height, rgb):
    """A plain colour PNG (the fake generator's images): valid for any reader, a few hundred bytes."""
    row = b"\x00" + bytes(rgb) * width
    raw = zlib.compress(row * height)

    def chunk(kind, data):
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack("!IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", raw) + chunk(b"IEND", b"")


class FakeGenerator(Generator):
    """Records the graphs and answers each with a small PNG whose colour follows the seed; `state` and `fail` steer it."""

    name = "fake"

    def __init__(self, steps=3, state=GeneratorState.READY, fail=None):
        self.steps = steps
        self.state = state
        self.fail = fail
        self.graphs = []

    def status(self):
        return GeneratorStatus(self.state, "" if self.state == GeneratorState.READY else f"fake: {self.state}")

    def info(self):
        return {"name": self.name, "version": "1"}

    def run(self, graph, on_progress=None, cancel=None, timeout_s=DEFAULT_RUN_TIMEOUT_S):
        self.graphs.append(graph)
        for step in range(1, self.steps + 1):
            if cancel is not None and cancel.is_set():
                raise Cancelled()
            if on_progress:
                on_progress(step, self.steps)
        if self.fail:
            raise GenerationError(self.fail)
        seed = next((n["inputs"]["seed"] for n in graph.values() if "seed" in n.get("inputs", {})), 0)
        rgb = (seed % 256, (seed // 256) % 256, 128)
        return [Image(tiny_png(8, 8, rgb), f"fake_{seed}.png")]


def make_generator(kind="comfyui", url=None):
    if kind == "fake":
        return FakeGenerator()
    if kind == "comfyui":
        return ComfyUIGenerator(comfyui_check.resolve_url(url), default_install_root())
    raise ValueError(f"unknown generator '{kind}' (comfyui, fake)")
