"""
The reference picture of a run (tools/artgen/CLAUDE.md, "Presets"): what a `reference` says, which workflow runs it and how its
weight reaches the graph. `reference` is {"mode": "init", "image": "<path>", "weight": 0.4}; a mode is a key of `MODES`, and a
workflow's `.inputs.json` maps each mode it can run to the workflow that runs it (`"reference": {"init": "sdxl_img2img"}`), so a
preset names the plain workflow and never knows which variant serves the reference.

This module and `artgen_preset` use each other only inside functions, so either can be imported first.
"""

import hashlib
from dataclasses import dataclass
from pathlib import Path

import artgen_preset

REFERENCE_KEYS = ("mode", "image", "weight")
MAX_REFERENCE_BYTES = 16 * 1024 * 1024
# The first bytes that make a file a picture ComfyUI's LoadImage reads, by what it is and not by its name: (offset, bytes, extension)
IMAGE_MAGIC = (
    (0, b"\x89PNG\r\n\x1a\n", ".png"),
    (0, b"\xff\xd8\xff", ".jpg"),
)
WEBP_RIFF = b"RIFF"
WEBP_TAG = b"WEBP"
WEBP_TAG_OFFSET = 8
UPLOAD_NAME_PREFIX = "artgen_ref_"
UPLOAD_HASH_CHARS = 16


@dataclass(frozen=True)
class Mode:
    weight_input: str  # the workflow input that takes the weight
    default_weight: float
    weight_range: tuple  # the open interval a weight lies in
    to_input: object  # weight -> the value of weight_input


# `init` starts from the picture instead of from noise: the sampler's denoise is the part of the picture that is redrawn.
MODES = {
    "init": Mode(weight_input="denoise", default_weight=0.4, weight_range=(0.0, 1.0), to_input=lambda weight: 1.0 - weight),
}


@dataclass
class ReferenceUse:
    """What `build_graph` needs of a reference that is resolved: its mode and weight, the name the generator got the picture
    under, and the workflow that runs it."""

    mode: str
    weight: float
    image_name: str
    workflow: object
    image: str = ""  # the path as the request asked it, for the provenance
    file_hash: str = ""  # sha256 of the picture's bytes


class ReferenceImageError(Exception):
    """A reference picture that cannot be used; the message names the path and the reason."""


@dataclass(frozen=True)
class ResolvedImage:
    data: bytes
    sha256: str
    extension: str

    @property
    def upload_name(self):
        """Named by content, so one picture is one file in ComfyUI's input folder however often it is used."""
        return f"{UPLOAD_NAME_PREFIX}{self.sha256[:UPLOAD_HASH_CHARS]}{self.extension}"


def _is_number(value):
    return type(value) in (int, float)


def parse_reference(data, label):
    """Checks the shape of a `reference` and returns {"mode", "image", "weight"} with the default weight filled in. `label`
    names the owner (a preset file, the request) in every error; the image itself is `resolve_image`'s."""
    if not isinstance(data, dict):
        raise artgen_preset.PresetError(f"{label}: \"reference\" must be an object {{\"mode\", \"image\", \"weight\"}}")
    for key in data:
        if key not in REFERENCE_KEYS:
            raise artgen_preset.PresetError(f"{label}: \"reference\" has no key '{key}' (it has: {', '.join(REFERENCE_KEYS)})")
    mode = data.get("mode")
    if not isinstance(mode, str) or mode not in MODES:
        raise artgen_preset.PresetError(f"{label}: \"reference.mode\" must be one of {', '.join(MODES)}")
    image = data.get("image")
    if not isinstance(image, str) or not image:
        raise artgen_preset.PresetError(f"{label}: \"reference.image\" must be the path of a picture")
    weight = data.get("weight", MODES[mode].default_weight)
    low, high = MODES[mode].weight_range
    if not _is_number(weight) or not low < weight < high:
        raise artgen_preset.PresetError(f"{label}: \"reference.weight\" of the mode '{mode}' must be a number above {low} and below {high}")
    return {"mode": mode, "image": image, "weight": float(weight)}


def reference_of(preset, request_reference):
    """The reference of a run: the request's replaces the preset's whole (no key-by-key merge); None when neither has one. A
    sound has none."""
    if request_reference is not None:
        reference = parse_reference(request_reference, "the request")
    else:
        reference = preset.reference
    if reference is not None and preset.kind != artgen_preset.KIND_IMAGE:
        raise artgen_preset.PresetError(f"the preset '{preset.name}' makes a sound, which has no reference")
    return reference


def _image_extension(head):
    for offset, magic, extension in IMAGE_MAGIC:
        if head[offset:offset + len(magic)] == magic:
            return extension
    if head[:len(WEBP_RIFF)] == WEBP_RIFF and head[WEBP_TAG_OFFSET:WEBP_TAG_OFFSET + len(WEBP_TAG)] == WEBP_TAG:
        return ".webp"
    return None


def _image_path(game_root, image):
    """The file `image` names. A relative path is read from the game root and may never leave it: a request of the control API
    chooses it, and what it names is sent to another program. The `..` segments are refused by name, a link out of the root and
    a drive-relative path by where they end up."""
    candidate = Path(image)
    if candidate.is_absolute():
        return candidate
    if not game_root:
        raise ReferenceImageError(f"the reference image '{image}' is relative and there is no game root to read it from")
    if candidate.drive or candidate.root or "\\" in image or "\0" in image or ".." in image.split("/"):
        raise ReferenceImageError(f"the reference image '{image}' must be a '/'-separated path under the game root, without '..', or an absolute path")
    root = Path(game_root).resolve()
    path = (root / candidate).resolve()
    if not path.is_relative_to(root):
        raise ReferenceImageError(f"the reference image '{image}' is outside the game root")
    return path


def resolve_image(game_root, image):
    """Reads the picture of a reference: its bytes, their sha256 and the extension its first bytes give (PNG, JPEG or WebP, whatever the
    name says). A refusal names the path and the reason (a ReferenceImageError) and reads no more than it needs to say it."""
    path = _image_path(game_root, image)
    try:
        if not path.is_file():
            raise ReferenceImageError(f"the reference image '{image}' is not a file")
        size = path.stat().st_size
        if size > MAX_REFERENCE_BYTES:
            raise ReferenceImageError(f"the reference image '{image}' is {size} bytes; the limit is {MAX_REFERENCE_BYTES}")
        with path.open("rb") as stream:
            data = stream.read(MAX_REFERENCE_BYTES + 1)
    except OSError as error:
        raise ReferenceImageError(f"the reference image '{image}' cannot be read: {error}") from error
    if len(data) > MAX_REFERENCE_BYTES:
        raise ReferenceImageError(f"the reference image '{image}' is over {MAX_REFERENCE_BYTES} bytes")
    extension = _image_extension(data)
    if extension is None:
        raise ReferenceImageError(f"the reference image '{image}' is not a PNG, JPEG or WebP picture")
    return ResolvedImage(data, hashlib.sha256(data).hexdigest(), extension)


def modes_of(workflow):
    """The reference modes a workflow can run: the mapped ones, and its own when it takes the picture itself."""
    modes = list(workflow.reference)
    if artgen_preset.REFERENCE_IMAGE_INPUT in workflow.inputs:
        modes += [m for m, mode in MODES.items() if mode.weight_input in workflow.inputs and m not in modes]
    return modes


def effective_workflow(workflow, mode, game_root=None):
    """The workflow that runs `mode` for a preset that names `workflow`: the mapped one, else the workflow itself when it takes
    the picture and the mode's weight, else a PresetError naming the modes it has."""
    if mode in workflow.reference:
        return artgen_preset.load_workflow(workflow.reference[mode], game_root)
    if mode in modes_of(workflow):
        return workflow
    has = ", ".join(modes_of(workflow)) or "none"
    raise artgen_preset.PresetError(f"the workflow '{workflow.name}' cannot run the reference mode '{mode}' (its modes: {has})")


def weight_value(mode, weight):
    """The value of the mode's weight input for a weight."""
    return MODES[mode].to_input(weight)
