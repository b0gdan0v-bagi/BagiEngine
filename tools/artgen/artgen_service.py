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

import artgen_preset
from artgen_generators import GeneratorState, flac_info

PROVENANCE_FORMAT = 1
PROVENANCE_SUFFIX = ".artgen.json"
# ComfyUI takes a seed up to 2**64 - 1; 2**32 keeps the numbers short enough to read and type back.
SEED_RANGE = 2**32
MAX_COUNT = 16


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


@dataclass
class Result:
    path: Path
    provenance_path: Path
    seed: int
    provenance: dict


def slug(text, fallback="image"):
    value = re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")
    return value[:48] or fallback


def check_request(request):
    if not 1 <= request.count <= MAX_COUNT:
        raise RequestError(f"count must be 1 to {MAX_COUNT}, not {request.count}")
    if request.seed is not None and not 0 <= request.seed < 2**63:
        raise RequestError("seed must be a non-negative integer below 2**63")


def provenance_of(request, seed, graph, generator_info, source_name, now=None, data=b""):
    preset = request.preset
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
        "workflow": preset.workflow.name,
        "workflowHash": preset.workflow.hash,
        "generator": generator_info,
        "generatorFile": source_name,
        "date": (now or datetime.now(timezone.utc)).isoformat(timespec="seconds"),
    }
    if preset.kind == artgen_preset.KIND_AUDIO:
        # The asked length and what the file holds (FLAC: read from its STREAMINFO)
        record["duration"] = preset.duration
        info = flac_info(data)
        record["sampleRate"], record["channels"], record["seconds"] = info if info else (None, None, None)
    return record


def generate(request, generator, on_progress=None, cancel=None, rng=random):
    """Runs the images one by one and writes each as soon as it is there, so a cancel keeps what was done.

    `on_progress(event)` gets dicts: {"kind": "step", "image": i, "count": n, "step": s, "steps": m, "seed": seed} while an
    image (or a sound) runs and {"kind": "image", "image": i, "count": n, "seed": seed, "path": str, "output": "image" | "audio"} when it
    is written. Returns the
    results; `Cancelled` and `GenerationError` of the generator pass through after the images done so far are written.
    """
    check_request(request)
    status = generator.status()
    if status.state != GeneratorState.READY:
        raise RequestError(f"the generator is {status.state}: {status.reason}")
    kinds = status.details.get("kinds")
    if kinds is not None and request.preset.kind not in kinds:
        raise RequestError(f"the generator makes no {request.preset.kind}: its models are missing (artgen_setup.py install --{request.preset.kind})")
    first_seed = request.seed if request.seed is not None else rng.randrange(SEED_RANGE)
    out_dir = Path(request.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = request.name or slug(request.subject)
    info = generator.info()
    results = []
    for index in range(request.count):
        seed = first_seed + index
        graph = artgen_preset.build_graph(request.preset, request.subject, seed, file_prefix=f"artgen_{stem}")

        def step(value, steps, index=index, seed=seed):
            if on_progress:
                on_progress({"kind": "step", "image": index, "count": request.count, "step": value, "steps": steps, "seed": seed})

        images = generator.run(graph, on_progress=step, cancel=cancel)
        image = images[0]
        default_suffix = ".flac" if request.preset.kind == artgen_preset.KIND_AUDIO else ".png"
        suffix = Path(image.name).suffix.lower() or default_suffix
        path = out_dir / f"{stem}_{seed}{suffix}"
        path.write_bytes(image.data)
        provenance = provenance_of(request, seed, graph, info, image.name, data=image.data)
        provenance_path = path.with_name(path.stem + PROVENANCE_SUFFIX)
        provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
        results.append(Result(path, provenance_path, seed, provenance))
        if on_progress:
            on_progress({"kind": "image", "image": index, "count": request.count, "seed": seed, "path": str(path), "output": request.preset.kind})
    return results
