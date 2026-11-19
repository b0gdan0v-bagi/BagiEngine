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
    """One output file of a run: an image, or a sound (the name's extension says which)."""

    data: bytes
    name: str  # the generator's own file name, for the log and the extension


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

    def choices(self):
        """The values the generator offers for its combo inputs, {node class: {input name: [values]}} (checkpoints, samplers, ...);
        {} when it cannot say. Asked when it becomes ready, not every poll."""
        return {}

    def upload_image(self, data, name):
        """Hands a picture to the generator (the input of a run that starts from one) and returns the name to put into the graph.
        The caller names the file by its content, so the same picture is one file."""
        raise GenerationError(f"the {self.name} generator cannot take a reference picture")


def combo_choices(object_info):
    """{node class: {input: [values]}} of the combo inputs in ComfyUI's /object_info: `[[values], {...}]`, or the newer
    `["COMBO", {"options": [values]}]`."""
    found = {}
    for node_class, info in (object_info or {}).items():
        inputs = (info or {}).get("input", {}) if isinstance(info, dict) else {}
        for group in ("required", "optional"):
            for name, spec in (inputs.get(group) or {}).items():
                if not isinstance(spec, list) or not spec:
                    continue
                values = None
                if isinstance(spec[0], list):
                    values = spec[0]
                elif spec[0] == "COMBO" and len(spec) > 1 and isinstance(spec[1], dict):
                    values = spec[1].get("options")
                if isinstance(values, list) and all(isinstance(v, (str, int, float)) for v in values):
                    found.setdefault(node_class, {})[name] = values
    return found


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

    def _http(self, path, data=None, raw=False, form=None):
        """A GET, or a POST of `data` as JSON, or of `form` (body, content type) as it is."""
        body, content_type = form if form else (None if data is None else json.dumps(data).encode("utf-8"), "application/json")
        request = urllib.request.Request(f"{self.url}{path}", data=body, headers={"Content-Type": content_type} if body else {})
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
        details = {"url": self.url, "version": report.get("version"), "warnings": report["warnings"], "kinds": report.get("kinds", [])}
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

    def choices(self):
        try:
            return combo_choices(self._http("/object_info"))
        except (GenerationError, ValueError):
            return {}

    def upload_image(self, data, name):
        if not name or any(c in name for c in '"\r\n\\/'):
            raise GenerationError(f"ComfyUI: '{name}' is not a file name for an upload")
        boundary = uuid.uuid4().hex
        parts = [
            (f'Content-Disposition: form-data; name="image"; filename="{name}"\r\nContent-Type: application/octet-stream', data),
            ('Content-Disposition: form-data; name="type"', b"input"),
            ('Content-Disposition: form-data; name="overwrite"', b"true"),
        ]
        body = b"".join(f"--{boundary}\r\n{head}\r\n\r\n".encode("utf-8") + payload + b"\r\n" for head, payload in parts)
        body += f"--{boundary}--\r\n".encode("utf-8")
        answer = self._http("/upload/image", form=(body, f"multipart/form-data; boundary={boundary}"))
        if not isinstance(answer, dict) or not isinstance(answer.get("name"), str):
            raise GenerationError(f"ComfyUI answered the upload of '{name}' without a name: {json.dumps(answer)[:200]}")
        return f"{answer['subfolder']}/{answer['name']}" if answer.get("subfolder") else answer["name"]

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
            # SaveImage reports "images", SaveAudio "audio"; both are fetched by /view
            for image in output.get("images", []) + output.get("audio", []):
                if image.get("type") != "output":
                    continue
                query = urllib.parse.urlencode({k: image.get(k, "") for k in ("filename", "subfolder", "type")})
                images.append(Image(self._http(f"/view?{query}", raw=True), image["filename"]))
        if not images:
            raise GenerationError("ComfyUI finished without an output file (does the workflow end in SaveImage or SaveAudio?)")
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


FLAC_RATE = 44100
FLAC_BLOCK = 4096
FAKE_MAX_SECONDS = 0.5


def _crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def _crc16(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _utf8_number(value):
    """FLAC's frame number coding (UTF-8 extended to 36 bits); a fake sound has few frames."""
    if value < 0x80:
        return bytes([value])
    if value < 0x800:
        return bytes([0xC0 | (value >> 6), 0x80 | (value & 0x3F)])
    return bytes([0xE0 | (value >> 12), 0x80 | ((value >> 6) & 0x3F), 0x80 | (value & 0x3F)])


def tiny_flac(seconds, frequency=440.0, rate=FLAC_RATE):
    """A stereo 16-bit FLAC of a tone (the fake generator's sounds): verbatim subframes, real CRCs, so any decoder reads it."""
    import math

    total = max(1, int(rate * seconds))
    samples = [int(0.3 * 32767 * math.sin(2 * math.pi * frequency * i / rate)) for i in range(total)]
    info = struct.pack(">HH", FLAC_BLOCK, FLAC_BLOCK) + bytes(6)  # the frame sizes: unknown
    # 20 bits rate, 3 bits channels - 1, 5 bits bits - 1, 36 bits total samples
    packed = (rate << 44) | (1 << 41) | (15 << 36) | total
    info += packed.to_bytes(8, "big") + bytes(16)
    out = bytearray(b"fLaC" + bytes([0x80]) + len(info).to_bytes(3, "big") + info)
    for number, start in enumerate(range(0, total, FLAC_BLOCK)):
        block = samples[start:start + FLAC_BLOCK]
        # sync, fixed block size; block size from the 16-bit field, rate and depth from STREAMINFO; independent stereo
        header = bytearray([0xFF, 0xF8, 0x70, 0x10]) + _utf8_number(number) + struct.pack(">H", len(block) - 1)
        header.append(_crc8(header))
        frame = bytearray(header)
        for divisor in (1, 2):  # the right channel is the left at half level: in phase, so a mix to mono keeps it
            frame.append(0x02)  # verbatim subframe, no wasted bits
            frame += b"".join(struct.pack(">h", s // divisor) for s in block)
        frame += struct.pack(">H", _crc16(frame))
        out += frame
    return bytes(out)


def flac_info(data):
    """(sample rate, channels, seconds) of a FLAC's STREAMINFO, or None when the bytes are not FLAC."""
    if len(data) < 42 or data[:4] != b"fLaC" or data[4] & 0x7F != 0:
        return None
    packed = int.from_bytes(data[18:26], "big")
    rate = packed >> 44
    channels = ((packed >> 41) & 0x7) + 1
    total = packed & ((1 << 36) - 1)
    return rate, channels, (total / rate if rate else 0.0)


class FakeGenerator(Generator):
    """Records the graphs and answers each with a small PNG whose colour follows the seed, or a short FLAC tone for a graph that saves
    audio; `state`, `fail` and `kinds` steer it."""

    name = "fake"

    def __init__(self, steps=3, state=GeneratorState.READY, fail=None):
        self.steps = steps
        self.state = state
        self.fail = fail
        self.kinds = ["audio", "image", "structure", "style"]  # what status() says the models make; a test removes one
        self.graphs = []
        self.uploads = []  # (name, data) of every upload_image

    def status(self):
        return GeneratorStatus(self.state, "" if self.state == GeneratorState.READY else f"fake: {self.state}", {"kinds": list(self.kinds)})

    def info(self):
        return {"name": self.name, "version": "1"}

    def choices(self):
        return {
            "CheckpointLoaderSimple": {"ckpt_name": ["sd_xl_base_1.0.safetensors", "fake_model.safetensors"]},
            "VAELoader": {"vae_name": ["sdxl.vae.safetensors"]},
            "KSampler": {"sampler_name": ["euler", "dpmpp_2m"], "scheduler": ["normal", "karras"]},
            "LoadBackgroundRemovalModel": {"bg_removal_name": ["birefnet.safetensors"]},
        }

    def upload_image(self, data, name):
        self.uploads.append((name, data))
        return name

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
        if any(str(n.get("class_type", "")).startswith("SaveAudio") for n in graph.values()):
            seconds = next((n["inputs"]["seconds"] for n in graph.values() if n.get("class_type") == "EmptyLatentAudio"), 1.0)
            return [Image(tiny_flac(min(float(seconds), FAKE_MAX_SECONDS), 220.0 + seed % 440), f"fake_{seed}.flac")]
        rgb = (seed % 256, (seed // 256) % 256, 128)
        return [Image(tiny_png(8, 8, rgb), f"fake_{seed}.png")]


def make_generator(kind="comfyui", url=None):
    if kind == "fake":
        return FakeGenerator()
    if kind == "comfyui":
        return ComfyUIGenerator(comfyui_check.resolve_url(url), default_install_root())
    raise ValueError(f"unknown generator '{kind}' (comfyui, fake)")
