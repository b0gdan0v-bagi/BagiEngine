"""
The reference picture of a run (tools/artgen/CLAUDE.md, "Presets"): what a `reference` says, which workflow runs it and how its
weight reaches the graph. `reference` is {"mode": "init", "image": "<path>", "weight": 0.4}; a mode is a key of `MODES`, and a
workflow's `.inputs.json` maps each mode it can run to the workflow that runs it (`"reference": {"init": "sdxl_img2img"}`), so a
preset names the plain workflow and never knows which variant serves the reference.

This module and `artgen_preset` use each other only inside functions, so either can be imported first.
"""

from dataclasses import dataclass

import artgen_preset

REFERENCE_KEYS = ("mode", "image", "weight")


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
