"""
Style presets and workflows (tools/artgen/CLAUDE.md, "Presets"): load and check them, and build the graph of one image.

A preset is a JSON file of the game, `<game root>/artgen/presets/<name>.json`. It names a workflow, which is looked up in
`<game root>/artgen/workflows/` first and in the engine's `tools/artgen/workflows/` next: `<name>.json` is the graph in
ComfyUI's API format, `<name>.inputs.json` names its inputs (`"prompt": ["6", "text"]`: node id and input). The preset gives
values only by those names, so a preset never knows node ids and one workflow serves many presets.
"""

import copy
import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path

ENGINE_WORKFLOWS = Path(__file__).resolve().parent / "workflows"
PRESET_FORMAT = 1
SUBJECT_TOKEN = "{subject}"
# Named inputs the service fills itself; a preset sets the rest through "params".
SERVICE_INPUTS = ("prompt", "negative", "seed", "width", "height")
REQUIRED_INPUTS = ("prompt", "seed")
# The output node is named too: the service sets its file prefix and reads its images.
OUTPUT_INPUT = "output"
# Inputs whose values are model files: they go into the provenance.
MODEL_INPUT_NAMES = ("ckpt_name", "vae_name", "lora_name", "unet_name", "clip_name", "control_net_name")


class PresetError(Exception):
    """A preset or workflow that cannot be used; the message names the file."""


@dataclass
class Workflow:
    name: str
    path: Path
    graph: dict
    inputs: dict  # named input -> [node id, input name]

    @property
    def hash(self):
        """Of the template and its input names: two images with the same hash ran the same graph apart from the values."""
        text = json.dumps({"graph": self.graph, "inputs": self.inputs}, sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(text.encode("utf-8")).hexdigest()


@dataclass
class Preset:
    name: str
    path: Path
    title: str
    workflow: Workflow
    prompt: str
    negative: str = ""
    width: int | None = None
    height: int | None = None
    params: dict = field(default_factory=dict)
    file_hash: str = ""


def _read_json(path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PresetError(f"{path}: {error}") from error


def load_workflow(name, game_root=None):
    folders = ([Path(game_root) / "artgen" / "workflows"] if game_root else []) + [ENGINE_WORKFLOWS]
    for folder in folders:
        path = folder / f"{name}.json"
        if path.is_file():
            break
    else:
        raise PresetError(f"no workflow '{name}' in {', '.join(str(f) for f in folders)}")
    graph = _read_json(path)
    inputs_path = folder / f"{name}.inputs.json"
    if not inputs_path.is_file():
        raise PresetError(f"{path}: the workflow has no {inputs_path.name} naming its inputs")
    inputs = _read_json(inputs_path)
    for key, target in inputs.items():
        if key == OUTPUT_INPUT:
            if target not in graph:
                raise PresetError(f"{inputs_path}: the output node '{target}' is not in the graph")
            continue
        if not (isinstance(target, list) and len(target) == 2 and target[0] in graph):
            raise PresetError(f"{inputs_path}: '{key}' must be [node id, input name] of a node of the graph")
        if target[1] not in graph[target[0]].get("inputs", {}):
            raise PresetError(f"{inputs_path}: node {target[0]} has no input '{target[1]}' for '{key}'")
    for key in REQUIRED_INPUTS + (OUTPUT_INPUT,):
        if key not in inputs:
            raise PresetError(f"{inputs_path}: the input '{key}' is not named")
    return Workflow(name=name, path=path, graph=graph, inputs=inputs)


def load_preset(path, game_root=None):
    path = Path(path)
    data = _read_json(path)
    if data.get("preset") != PRESET_FORMAT:
        raise PresetError(f"{path}: \"preset\" must be {PRESET_FORMAT} (the format version)")
    for key in ("workflow", "prompt"):
        if not isinstance(data.get(key), str) or not data[key]:
            raise PresetError(f"{path}: \"{key}\" is required")
    try:
        workflow = load_workflow(data["workflow"], game_root)
    except PresetError as error:
        raise PresetError(f"{path}: {error}") from error
    size = data.get("size")
    if size is not None and not (isinstance(size, list) and len(size) == 2 and all(isinstance(v, int) and v > 0 for v in size)):
        raise PresetError(f"{path}: \"size\" must be [width, height]")
    if size is not None and not ("width" in workflow.inputs and "height" in workflow.inputs):
        raise PresetError(f"{path}: the workflow '{workflow.name}' has no width and height inputs for \"size\"")
    if data.get("negative") and "negative" not in workflow.inputs:
        raise PresetError(f"{path}: the workflow '{workflow.name}' has no negative input")
    params = data.get("params", {})
    for key in params:
        if key in SERVICE_INPUTS or key == OUTPUT_INPUT:
            raise PresetError(f"{path}: \"{key}\" is set by the service or by \"size\", not in \"params\"")
        if key not in workflow.inputs:
            known = ", ".join(k for k in workflow.inputs if k not in SERVICE_INPUTS and k != OUTPUT_INPUT)
            raise PresetError(f"{path}: the workflow '{workflow.name}' has no input '{key}' (it has: {known})")
    return Preset(
        name=path.stem,
        path=path,
        title=data.get("title", path.stem),
        workflow=workflow,
        prompt=data["prompt"],
        negative=data.get("negative", ""),
        width=size[0] if size else None,
        height=size[1] if size else None,
        params=dict(params),
        file_hash=hashlib.sha256(path.read_bytes()).hexdigest(),
    )


def find_presets(game_root):
    """Every preset of the game, by name; a broken one is reported as its error, not skipped silently."""
    folder = Path(game_root) / "artgen" / "presets"
    found = {}
    for path in sorted(folder.glob("*.json")) if folder.is_dir() else []:
        try:
            found[path.stem] = load_preset(path, game_root)
        except PresetError as error:
            found[path.stem] = error
    return found


def final_prompt(preset, subject):
    if SUBJECT_TOKEN in preset.prompt:
        return preset.prompt.replace(SUBJECT_TOKEN, subject)
    return f"{subject}, {preset.prompt}" if subject else preset.prompt


def build_graph(preset, subject, seed, file_prefix="artgen"):
    """The graph of one image: a copy of the workflow with the preset's and the request's values in its named inputs."""
    graph = copy.deepcopy(preset.workflow.graph)
    values = dict(preset.params)
    values["prompt"] = final_prompt(preset, subject)
    values["seed"] = seed
    if preset.negative:
        values["negative"] = preset.negative
    if preset.width is not None:
        values["width"], values["height"] = preset.width, preset.height
    for key, value in values.items():
        node, name = preset.workflow.inputs[key]
        graph[node]["inputs"][name] = value
    output = graph[preset.workflow.inputs[OUTPUT_INPUT]]
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
