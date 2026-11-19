"""
The generation service (tools/artgen/CLAUDE.md, "The service"): a request of a preset, a subject, a count and a seed becomes
that many images on disk, each with its provenance file. The editor bridge and the MCP tool call `generate()`; they never
build graphs or talk to a generator themselves.
"""

import json
import random
import re
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

import artgen_checks
import artgen_preset
import artgen_reference
from artgen_generators import GeneratorState, flac_info

PROVENANCE_FORMAT = 1
PROVENANCE_SUFFIX = ".artgen.json"
# ComfyUI takes a seed up to 2**64 - 1; 2**32 keeps the numbers short enough to read and type back.
SEED_RANGE = 2**32
MAX_COUNT = 16
# What of a verdict the progress event carries (the metrics and the thresholds are in the provenance file)
EVENT_CHECK_KEYS = ("passed", "failed", "error")
# The editor reads seeds as int64
SEED_LIMIT = 2**63
# How far past the base seeds an extra image looks for a seed whose file is not there yet
MAX_SEED_PROBES = 1000
OUTPUT_SUFFIXES = (".png", ".flac")


class RequestError(Exception):
    """A request the service refuses before it touches the generator."""


@dataclass
class Request:
    preset: object  # artgen_preset.Preset
    subject: str
    count: int = 4
    seed: int | None = None  # None: a random one; image i takes seed + i
    out_dir: Path = Path(".")
    name: str = ""  # the file name stem; the subject's slug by default
    reference: dict | None = None  # {"mode", "image", "weight"?}: replaces the preset's own whole
    game_root: Path | None = None  # what a relative reference image is read from
    # Regeneration: more images on the next seeds while fewer than `min_pass` of them pass their checks. None takes the preset's
    # `regenerate` (artgen_preset.py), else `count` and no extra images.
    min_pass: int | None = None
    extra: int | None = None


@dataclass
class Result:
    path: Path
    provenance_path: Path
    seed: int
    provenance: dict
    extra: bool = False  # made after the `count` images because too few passed


def slug(text, fallback="image"):
    value = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return value[:48] or fallback


def check_request(request):
    if not 1 <= request.count <= MAX_COUNT:
        raise RequestError(f"count must be 1 to {MAX_COUNT}, not {request.count}")
    if request.seed is not None and not 0 <= request.seed < 2**63:
        raise RequestError("seed must be a non-negative integer below 2**63")
    for name, value in (("min_pass", request.min_pass), ("extra", request.extra)):
        if value is not None and type(value) is not int:
            raise RequestError(f"{name} must be an integer, not {value!r}")
    if request.min_pass is not None and not 1 <= request.min_pass <= request.count:
        raise RequestError(f"min_pass must be 1 to the count ({request.count}), not {request.min_pass}")
    if request.extra is not None and not 0 <= request.extra <= artgen_preset.MAX_EXTRA:
        raise RequestError(f"extra must be 0 to {artgen_preset.MAX_EXTRA}, not {request.extra}")
    if request.extra:
        if request.preset.kind != artgen_preset.KIND_IMAGE:
            raise RequestError("extra is for an image: a sound has no checks that could fail")
        if request.preset.checks is None:
            raise RequestError(f"extra needs the checks of the preset, and its \"{artgen_preset.CHECKS_KEY}\" is false")


def regeneration_of(request):
    """(min_pass, extra) a checked request runs with: its own keys, else the preset's `regenerate`, else the count and nothing extra.
    A preset's `minPass` above the count is the count."""
    rule = request.preset.regenerate or {}
    extra = request.extra if request.extra is not None else rule.get("extra", 0)
    min_pass = request.min_pass if request.min_pass is not None else min(rule.get("minPass") or request.count, request.count)
    return min_pass, extra


def is_failing(result):
    """Only a verdict of `passed: false` fails: a picture without alpha has none and one that cannot be read has `null`, and another
    image cannot cure either."""
    return result.provenance.get("checks", {}).get("passed") is False


def count_passing(results):
    return sum(1 for result in results if not is_failing(result))


def free_seed(out_dir, stem, start):
    """The first seed from `start` up whose file `<stem>_<seed>` (picture, sound or provenance) is not in `out_dir`, or None when
    none is free in MAX_SEED_PROBES seeds or before SEED_LIMIT. A file already there is never written over: an earlier run's
    image, or a variant the editor's more-run drew its own seeds for."""
    for seed in range(start, min(start + MAX_SEED_PROBES, SEED_LIMIT)):
        names = [f"{stem}_{seed}{suffix}" for suffix in OUTPUT_SUFFIXES] + [f"{stem}_{seed}{PROVENANCE_SUFFIX}"]
        if not any((Path(out_dir) / name).exists() for name in names):
            return seed
    return None


def prepare_reference(request):
    """The reference of a request as (reference, picture, workflow that runs it), or (None, None, None). Everything that can be wrong
    with it is found here, from the files only, before the generator is asked anything and before anything is written."""
    reference = artgen_reference.reference_of(request.preset, request.reference)
    workflow = request.preset.workflow
    if reference is None:
        if artgen_preset.REFERENCE_IMAGE_INPUT in workflow.inputs:
            raise RequestError(f"the workflow '{workflow.name}' needs a reference picture and the request has none")
        return None, None, None
    try:
        resolved = artgen_reference.resolve_image(request.game_root, reference["image"])
    except artgen_reference.ReferenceImageError as error:
        raise RequestError(str(error)) from error
    return reference, resolved, artgen_reference.effective_workflow(workflow, reference["mode"], request.game_root)


def provenance_of(request, seed, graph, generator_info, source_name, now=None, data=b"", reference=None):
    """`reference` is the ReferenceUse of the run: the workflow that ran is then the mapped one, so the record names it and a
    variant can be made again alone."""
    preset = request.preset
    workflow = reference.workflow if reference else preset.workflow
    record = {
        "provenance": PROVENANCE_FORMAT,
        "preset": preset.name,
        "presetSource": preset.source,
        "presetHash": preset.file_hash,
        "presetData": preset.data,
        "kind": preset.kind,
        "subject": request.subject,
        "prompt": artgen_preset.final_prompt(preset, request.subject),
        "negative": preset.negative,
        "seed": seed,
        "size": [preset.width, preset.height] if preset.width else None,
        "params": preset.params,
        "models": artgen_preset.models_of(graph),
        "workflow": workflow.name,
        "workflowHash": workflow.hash,
        "generator": generator_info,
        "generatorFile": source_name,
        "date": (now or datetime.now(timezone.utc)).isoformat(timespec="seconds"),
    }
    if reference:
        record["reference"] = {
            "mode": reference.mode,
            "image": reference.image,
            "hash": reference.file_hash,
            "weight": reference.weight,
            "workflow": workflow.name,
        }
    if preset.kind == artgen_preset.KIND_AUDIO:
        # The asked length and what the file holds (FLAC: read from its STREAMINFO)
        record["duration"] = preset.duration
        info = flac_info(data)
        record["sampleRate"], record["channels"], record["seconds"] = info if info else (None, None, None)
    elif preset.checks is not None:
        # No alpha channel (an icon) gives no verdict and no key; a picture that cannot be read is a verdict with an error, never a failed run
        verdict = artgen_checks.check_png(data, preset.checks)
        if verdict is not None:
            record["checks"] = verdict
    return record


def generate(request, generator, on_progress=None, cancel=None, rng=random):
    """Runs the images one by one and writes each as soon as it is there, so a cancel keeps what was done.

    `on_progress(event)` gets dicts: {"kind": "step", "image": i, "count": n, "step": s, "steps": m, "seed": seed} while an
    image (or a sound) runs and {"kind": "image", "image": i, "count": n, "seed": seed, "path": str, "output": "image" | "audio"} when it
    is written, with "checks": {"passed", "failed"} (or "error") when the variant has a verdict. After the `count` images, while
    fewer than `min_pass` of them are not failing and fewer than `extra` extra images were made, one more is made on the next free
    seed (artgen_service.free_seed); its events say "extra": true and a `count` of the images planned so far. Short at the limit is
    not an error: the verdicts are in the provenance. Returns the results; `Cancelled` and `GenerationError` of the generator pass
    through after the images done so far are written.
    """
    check_request(request)
    reference, resolved, workflow = prepare_reference(request)
    status = generator.status()
    if status.state != GeneratorState.READY:
        raise RequestError(f"the generator is {status.state}: {status.reason}")
    kinds = status.details.get("kinds")
    if kinds is not None and request.preset.kind not in kinds:
        raise RequestError(f"the generator makes no {request.preset.kind}: its models are missing (artgen_setup.py install --{request.preset.kind})")
    if reference and kinds is not None:
        needed = artgen_reference.MODES[reference["mode"]].kind
        if needed is not None and needed not in kinds:
            raise RequestError(
                f"the generator cannot run the reference mode '{reference['mode']}': its models or custom node are missing "
                f"(python tools/artgen/artgen_setup.py install --{needed})"
            )
    first_seed = request.seed if request.seed is not None else rng.randrange(SEED_RANGE)
    use = None
    if reference:
        # Once per request, whatever the count: the name is the picture's hash, so the same picture is one file in the input folder
        uploaded = generator.upload_image(resolved.data, resolved.upload_name)
        use = artgen_reference.ReferenceUse(
            reference["mode"], reference["weight"], uploaded, workflow, reference["image"], resolved.sha256
        )
    out_dir = Path(request.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = request.name or slug(request.subject)
    info = generator.info()
    results = []

    def make_image(seed, count, extra):
        """`count` is what the events say the run holds so far: the request's for a base image, the images planned up to this one
        for an extra, so the editor's label never reads past its total."""
        index = len(results)
        flag = {"extra": True} if extra else {}
        graph = artgen_preset.build_graph(request.preset, request.subject, seed, file_prefix=f"artgen_{stem}", reference=use)

        def step(value, steps):
            if on_progress:
                on_progress({"kind": "step", "image": index, "count": count, "step": value, "steps": steps, "seed": seed, **flag})

        images = generator.run(graph, on_progress=step, cancel=cancel)
        image = images[0]
        default_suffix = ".flac" if request.preset.kind == artgen_preset.KIND_AUDIO else ".png"
        suffix = Path(image.name).suffix.lower() or default_suffix
        path = out_dir / f"{stem}_{seed}{suffix}"
        path.write_bytes(image.data)
        provenance = provenance_of(request, seed, graph, info, image.name, data=image.data, reference=use)
        provenance_path = path.with_name(path.stem + PROVENANCE_SUFFIX)
        provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
        results.append(Result(path, provenance_path, seed, provenance, extra))
        if on_progress:
            event = {"kind": "image", "image": index, "count": count, "seed": seed, "path": str(path), "output": request.preset.kind, **flag}
            if "checks" in provenance:
                event["checks"] = {key: provenance["checks"][key] for key in EVENT_CHECK_KEYS if key in provenance["checks"]}
            on_progress(event)

    for index in range(request.count):
        make_image(first_seed + index, request.count, False)
    min_pass, extra_limit = regeneration_of(request)
    next_seed = first_seed + request.count
    # One image at a time, so the run ends at the first one that makes enough pass
    for _ in range(extra_limit):
        if count_passing(results) >= min_pass:
            break
        seed = free_seed(out_dir, stem, next_seed)
        if seed is None:
            break
        make_image(seed, len(results) + 1, True)
        next_seed = seed + 1
    return results
