"""
Style presets and workflows (tools/artgen/CLAUDE.md, "Presets"): load and check them, describe a workflow's inputs for the editor's
form, and build the graph of one image.

A preset is a JSON file of the game, `<game root>/artgen/presets/<name>.json`, or one of the engine's starters in `tools/artgen/presets/`
(the game's of the same name wins), or a draft the editor sends inline: all three go through `parse_preset`. It names a workflow, which
is looked up in `<game root>/artgen/workflows/` first and in the engine's `tools/artgen/workflows/` next: `<name>.json` is the graph in
ComfyUI's API format, `<name>.inputs.json` names its inputs (`"prompt": ["6", "text"]`: node id and input). The preset gives values only
by those names, so a preset never knows node ids and one workflow serves many presets.
"""

import copy
import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

import artgen_checks
import artgen_reference

ENGINE_WORKFLOWS = Path(__file__).resolve().parent / "workflows"
ENGINE_PRESETS = Path(__file__).resolve().parent / "presets"
PRESET_FORMAT = 1
SUBJECT_TOKEN = "{subject}"
# The reference picture of a run: a preset's key, a workflow's map of modes to the workflows that run them (artgen_reference.py), and
# the input of such a workflow that takes the picture
REFERENCE_KEY = "reference"
REFERENCE_IMAGE_INPUT = "reference_image"
# Named inputs the service fills itself; a preset sets the rest through "params".
SERVICE_INPUTS = ("prompt", "negative", "seed", "width", "height", REFERENCE_IMAGE_INPUT)
REQUIRED_INPUTS = ("prompt", "seed")
# The output node is named too: the service sets its file prefix and reads its images (or sounds).
OUTPUT_INPUT = "output"
# What the workflow makes; a reserved key of `.inputs.json` like "output". A preset of another kind cannot use it.
KIND_KEY = "kind"
KIND_IMAGE = "image"
KIND_AUDIO = "audio"
KINDS = (KIND_IMAGE, KIND_AUDIO)
RESERVED_KEYS = (OUTPUT_INPUT, KIND_KEY, REFERENCE_KEY)
# A preset's key: false, or the thresholds of artgen_checks.DEFAULTS to change
CHECKS_KEY = "checks"
# A preset's key: how many extra images the service may make when too few pass their checks (artgen_service.py), at most MAX_EXTRA
REGENERATE_KEY = "regenerate"
REGENERATE_NAMES = ("extra", "minPass")
MAX_EXTRA = 16
# The length of a sound in seconds: an input of an audio workflow, refused for an image
DURATION_INPUT = "duration"
# Inputs whose values are model files: they go into the provenance.
MODEL_INPUT_NAMES = ("ckpt_name", "vae_name", "lora_name", "unet_name", "clip_name", "control_net_name", "bg_removal_name")
# What a preset is called on disk and in a request: a safe file name on every platform
PRESET_NAME = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
DRAFT = "draft"
SOURCE_GAME = "game"
SOURCE_ENGINE = "engine"


class PresetError(Exception):
    """A preset or workflow that cannot be used; the message names the file (or the draft)."""


@dataclass
class Workflow:
    name: str
    path: Path
    graph: dict
    inputs: dict  # named input -> [node id, input name], or a list of them when one value goes to several nodes
    source: str = SOURCE_ENGINE
    kind: str = KIND_IMAGE
    reference: dict = field(default_factory=dict)  # reference mode -> the workflow that runs it

    def targets(self, key):
        """Every [node id, input name] the named input sets."""
        target = self.inputs[key]
        return target if isinstance(target[0], list) else [target]

    @property
    def hash(self):
        """Of the template and its input names: two images with the same hash ran the same graph apart from the values."""
        text = json.dumps({"graph": self.graph, "inputs": self.inputs}, sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(text.encode("utf-8")).hexdigest()

    def describe(self, choices=None):
        """What the editor's form needs: every named input but the output, with the graph's own value as its default, and the values
        the generator offers for a combo input (`choices`: {node class: {input name: [values]}}, from ComfyUI's /object_info)."""
        inputs = {}
        for key in self.inputs:
            if key in RESERVED_KEYS:
                continue
            target = self.targets(key)[0]
            node = self.graph[target[0]]
            entry = {"default": node["inputs"][target[1]]}
            offered = (choices or {}).get(node.get("class_type"), {}).get(target[1])
            if offered:
                entry["choices"] = list(offered)
            inputs[key] = entry
        return {"name": self.name, "source": self.source, "kind": self.kind, "inputs": inputs}


@dataclass
class Preset:
    name: str
    path: Path | None  # None for a draft
    title: str
    workflow: Workflow
    prompt: str
    negative: str = ""
    width: int | None = None
    height: int | None = None
    params: dict = field(default_factory=dict)
    file_hash: str = ""
    source: str = SOURCE_GAME  # game, engine or draft
    data: dict = field(default_factory=dict)  # the preset as written, for the form and the provenance
    kind: str = KIND_IMAGE  # the workflow's: what the preset makes
    reference: dict | None = None  # the checked `reference` of the file (artgen_reference.parse_reference), None when it has none
    checks: dict | None = None  # the thresholds of the cut-out checks (defaults merged with the file's), None when off or for a sound
    regenerate: dict | None = None  # {"minPass": int | None, "extra": int} of the file, None when it has none

    @property
    def duration(self):
        """The seconds of a sound: the preset's `duration`, else the workflow's own; None for an image."""
        if self.kind != KIND_AUDIO or DURATION_INPUT not in self.workflow.inputs:
            return None
        if DURATION_INPUT in self.params:
            return self.params[DURATION_INPUT]
        node, name = self.workflow.targets(DURATION_INPUT)[0]
        return self.workflow.graph[node]["inputs"][name]


def _read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PresetError(f"{path}: {error}") from error


def _folders(game_root, kind, engine_folder):
    return ([(Path(game_root) / "artgen" / kind, SOURCE_GAME)] if game_root else []) + [(engine_folder, SOURCE_ENGINE)]


def _check_reference_map(base, inputs_path, game_root):
    """Every workflow the map names must load, make the same kind, take the picture and the mode's weight, and keep every named
    input of `base`, so a preset's `params` work with or without a reference."""
    for mode, name in base.reference.items():
        where = f"{inputs_path}: the workflow '{name}' for the reference mode '{mode}'"
        try:
            mapped = load_workflow(name, game_root, mapped=True)
        except PresetError as error:
            raise PresetError(f"{inputs_path}: the reference mode '{mode}': {error}") from error
        if mapped.kind != base.kind:
            raise PresetError(f"{where} makes {mapped.kind}, not {base.kind}")
        if mapped.reference:
            raise PresetError(f"{where} has a reference map of its own")
        weight_input = artgen_reference.MODES[mode].weight_input
        for needed in (REFERENCE_IMAGE_INPUT, weight_input):
            if needed not in mapped.inputs:
                raise PresetError(f"{where} does not name the input '{needed}'")
        for key in base.inputs:
            if key not in RESERVED_KEYS and key not in mapped.inputs:
                raise PresetError(f"{where} does not name the input '{key}' of '{base.name}'")


def load_workflow(name, game_root=None, mapped=False):
    """`mapped` loads a workflow a reference map names: it is not checked for a map of its own (the caller refuses one)."""
    folders = _folders(game_root, "workflows", ENGINE_WORKFLOWS)
    for folder, source in folders:
        path = folder / f"{name}.json"
        if path.is_file():
            break
    else:
        raise PresetError(f"no workflow '{name}' in {', '.join(str(f) for f, _ in folders)}")
    graph = _read_json(path)
    inputs_path = folder / f"{name}.inputs.json"
    if not inputs_path.is_file():
        raise PresetError(f"{path}: the workflow has no {inputs_path.name} naming its inputs")
    inputs = _read_json(inputs_path)
    if not isinstance(graph, dict) or not isinstance(inputs, dict):
        raise PresetError(f"{path}: the graph and its inputs must be JSON objects")
    kind = inputs.get(KIND_KEY, KIND_IMAGE)
    if kind not in KINDS:
        raise PresetError(f"{inputs_path}: \"kind\" must be one of {', '.join(KINDS)}")
    reference = inputs.get(REFERENCE_KEY, {})
    if not isinstance(reference, dict) or not all(m in artgen_reference.MODES and isinstance(n, str) and n for m, n in reference.items()):
        raise PresetError(f"{inputs_path}: \"{REFERENCE_KEY}\" must map reference modes ({', '.join(artgen_reference.MODES)}) to workflow names")
    for key, target in inputs.items():
        if key in (KIND_KEY, REFERENCE_KEY):
            continue
        if key == OUTPUT_INPUT:
            if target not in graph:
                raise PresetError(f"{inputs_path}: the output node '{target}' is not in the graph")
            continue
        targets = target if isinstance(target, list) and target and isinstance(target[0], list) else [target]
        for one in targets:
            if not (isinstance(one, list) and len(one) == 2 and one[0] in graph):
                raise PresetError(f"{inputs_path}: '{key}' must be [node id, input name] of a node of the graph, or a list of them")
            if one[1] not in graph[one[0]].get("inputs", {}):
                raise PresetError(f"{inputs_path}: node {one[0]} has no input '{one[1]}' for '{key}'")
    for key in REQUIRED_INPUTS + (OUTPUT_INPUT,):
        if key not in inputs:
            raise PresetError(f"{inputs_path}: the input '{key}' is not named")
    workflow = Workflow(name=name, path=path, graph=graph, inputs=inputs, source=source, kind=kind, reference=dict(reference))
    if not mapped:
        _check_reference_map(workflow, inputs_path, game_root)
    return workflow


def find_workflows(game_root=None):
    """Every workflow the game can use, by name (the game's of a name wins); a broken one is its PresetError."""
    names = {}
    for folder, _ in reversed(_folders(game_root, "workflows", ENGINE_WORKFLOWS)):
        for path in sorted(folder.glob("*.json")) if folder.is_dir() else []:
            if not path.name.endswith(".inputs.json"):
                names[path.stem] = True
    found = {}
    for name in sorted(names):
        try:
            found[name] = load_workflow(name, game_root)
        except PresetError as error:
            found[name] = error
    return found


def content_hash(data):
    """The hash of a preset's content, the same for a draft and for a file that holds the same JSON."""
    return hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()


def _parse_regenerate(value, label):
    where = f"{label}: \"{REGENERATE_KEY}\""
    if not isinstance(value, dict):
        raise PresetError(f"{where} must be an object with \"extra\" and optionally \"minPass\"")
    for name in value:
        if name not in REGENERATE_NAMES:
            raise PresetError(f"{where} has no key '{name}' (it has: {', '.join(REGENERATE_NAMES)})")
    extra = value.get("extra")
    if type(extra) is not int or not 1 <= extra <= MAX_EXTRA:
        raise PresetError(f"{where}: \"extra\" is required and must be an integer from 1 to {MAX_EXTRA}")
    min_pass = value.get("minPass")
    if "minPass" in value and (type(min_pass) is not int or min_pass < 1):
        raise PresetError(f"{where}: \"minPass\" must be an integer 1 or more")
    return {"minPass": min_pass, "extra": extra}


def parse_preset(data, name, game_root=None, label=None, path=None, source=SOURCE_GAME, file_hash=None):
    """Checks a preset's content (from a file or a draft of the editor) and makes the Preset. `label` names it in every error."""
    label = label or str(path or name)
    if not isinstance(data, dict):
        raise PresetError(f"{label}: a preset is a JSON object")
    if data.get("preset") != PRESET_FORMAT:
        raise PresetError(f"{label}: \"preset\" must be {PRESET_FORMAT} (the format version)")
    for key in ("workflow", "prompt"):
        if not isinstance(data.get(key), str) or not data[key]:
            raise PresetError(f"{label}: \"{key}\" is required")
    for key in ("title", "negative"):
        if key in data and not isinstance(data[key], str):
            raise PresetError(f"{label}: \"{key}\" must be text")
    try:
        workflow = load_workflow(data["workflow"], game_root)
    except PresetError as error:
        raise PresetError(f"{label}: {error}") from error
    kind = data.get(KIND_KEY, workflow.kind)
    if kind not in KINDS:
        raise PresetError(f"{label}: \"kind\" must be one of {', '.join(KINDS)}")
    if kind != workflow.kind:
        raise PresetError(f"{label}: an {kind} preset cannot use the workflow '{workflow.name}', which makes {workflow.kind}")
    if kind == KIND_AUDIO and data.get("size") is not None:
        raise PresetError(f"{label}: \"size\" is for an image; a sound has \"params\": {{\"duration\": seconds}}")
    size = data.get("size")
    if size is not None and not (isinstance(size, list) and len(size) == 2 and all(type(v) is int and v > 0 for v in size)):
        raise PresetError(f"{label}: \"size\" must be [width, height]")
    if size is not None and not ("width" in workflow.inputs and "height" in workflow.inputs):
        raise PresetError(f"{label}: the workflow '{workflow.name}' has no width and height inputs for \"size\"")
    if data.get("negative") and "negative" not in workflow.inputs:
        raise PresetError(f"{label}: the workflow '{workflow.name}' has no negative input")
    reference = None
    if REFERENCE_KEY in data:
        if kind != KIND_IMAGE:
            raise PresetError(f"{label}: a sound has no \"{REFERENCE_KEY}\"")
        reference = artgen_reference.parse_reference(data[REFERENCE_KEY], label)
        try:
            artgen_reference.effective_workflow(workflow, reference["mode"], game_root)
        except PresetError as error:
            raise PresetError(f"{label}: {error}") from error
    checks = None
    if kind == KIND_IMAGE and data.get(CHECKS_KEY) is not False:
        try:
            checks = artgen_checks.merge_thresholds(data.get(CHECKS_KEY, {}))
        except ValueError as error:
            raise PresetError(f"{label}: \"{CHECKS_KEY}\" {error}") from error
    elif kind == KIND_AUDIO and CHECKS_KEY in data:
        raise PresetError(f"{label}: a sound has no \"{CHECKS_KEY}\"")
    regenerate = None
    if REGENERATE_KEY in data:
        if kind != KIND_IMAGE:
            raise PresetError(f"{label}: a sound has no \"{REGENERATE_KEY}\" (it has no checks that could fail)")
        if checks is None:
            raise PresetError(f"{label}: \"{REGENERATE_KEY}\" needs the checks, and \"{CHECKS_KEY}\" is false")
        regenerate = _parse_regenerate(data[REGENERATE_KEY], label)
    params = data.get("params", {})
    if not isinstance(params, dict):
        raise PresetError(f"{label}: \"params\" must be an object")
    if kind == KIND_IMAGE and DURATION_INPUT in params:
        raise PresetError(f"{label}: \"duration\" is for a sound, not an image")
    if kind == KIND_AUDIO and DURATION_INPUT in params and not (type(params[DURATION_INPUT]) in (int, float) and params[DURATION_INPUT] > 0):
        raise PresetError(f"{label}: \"duration\" must be a positive number of seconds")
    for key in params:
        if key in SERVICE_INPUTS or key in RESERVED_KEYS:
            raise PresetError(f"{label}: \"{key}\" is set by the service or by \"size\", not in \"params\"")
        if key not in workflow.inputs:
            known = ", ".join(k for k in workflow.inputs if k not in SERVICE_INPUTS and k not in RESERVED_KEYS)
            raise PresetError(f"{label}: the workflow '{workflow.name}' has no input '{key}' (it has: {known})")
    return Preset(
        name=name,
        path=path,
        title=data.get("title", name),
        workflow=workflow,
        prompt=data["prompt"],
        negative=data.get("negative", ""),
        width=size[0] if size else None,
        height=size[1] if size else None,
        params=dict(params),
        file_hash=file_hash or content_hash(data),
        source=source,
        data=data,
        kind=kind,
        reference=reference,
        checks=checks,
        regenerate=regenerate,
    )


def load_preset(path, game_root=None, source=SOURCE_GAME):
    path = Path(path)
    data = _read_json(path)
    return parse_preset(data, path.stem, game_root, path=path, source=source, file_hash=hashlib.sha256(path.read_bytes()).hexdigest())


def preset_path(name, game_root=None):
    """The file of a preset by name, the game's before the engine's, with its source; None when neither has it."""
    if not PRESET_NAME.match(name or ""):
        return None
    for folder, source in _folders(game_root, "presets", ENGINE_PRESETS):
        path = folder / f"{name}.json"
        if path.is_file():
            return path, source
    return None


def find_preset(name, game_root=None):
    found = preset_path(name, game_root)
    if found is None:
        raise PresetError(f"no preset '{name}' in the game's artgen/presets/ or the engine's")
    return load_preset(found[0], game_root, source=found[1])


def find_presets(game_root=None):
    """Every preset the game can use, by name: the game's, then the engine's starters it does not override. A broken one is its
    PresetError (with `.source` and, when the file is JSON, `.data`), not skipped silently."""
    found = {}
    for folder, source in _folders(game_root, "presets", ENGINE_PRESETS):
        for path in sorted(folder.glob("*.json")) if folder.is_dir() else []:
            if path.stem in found or not PRESET_NAME.match(path.stem):
                continue
            try:
                found[path.stem] = load_preset(path, game_root, source=source)
            except PresetError as error:
                error.source = source
                try:
                    error.data = json.loads(path.read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    error.data = None
                found[path.stem] = error
    return found


def final_prompt(preset, subject):
    if SUBJECT_TOKEN in preset.prompt:
        return preset.prompt.replace(SUBJECT_TOKEN, subject)
    return f"{subject}, {preset.prompt}" if subject else preset.prompt


def build_graph(preset, subject, seed, file_prefix="artgen", reference=None):
    """The graph of one image: a copy of the workflow with the preset's and the request's values in its named inputs. With a
    `reference` (artgen_reference.ReferenceUse) it is the graph of the workflow that runs the mode, with the picture and the weight
    set; a workflow that takes a picture is refused without one."""
    workflow = reference.workflow if reference else preset.workflow
    if reference is None and REFERENCE_IMAGE_INPUT in workflow.inputs:
        raise PresetError(f"the workflow '{workflow.name}' needs a reference picture")
    graph = copy.deepcopy(workflow.graph)
    values = dict(preset.params)
    if reference:
        weight_input = artgen_reference.MODES[reference.mode].weight_input
        if weight_input in values:
            raise PresetError(f"\"{weight_input}\" is set by the reference weight, not in \"params\"")
        values[REFERENCE_IMAGE_INPUT] = reference.image_name
        values[weight_input] = artgen_reference.weight_value(reference.mode, reference.weight)
    values["prompt"] = final_prompt(preset, subject)
    values["seed"] = seed
    if preset.negative:
        values["negative"] = preset.negative
    if preset.width is not None:
        values["width"], values["height"] = preset.width, preset.height
    for key, value in values.items():
        for node, name in workflow.targets(key):
            graph[node]["inputs"][name] = value
    output = graph[workflow.inputs[OUTPUT_INPUT]]
    if "filename_prefix" in output.get("inputs", {}):
        output["inputs"]["filename_prefix"] = file_prefix
    return graph


def models_of(graph):
    """The model files a graph loads, as {input name: file}, for the provenance."""
    found = {}
    for node in graph.values():
        for name, value in node.get("inputs", {}).items():
            if name in MODEL_INPUT_NAMES and isinstance(value, str):
                found.setdefault(name, [])
                if value not in found[name]:
                    found[name].append(value)
    return found
