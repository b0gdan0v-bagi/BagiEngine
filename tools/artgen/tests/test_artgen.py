"""The generation service: presets, the service over the fake generator, the ComfyUI adapter against a fake ComfyUI with a
real WebSocket progress stream, the generator states and the command line."""

import base64
import email.parser
import email.policy
import hashlib
import json
import queue
import shutil
import socket
import struct
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen  # noqa: E402
import artgen_checks  # noqa: E402
import artgen_generators  # noqa: E402
import artgen_preset  # noqa: E402
import artgen_reference  # noqa: E402
import artgen_service  # noqa: E402
import comfyui_check  # noqa: E402
from artgen_generators import (  # noqa: E402
    Cancelled, ComfyUIGenerator, FakeGenerator, GenerationError, GeneratorState, Image, tiny_png,
)
from test_artgen_checks import disc, draw, png_of  # noqa: E402

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
PRESET = {
    "preset": 1,
    "title": "Test",
    "workflow": "sdxl_basic",
    "prompt": "a {subject}, carved wood",
    "negative": "blurry",
    "size": [512, 768],
    "params": {"steps": 12, "cfg": 5.5},
}


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "artgen" / "presets").mkdir(parents=True)
    write_preset(root, "wood", PRESET)
    return root


def write_preset(root, name, data):
    path = root / "artgen" / "presets" / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def preset_of(game, name="wood"):
    return artgen_preset.load_preset(game / "artgen" / "presets" / f"{name}.json", game)


# --- presets


def test_preset_loads_with_the_engine_workflow(game):
    preset = preset_of(game)
    assert preset.workflow.name == "sdxl_basic" and preset.workflow.path.parent == artgen_preset.ENGINE_WORKFLOWS
    assert (preset.width, preset.height) == (512, 768)
    assert len(preset.file_hash) == 64


def test_graph_takes_the_values_and_leaves_the_template_alone(game):
    preset = preset_of(game)
    template = json.dumps(preset.workflow.graph, sort_keys=True)
    graph = artgen_preset.build_graph(preset, "knight", 7, file_prefix="artgen_x")
    assert graph["6"]["inputs"]["text"] == "a knight, carved wood"
    assert graph["7"]["inputs"]["text"] == "blurry"
    assert graph["3"]["inputs"]["seed"] == 7 and graph["3"]["inputs"]["steps"] == 12 and graph["3"]["inputs"]["cfg"] == 5.5
    assert (graph["5"]["inputs"]["width"], graph["5"]["inputs"]["height"]) == (512, 768)
    assert graph["9"]["inputs"]["filename_prefix"] == "artgen_x"
    assert json.dumps(preset.workflow.graph, sort_keys=True) == template
    assert artgen_preset.models_of(graph) == {"ckpt_name": ["sd_xl_base_1.0.safetensors"], "vae_name": ["sdxl.vae.safetensors"]}


def test_prompt_without_the_subject_token_starts_with_the_subject(game):
    write_preset(game, "plain", {**PRESET, "prompt": "pixel art"})
    assert artgen_preset.final_prompt(preset_of(game, "plain"), "a cat") == "a cat, pixel art"


@pytest.mark.parametrize("change, message", [
    ({"preset": 2}, "format version"),
    ({"workflow": "nope"}, "no workflow 'nope'"),
    ({"params": {"denoise": 1}}, "no input 'denoise'"),
    ({"params": {"seed": 1}}, "set by the service"),
    ({"size": [512]}, "[width, height]"),
    ({"prompt": ""}, "\"prompt\" is required"),
])
def test_broken_presets_are_refused_by_name(game, change, message):
    path = write_preset(game, "bad", {**PRESET, **change})
    with pytest.raises(artgen_preset.PresetError, match=message.replace("[", r"\[").replace("]", r"\]")) as error:
        artgen_preset.load_preset(path, game)
    assert "bad.json" in str(error.value)


def test_a_game_workflow_wins_and_its_inputs_are_checked(game):
    folder = game / "artgen" / "workflows"
    folder.mkdir()
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.json", folder / "sdxl_basic.json")
    inputs = json.loads((artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.inputs.json").read_text(encoding="utf-8"))
    (folder / "sdxl_basic.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    assert preset_of(game).workflow.path.parent == folder
    inputs["steps"] = ["3", "nothing"]
    (folder / "sdxl_basic.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    with pytest.raises(artgen_preset.PresetError, match="no input 'nothing'"):
        preset_of(game)


def test_find_presets_reports_a_broken_one(game):
    write_preset(game, "zbroken", {**PRESET, "workflow": "nope"})
    found = artgen_preset.find_presets(game)
    assert isinstance(found["wood"], artgen_preset.Preset)
    assert isinstance(found["zbroken"], artgen_preset.PresetError)
    assert set(artgen_preset.find_presets(game / "missing")) == {"icon", "sfx", "sprite"}, "a game without presets gets the engine's starters"
    assert found["wood"].source == "game" and found["sprite"].source == "engine"
    assert found["zbroken"].source == "game" and found["zbroken"].data["workflow"] == "nope"


def test_every_engine_starter_preset_loads():
    for name, preset in artgen_preset.find_presets(None).items():
        assert isinstance(preset, artgen_preset.Preset), f"{name}: {preset}"
        assert preset.source == "engine" and artgen_preset.SUBJECT_TOKEN in preset.prompt


def test_a_game_preset_wins_over_the_engine_starter_of_its_name(game):
    write_preset(game, "sprite", {**PRESET, "title": "Ours"})
    found = artgen_preset.find_presets(game)
    assert found["sprite"].title == "Ours" and found["sprite"].source == "game"
    assert artgen_preset.find_preset("sprite", game).title == "Ours"
    assert artgen_preset.find_preset("icon", game).source == "engine"
    with pytest.raises(artgen_preset.PresetError, match="no preset 'nope'"):
        artgen_preset.find_preset("nope", game)
    with pytest.raises(artgen_preset.PresetError, match="no preset"):
        artgen_preset.find_preset("../wood", game)


def test_a_draft_is_checked_like_a_file_and_hashed_by_content(game):
    draft = artgen_preset.parse_preset(dict(PRESET), artgen_preset.DRAFT, game, label="the draft", source=artgen_preset.DRAFT)
    from_file = preset_of(game)
    assert draft.path is None and draft.source == "draft" and draft.data == PRESET
    assert draft.file_hash == artgen_preset.content_hash(PRESET)
    assert artgen_preset.build_graph(draft, "knight", 7) == artgen_preset.build_graph(from_file, "knight", 7)
    cases = (
        ({**PRESET, "params": {"stepz": 3}}, "has no input 'stepz'"),
        ({**PRESET, "preset": 2}, "format version"),
        ({**PRESET, "params": []}, "must be an object"),
        ({**PRESET, "size": [True, 4]}, "width, height"),
        ([], "JSON object"),
    )
    for broken, message in cases:
        with pytest.raises(artgen_preset.PresetError, match=message) as error:
            artgen_preset.parse_preset(broken, "draft", game, label="the draft")
        assert str(error.value).startswith("the draft:")


def test_a_workflow_describes_its_inputs_with_the_graph_defaults_and_the_choices(game):
    found = artgen_preset.find_workflows(game)
    assert {"sdxl_basic", "sdxl_cutout"} <= set(found)
    choices = {"KSampler": {"sampler_name": ["euler", "dpmpp_2m"]}, "CheckpointLoaderSimple": {"ckpt_name": ["a.safetensors"]}}
    described = found["sdxl_cutout"].describe(choices)
    assert described["name"] == "sdxl_cutout" and described["source"] == "engine"
    inputs = described["inputs"]
    assert "output" not in inputs
    assert inputs["steps"] == {"default": 25} and inputs["cfg"] == {"default": 7.0}
    assert inputs["width"] == {"default": 1024} and inputs["seed"] == {"default": 42}
    assert inputs["sampler"] == {"default": "dpmpp_2m", "choices": ["euler", "dpmpp_2m"]}
    assert inputs["checkpoint"]["choices"] == ["a.safetensors"]
    assert "choices" not in inputs["vae"]


def test_a_game_workflow_is_listed_as_the_games_and_a_broken_one_as_its_error(game):
    folder = game / "artgen" / "workflows"
    folder.mkdir()
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.json", folder / "sdxl_basic.json")
    shutil.copy(artgen_preset.ENGINE_WORKFLOWS / "sdxl_basic.inputs.json", folder / "sdxl_basic.inputs.json")
    (folder / "lonely.json").write_text("{}", encoding="utf-8")
    found = artgen_preset.find_workflows(game)
    assert found["sdxl_basic"].source == "game" and found["sdxl_cutout"].source == "engine"
    assert isinstance(found["lonely"], artgen_preset.PresetError)


def test_combo_choices_reads_both_forms_of_object_info():
    info = {
        "KSampler": {"input": {"required": {
            "sampler_name": [["euler", "ddim"]],
            "steps": ["INT", {"default": 20}],
            "scheduler": ["COMBO", {"options": ["normal", "karras"]}],
        }}},
        "Broken": None,
    }
    assert artgen_generators.combo_choices(info) == {"KSampler": {"sampler_name": ["euler", "ddim"], "scheduler": ["normal", "karras"]}}


# --- the service over the fake generator


def test_generate_writes_images_and_provenance_with_consecutive_seeds(game, tmp_path):
    events = []
    generator = FakeGenerator(steps=2)
    request = artgen_service.Request(preset_of(game), "White Knight!", count=3, seed=100, out_dir=tmp_path / "out")
    results = artgen_service.generate(request, generator, events.append)
    assert [r.seed for r in results] == [100, 101, 102]
    assert [r.path.name for r in results] == ["white_knight_100.png", "white_knight_101.png", "white_knight_102.png"]
    assert all(r.path.read_bytes().startswith(b"\x89PNG") for r in results)
    provenance = json.loads(results[1].provenance_path.read_text(encoding="utf-8"))
    assert provenance["seed"] == 101 and provenance["preset"] == "wood" and provenance["subject"] == "White Knight!"
    assert provenance["prompt"] == "a White Knight!, carved wood"
    assert provenance["workflowHash"] == preset_of(game).workflow.hash
    assert provenance["generator"] == {"name": "fake", "version": "1"}
    assert provenance["models"]["ckpt_name"] == ["sd_xl_base_1.0.safetensors"]
    assert [g["3"]["inputs"]["seed"] for g in generator.graphs] == [100, 101, 102]
    kinds = [(e["kind"], e["image"]) for e in events]
    assert kinds == [("step", 0), ("step", 0), ("image", 0), ("step", 1), ("step", 1), ("image", 1), ("step", 2), ("step", 2), ("image", 2)]


def test_random_seed_when_none_is_given(game, tmp_path):
    class Rng:
        def randrange(self, n):
            return 5

    results = artgen_service.generate(artgen_service.Request(preset_of(game), "x", 2, None, tmp_path), FakeGenerator(), rng=Rng())
    assert [r.seed for r in results] == [5, 6]


@pytest.mark.parametrize("count", [0, artgen_service.MAX_COUNT + 1])
def test_count_out_of_range_is_refused(game, tmp_path, count):
    with pytest.raises(artgen_service.RequestError, match="count"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", count, 1, tmp_path), FakeGenerator())


def test_a_generator_that_is_not_ready_is_refused_before_running(game, tmp_path):
    generator = FakeGenerator(state=GeneratorState.NOT_RUNNING)
    with pytest.raises(artgen_service.RequestError, match="not_running"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path), generator)
    assert generator.graphs == []


def test_cancel_keeps_the_images_already_written(game, tmp_path):
    cancel = threading.Event()

    def progress(event):
        if event["kind"] == "image" and event["image"] == 0:
            cancel.set()

    with pytest.raises(Cancelled):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 3, 1, tmp_path / "o"), FakeGenerator(), progress, cancel)
    assert sorted(p.name for p in (tmp_path / "o").iterdir()) == ["x_1.artgen.json", "x_1.png"]


def test_a_failure_of_the_generator_passes_through(game, tmp_path):
    with pytest.raises(GenerationError, match="boom"):
        artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path), FakeGenerator(fail="boom"))


# --- a reference picture

REFERENCE_PNG = tiny_png(4, 4, (10, 200, 10))
REFERENCE = {"mode": "init", "image": "assets/artgen/rook.png", "weight": 0.25}


@pytest.fixture
def picture(game):
    path = game / "assets" / "artgen" / "rook.png"
    path.parent.mkdir(parents=True)
    path.write_bytes(REFERENCE_PNG)
    return path


def reference_request(game, out, reference=REFERENCE, preset="wood"):
    return artgen_service.Request(preset_of(game, preset), "rook", 2, 40, out, reference=reference, game_root=game)


def test_a_request_with_a_reference_uploads_the_picture_once_and_runs_the_mapped_workflow(game, picture, tmp_path):
    generator = FakeGenerator()
    results = artgen_service.generate(reference_request(game, tmp_path / "o"), generator)
    digest = hashlib.sha256(REFERENCE_PNG).hexdigest()
    name = f"artgen_ref_{digest[:16]}.png"
    assert generator.uploads == [(name, REFERENCE_PNG)], "once for the two images"
    for graph in generator.graphs:
        assert graph["20"]["inputs"]["image"] == name and graph["3"]["inputs"]["denoise"] == 0.75
    provenance = json.loads(results[0].provenance_path.read_text(encoding="utf-8"))
    assert provenance["reference"] == {"mode": "init", "image": "assets/artgen/rook.png", "hash": digest, "weight": 0.25, "workflow": "sdxl_img2img"}
    assert provenance["workflow"] == "sdxl_img2img" and provenance["workflowHash"] == artgen_preset.load_workflow("sdxl_img2img").hash
    assert provenance["models"]["ckpt_name"] == ["sd_xl_base_1.0.safetensors"]


STYLE = {"mode": "style", "image": "assets/artgen/rook.png", "weight": 1.2}


def test_a_style_request_runs_the_style_workflow_and_records_it(game, picture, tmp_path):
    generator = FakeGenerator()
    results = artgen_service.generate(reference_request(game, tmp_path / "o", STYLE), generator)
    name = f"artgen_ref_{hashlib.sha256(REFERENCE_PNG).hexdigest()[:16]}.png"
    for graph in generator.graphs:
        assert graph["20"]["inputs"]["image"] == name and graph["32"]["inputs"]["weight"] == 1.2 and graph["3"]["inputs"]["denoise"] == 1.0
    provenance = results[0].provenance
    assert provenance["reference"]["mode"] == "style" and provenance["reference"]["workflow"] == "sdxl_ipadapter"
    assert provenance["workflow"] == "sdxl_ipadapter"
    assert provenance["models"]["ipadapter_file"] == ["ip-adapter-plus_sdxl_vit-h.safetensors"]
    assert provenance["models"]["clip_name"] == ["CLIP-ViT-H-14-laion2B-s32B-b79K.safetensors"]


def test_a_generator_without_the_style_kind_refuses_a_style_run_and_writes_nothing(game, picture, tmp_path):
    generator = FakeGenerator()
    generator.kinds = ["audio", "image"]
    out = tmp_path / "o"
    with pytest.raises(artgen_service.RequestError, match=r"cannot run the reference mode 'style'.*\(python tools/artgen/artgen_setup.py install --style\)"):
        artgen_service.generate(reference_request(game, out, STYLE), generator)
    assert generator.graphs == [] and generator.uploads == [] and not out.exists()
    assert artgen_service.generate(reference_request(game, out), generator), "an init run does not need the style parts"
    assert artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path / "p"), generator), "nor does a run with no reference"


def test_a_generator_that_does_not_list_kinds_is_not_refused(game, picture, tmp_path):
    class Unlisted(FakeGenerator):
        def status(self):
            status = super().status()
            del status.details["kinds"]
            return status

    assert artgen_service.generate(reference_request(game, tmp_path / "o", STYLE), Unlisted())


def test_a_preset_of_the_style_workflow_serves_style_and_is_refused_for_init(game, picture, tmp_path):
    write_preset(game, "direct", {**PRESET, "workflow": "sdxl_ipadapter"})
    generator = FakeGenerator()
    results = artgen_service.generate(reference_request(game, tmp_path / "o", STYLE, preset="direct"), generator)
    assert results[0].provenance["workflow"] == "sdxl_ipadapter"
    with pytest.raises(artgen_preset.PresetError, match=r"'sdxl_ipadapter' cannot run the reference mode 'init' \(its modes: style\)"):
        artgen_service.generate(reference_request(game, tmp_path / "p", REFERENCE, preset="direct"), generator)


def test_a_run_without_a_reference_has_no_reference_block(game, tmp_path):
    results = artgen_service.generate(artgen_service.Request(preset_of(game), "x", 1, 1, tmp_path), FakeGenerator())
    assert "reference" not in results[0].provenance
    assert results[0].provenance["workflow"] == "sdxl_basic"


def test_the_weight_defaults_and_is_reported_as_used(game, picture, tmp_path):
    results = artgen_service.generate(reference_request(game, tmp_path, {"mode": "init", "image": "assets/artgen/rook.png"}), FakeGenerator())
    assert results[0].provenance["reference"]["weight"] == 0.4


def test_two_requests_of_the_same_picture_upload_under_one_name(game, picture, tmp_path):
    generator = FakeGenerator()
    (game / "assets" / "artgen" / "copy.png").write_bytes(REFERENCE_PNG)
    artgen_service.generate(reference_request(game, tmp_path / "a"), generator)
    artgen_service.generate(reference_request(game, tmp_path / "b", {**REFERENCE, "image": "assets/artgen/copy.png"}), generator)
    assert len(generator.uploads) == 2 and len({name for name, _ in generator.uploads}) == 1


def test_a_preset_with_its_own_reference_runs_with_it_and_a_requests_replaces_it(game, picture, tmp_path):
    write_preset(game, "keeps", {**PRESET, "reference": {"mode": "init", "image": "assets/artgen/rook.png", "weight": 0.7}})
    generator = FakeGenerator()
    own = artgen_service.generate(artgen_service.Request(preset_of(game, "keeps"), "x", 1, 1, tmp_path / "a", game_root=game), generator)
    assert own[0].provenance["reference"]["weight"] == 0.7 and generator.graphs[0]["3"]["inputs"]["denoise"] == pytest.approx(0.3)
    replacing = {"mode": "init", "image": "assets/artgen/rook.png"}
    request = artgen_service.Request(preset_of(game, "keeps"), "x", 1, 1, tmp_path / "b", reference=replacing, game_root=game)
    assert artgen_service.generate(request, generator)[0].provenance["reference"]["weight"] == 0.4, "no key-by-key merge"


def oversized_picture(game):
    path = game / "assets" / "big.png"
    with path.open("wb") as stream:
        stream.write(REFERENCE_PNG)
        stream.truncate(artgen_reference.MAX_REFERENCE_BYTES + 1)
    return "assets/big.png"


@pytest.mark.parametrize("make, error, message", [
    (lambda game: {**REFERENCE, "image": "../x.png"}, artgen_service.RequestError, "'../x.png'"),
    (lambda game: {**REFERENCE, "image": "assets/artgen"}, artgen_service.RequestError, "not a file"),
    (lambda game: {**REFERENCE, "image": "assets/text.png"}, artgen_service.RequestError, "not a PNG, JPEG or WebP"),
    (lambda game: {**REFERENCE, "image": oversized_picture(game)}, artgen_service.RequestError, "the limit is"),
    (lambda game: {**REFERENCE, "mode": "warp"}, artgen_preset.PresetError, "reference.mode"),
    (lambda game: {**REFERENCE, "weight": 0}, artgen_preset.PresetError, "reference.weight"),
    (lambda game: {**REFERENCE, "weight": 1}, artgen_preset.PresetError, "reference.weight"),
    (lambda game: ["init"], artgen_preset.PresetError, "must be an object"),
])
def test_a_bad_reference_is_refused_before_anything_is_touched(game, picture, tmp_path, make, error, message):
    (game / "assets" / "text.png").write_text("not a picture", encoding="utf-8")
    generator = FakeGenerator()
    out = tmp_path / "o"
    with pytest.raises(error, match=message):
        artgen_service.generate(reference_request(game, out, make(game)), generator)
    assert generator.graphs == [] and generator.uploads == [] and not out.exists()


def test_a_workflow_that_cannot_run_the_mode_is_refused_naming_it(game, picture, tmp_path):
    source = artgen_preset.ENGINE_WORKFLOWS
    inputs = json.loads((source / "sdxl_basic.inputs.json").read_text(encoding="utf-8"))
    del inputs["reference"]
    folder = game / "artgen" / "workflows"
    folder.mkdir()
    (folder / "plain.json").write_text((source / "sdxl_basic.json").read_text(encoding="utf-8"), encoding="utf-8")
    (folder / "plain.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    write_preset(game, "plain", {**PRESET, "workflow": "plain"})
    generator = FakeGenerator()
    with pytest.raises(artgen_preset.PresetError, match="workflow 'plain' cannot run the reference mode 'init'"):
        artgen_service.generate(reference_request(game, tmp_path / "o", preset="plain"), generator)
    assert generator.uploads == [] and not (tmp_path / "o").exists()


def test_a_sound_takes_no_reference(game, picture, tmp_path):
    write_preset(game, "knock", AUDIO)
    generator = FakeGenerator()
    with pytest.raises(artgen_preset.PresetError, match="no reference"):
        artgen_service.generate(reference_request(game, tmp_path / "o", preset="knock"), generator)
    assert generator.graphs == [] and generator.uploads == [] and not (tmp_path / "o").exists()


def test_a_reference_workflow_with_no_reference_is_refused_before_anything_is_written(game, tmp_path):
    write_preset(game, "direct", {**PRESET, "workflow": "sdxl_img2img"})
    out = tmp_path / "o"
    with pytest.raises(artgen_service.RequestError, match="needs a reference picture"):
        artgen_service.generate(artgen_service.Request(preset_of(game, "direct"), "x", 1, 1, out, game_root=game), FakeGenerator())
    assert not out.exists()


def test_a_relative_picture_without_a_game_root_is_refused(game, picture, tmp_path):
    request = reference_request(game, tmp_path / "o")
    request.game_root = None
    with pytest.raises(artgen_service.RequestError, match="no game root"):
        artgen_service.generate(request, FakeGenerator())


def test_a_failed_upload_stops_the_run_before_any_image(game, picture, tmp_path):
    class Refusing(FakeGenerator):
        def upload_image(self, data, name):
            raise GenerationError("upload refused")

    generator = Refusing()
    with pytest.raises(GenerationError, match="upload refused"):
        artgen_service.generate(reference_request(game, tmp_path / "o"), generator)
    assert generator.graphs == [] and not (tmp_path / "o").exists()


# --- the ComfyUI adapter against a fake ComfyUI


class FakeComfy:
    """/system_stats, /models, /prompt, /history, /view, /queue, /interrupt and a WebSocket /ws that streams the events of
    each prompt to the client that queued it. `mode`: "ok", "error" (execution_error), "hang" (one step, then nothing until
    a cancel), "refuse" (node_errors); `ws` False answers /ws with 404."""

    def __init__(self):
        self.mode = "ok"
        self.ws = True
        self.models = {}
        for m in comfyui_check.load_manifest()["models"]:
            self.models.setdefault(m["folder"], []).append(m["name"])
        self.prompts = []
        self.cancels = []
        self.streams = {}
        self.history = {}
        self.uploads = []  # (filename, bytes, {field: text}) of each POST /upload/image
        self.upload_refusal = None  # the text of a 400 answer to an upload
        self.upload_answer = None  # replaces the answer of an upload

    def events_for(self, prompt_id):
        progress = [{"type": "progress", "data": {"value": v, "max": 3, "prompt_id": prompt_id}} for v in (1, 2, 3)]
        if self.mode == "error":
            return progress[:1] + [{"type": "execution_error", "data": {"prompt_id": prompt_id, "exception_message": "out of memory"}}]
        if self.mode == "hang":
            return progress[:1]
        return ([{"type": "status", "data": {}}, {"type": "progress", "data": {"value": 9, "max": 9, "prompt_id": "other"}}]
                + progress + [{"type": "executing", "data": {"node": None, "prompt_id": prompt_id}}])

    def queue_prompt(self, body):
        if self.mode == "refuse":
            return {"prompt_id": "p0", "node_errors": {"3": {"errors": ["bad"]}}}
        prompt_id = f"p{len(self.prompts) + 1}"
        self.prompts.append(body["prompt"])
        if self.mode in ("ok",):
            self.history[prompt_id] = {
                "status": {"status_str": "success", "completed": True},
                "outputs": {"9": {"images": [{"filename": f"{prompt_id}.png", "subfolder": "", "type": "output"},
                                             {"filename": "preview.png", "subfolder": "", "type": "temp"}]}},
            }
        stream = self.streams.get(body.get("client_id"))
        if stream is not None:
            for event in self.events_for(prompt_id):
                stream.put(json.dumps(event))
        return {"prompt_id": prompt_id, "number": 0, "node_errors": {}}


@pytest.fixture
def comfy(monkeypatch):
    state = FakeComfy()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _json(self, payload, code=200, raw=None):
            data = raw if raw is not None else json.dumps(payload).encode()
            self.send_response(code)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            path = self.path
            if path.startswith("/ws"):
                return self._websocket()
            if path == "/system_stats":
                return self._json({"system": {"comfyui_version": comfyui_check.load_manifest()["comfyui"]["version"]},
                                   "devices": [{"name": "gpu", "type": "cuda", "vram_total": 1, "vram_free": 1}]})
            if path.startswith("/models/"):
                return self._json(state.models.get(path[len("/models/"):], []))
            if path.startswith("/history/"):
                prompt_id = path[len("/history/"):]
                return self._json({prompt_id: state.history[prompt_id]} if prompt_id in state.history else {})
            if path.startswith("/view?"):
                return self._json(None, raw=tiny_png(4, 4, (1, 2, 3)))
            return self._json({"error": path}, 404)

        def do_POST(self):
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length)
            if self.path == "/upload/image":
                return self._upload(raw)
            body = json.loads(raw) if raw else {}
            if self.path == "/prompt":
                return self._json(state.queue_prompt(body))
            if self.path in ("/queue", "/interrupt"):
                state.cancels.append((self.path, body))
                return self._json(None, raw=b"")
            return self._json({"error": self.path}, 404)

        def _upload(self, raw):
            if state.upload_refusal:
                return self._json(None, 400, raw=state.upload_refusal.encode())
            message = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(
                b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + raw)
            filename, content, fields = None, None, {}
            for part in message.iter_parts():
                if part.get_filename():
                    filename, content = part.get_filename(), part.get_payload(decode=True)
                else:
                    fields[part.get_param("name", header="content-disposition")] = part.get_content()
            state.uploads.append((filename, content, fields))
            return self._json(state.upload_answer or {"name": filename, "subfolder": "", "type": fields.get("type")})

        def _websocket(self):
            if not state.ws:
                return self._json({"error": "no ws"}, 404)
            client_id = self.path.split("clientId=", 1)[1]
            accept = base64.b64encode(hashlib.sha1((self.headers["Sec-WebSocket-Key"] + WS_GUID).encode()).digest()).decode()
            self.send_response(101)
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", accept)
            self.end_headers()
            self.wfile.flush()
            stream = queue.Queue()
            state.streams[client_id] = stream
            self.wfile.write(b"\x82\x02\x00\x00")  # a binary preview frame, ignored by the client
            while True:
                text = stream.get()
                if text is None:
                    break
                data = text.encode()
                header = b"\x81" + (bytes([len(data)]) if len(data) < 126 else b"\x7e" + struct.pack("!H", len(data)))
                try:
                    self.wfile.write(header + data)
                    self.wfile.flush()
                except OSError:
                    break
            self.close_connection = True

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, daemon=True).start()
    monkeypatch.setattr(comfyui_check, "POLL_INTERVAL_S", 0.0)
    state.url = f"http://127.0.0.1:{server.server_address[1]}"
    yield state
    for stream in state.streams.values():
        stream.put(None)
    server.shutdown()
    server.server_close()


def test_comfyui_streams_progress_and_returns_only_output_images(comfy):
    steps = []
    images = ComfyUIGenerator(comfy.url).run({"1": {"inputs": {}}}, on_progress=lambda v, m: steps.append((v, m)), timeout_s=10)
    assert steps == [(1, 3), (2, 3), (3, 3)]
    assert [i.name for i in images] == ["p1.png"] and images[0].data.startswith(b"\x89PNG")


def test_comfyui_without_a_progress_stream_polls_the_history(comfy):
    comfy.ws = False
    images = ComfyUIGenerator(comfy.url).run({"1": {"inputs": {}}}, timeout_s=10)
    assert [i.name for i in images] == ["p1.png"]


def test_comfyui_errors_are_generation_errors(comfy):
    comfy.mode = "error"
    with pytest.raises(GenerationError, match="out of memory"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10)
    comfy.mode = "refuse"
    with pytest.raises(GenerationError, match="refused the graph"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10)


def test_comfyui_cancel_deletes_and_interrupts_its_own_prompt(comfy):
    comfy.mode = "hang"
    cancel = threading.Event()
    with pytest.raises(Cancelled):
        ComfyUIGenerator(comfy.url).run({}, on_progress=lambda v, m: cancel.set(), cancel=cancel, timeout_s=10)
    assert comfy.cancels == [("/queue", {"delete": ["p1"]}), ("/interrupt", {"prompt_id": "p1"})]


def test_comfyui_timeout_cancels_too(comfy):
    comfy.mode = "hang"
    calls = []

    def clock():
        calls.append(1)
        return 0 if len(calls) < 4 else 100

    with pytest.raises(GenerationError, match="no image"):
        ComfyUIGenerator(comfy.url).run({}, timeout_s=10, clock=clock)
    assert [path for path, _ in comfy.cancels] == ["/queue", "/interrupt"]


def test_comfyui_upload_gives_back_the_name_it_stored(comfy):
    assert ComfyUIGenerator(comfy.url).upload_image(b"\x89PNG-bytes", "artgen_ref_0123.png") == "artgen_ref_0123.png"
    comfy.upload_answer = {"name": "x_1.png", "subfolder": "refs", "type": "input"}
    assert ComfyUIGenerator(comfy.url).upload_image(b"data", "x.png") == "refs/x_1.png"


def test_comfyui_upload_is_a_multipart_form_of_the_file_and_its_input_folder(comfy):
    data = bytes(range(256)) + b"\r\n--boundary-like\r\n"
    ComfyUIGenerator(comfy.url).upload_image(data, "artgen_ref_0123.png")
    assert comfy.uploads == [("artgen_ref_0123.png", data, {"type": "input", "overwrite": "true"})]


def test_comfyui_refusing_an_upload_is_a_generation_error_with_its_text(comfy):
    comfy.upload_refusal = "invalid image file"
    with pytest.raises(GenerationError, match="/upload/image: HTTP 400: invalid image file"):
        ComfyUIGenerator(comfy.url).upload_image(b"x", "a.png")
    comfy.upload_refusal = None
    comfy.upload_answer = {"subfolder": ""}
    with pytest.raises(GenerationError, match="without a name"):
        ComfyUIGenerator(comfy.url).upload_image(b"x", "a.png")
    with pytest.raises(GenerationError, match="not a file name"):
        ComfyUIGenerator(comfy.url).upload_image(b"x", 'a"\r\nb.png')
    assert [upload[0] for upload in comfy.uploads] == ["a.png"], "a bad name never reaches the server"


def test_a_generator_without_uploads_says_so_and_the_fake_records_them():
    with pytest.raises(GenerationError, match="the generator generator cannot take a reference picture"):
        artgen_generators.Generator().upload_image(b"x", "a.png")
    fake = FakeGenerator()
    assert fake.upload_image(b"abc", "r.png") == "r.png" and fake.uploads == [("r.png", b"abc")]


def test_status_ready_and_missing_model(comfy):
    generator = ComfyUIGenerator(comfy.url)
    assert generator.status().state == GeneratorState.READY
    assert generator.info()["version"] == comfyui_check.load_manifest()["comfyui"]["version"]
    comfy.models["vae"] = []
    status = generator.status()
    assert status.state == GeneratorState.NOT_INSTALLED and "sdxl.vae.safetensors" in status.reason


def closed_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def test_status_when_nothing_answers(tmp_path, monkeypatch):
    monkeypatch.setattr(comfyui_check, "REQUEST_TIMEOUT_S", 0.5)
    url = f"http://127.0.0.1:{closed_port()}"
    assert ComfyUIGenerator(url, install_root=tmp_path).status().state == GeneratorState.NOT_INSTALLED
    (tmp_path / "ComfyUI").mkdir()
    (tmp_path / "ComfyUI" / "main.py").write_text("", encoding="utf-8")
    status = ComfyUIGenerator(url, install_root=tmp_path).status()
    assert status.state == GeneratorState.NOT_RUNNING and str(tmp_path) in status.reason
    # Another machine: whether it is installed there cannot be known from here.
    assert ComfyUIGenerator(f"http://192.0.2.1:{closed_port()}").status().state == GeneratorState.NOT_RUNNING


def test_service_over_the_comfyui_adapter(comfy, game, tmp_path):
    results = artgen_service.generate(artgen_service.Request(preset_of(game), "rook", 2, 40, tmp_path), ComfyUIGenerator(comfy.url))
    assert [r.seed for r in results] == [40, 41]
    assert [p["3"]["inputs"]["seed"] for p in comfy.prompts] == [40, 41]
    assert results[0].provenance["generator"]["name"] == "comfyui"
    assert results[0].provenance["generatorFile"] == "p1.png"


# --- the command line


def test_cli_generate_with_the_fake_generator(game, tmp_path, capsys):
    code = artgen.main(["generate", "--generator", "fake", "--game-root", str(game), "--preset", "wood", "--subject", "pawn",
                        "--count", "2", "--seed", "3", "--out", str(tmp_path / "o"), "--json"])
    assert code == artgen.EXIT_OK
    lines = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert lines[-1]["kind"] == "done" and [i["seed"] for i in lines[-1]["images"]] == [3, 4]
    assert (tmp_path / "o" / "pawn_4.png").is_file()


def test_cli_generate_with_a_reference_image_alone_means_init(game, picture, tmp_path, capsys):
    common = ["generate", "--generator", "fake", "--game-root", str(game), "--preset", "wood", "--subject", "rook", "--count", "1", "--seed", "3",
              "--out", str(tmp_path / "o")]
    provenance_path = tmp_path / "o" / "rook_3.artgen.json"
    assert artgen.main(common + ["--reference-image", "assets/artgen/rook.png"]) == artgen.EXIT_OK
    reference = json.loads(provenance_path.read_text(encoding="utf-8"))["reference"]
    assert reference["mode"] == "init" and reference["weight"] == 0.4
    assert artgen.main(common + ["--reference-image", str(picture), "--reference-weight", "0.6"]) == artgen.EXIT_OK
    assert json.loads(provenance_path.read_text(encoding="utf-8"))["reference"]["weight"] == 0.6
    capsys.readouterr()
    assert artgen.main(common + ["--reference-image", "../x.png"]) == artgen.EXIT_FAILED
    assert "'../x.png'" in capsys.readouterr().err
    assert artgen.main(common + ["--reference-weight", "0.5"]) == artgen.EXIT_FAILED
    assert "need --reference-image" in capsys.readouterr().err


def test_cli_presets_and_errors(game, capsys):
    assert artgen.main(["presets", "--game-root", str(game), "--json"]) == artgen.EXIT_OK
    assert json.loads(capsys.readouterr().out)["wood"]["workflow"] == "sdxl_basic"
    assert artgen.main(["generate", "--generator", "fake", "--game-root", str(game), "--preset", "nope", "--subject", "x"]) == artgen.EXIT_FAILED
    assert "no preset 'nope'" in capsys.readouterr().err


def test_cli_status_exit_codes(tmp_path, capsys):
    assert artgen.main(["status", "--generator", "fake"]) == artgen.EXIT_OK
    assert artgen.main(["status", "--url", f"http://127.0.0.1:{closed_port()}", "--json"]) in (artgen.EXIT_NOT_INSTALLED, artgen.EXIT_FAILED)


# --- sounds


AUDIO = {"preset": 1, "title": "Knock", "workflow": "sao_sfx", "prompt": "a {subject} on wood", "params": {"duration": 1.5, "steps": 8}}


def test_the_sound_workflow_is_audio_and_duration_reaches_both_nodes(game):
    write_preset(game, "knock", AUDIO)
    preset = preset_of(game, "knock")
    assert preset.kind == "audio" and preset.workflow.kind == "audio" and preset.duration == 1.5
    graph = artgen_preset.build_graph(preset, "pawn", 9)
    assert graph["5"]["inputs"]["seconds"] == 1.5 and graph["11"]["inputs"]["seconds_total"] == 1.5
    assert graph["6"]["inputs"]["text"] == "a pawn on wood" and graph["3"]["inputs"]["seed"] == 9
    assert artgen_preset.models_of(graph) == {"ckpt_name": ["stable-audio-open-1.0.safetensors"], "clip_name": ["t5_base.safetensors"]}
    described = artgen_preset.find_workflows(game)["sao_sfx"].describe()
    assert described["kind"] == "audio" and described["inputs"]["duration"] == {"default": 2.0}
    assert "kind" not in described["inputs"]
    write_preset(game, "plain", {**AUDIO, "params": {}})
    assert preset_of(game, "plain").duration == 2.0, "the workflow's own length"
    assert preset_of(game).kind == "image" and preset_of(game).duration is None


@pytest.mark.parametrize("change, message", [
    ({"size": [64, 64]}, "\"size\" is for an image"),
    ({"params": {"duration": 0}}, "positive number"),
    ({"params": {"duration": "long"}}, "positive number"),
    ({"kind": "image"}, "cannot use the workflow 'sao_sfx'"),
    ({"kind": "video"}, "must be one of"),
])
def test_what_a_sound_preset_cannot_say(game, change, message):
    write_preset(game, "knock", {**AUDIO, **change})
    with pytest.raises(artgen_preset.PresetError, match=message):
        preset_of(game, "knock")


def test_an_image_preset_has_no_duration(game):
    write_preset(game, "wood", {**PRESET, "params": {"duration": 2}})
    with pytest.raises(artgen_preset.PresetError, match="is for a sound"):
        preset_of(game)
    write_preset(game, "wood", {**PRESET, "kind": "audio"})
    with pytest.raises(artgen_preset.PresetError, match="makes image"):
        preset_of(game)


def test_a_sound_is_written_as_flac_with_its_length_in_the_provenance(game, tmp_path):
    write_preset(game, "knock", AUDIO)
    request = artgen_service.Request(preset_of(game, "knock"), "pawn", 2, 4, tmp_path / "out")
    events = []
    results = artgen_service.generate(request, FakeGenerator(), events.append)
    assert [r.path.name for r in results] == ["pawn_4.flac", "pawn_5.flac"]
    assert results[0].path.read_bytes()[:4] == b"fLaC"
    provenance = results[0].provenance
    assert provenance["kind"] == "audio" and provenance["duration"] == 1.5
    assert provenance["sampleRate"] == 44100 and provenance["channels"] == 2
    assert provenance["seconds"] == pytest.approx(0.5), "the fake makes half a second at most"
    assert provenance["size"] is None
    assert [e["output"] for e in events if e["kind"] == "image"] == ["audio", "audio"]


def test_a_generator_without_the_audio_models_refuses_a_sound(game, tmp_path):
    class ImagesOnly(FakeGenerator):
        def status(self):
            status = super().status()
            status.details["kinds"] = ["image"]
            return status

    write_preset(game, "knock", AUDIO)
    with pytest.raises(artgen_service.RequestError, match="install --audio"):
        artgen_service.generate(artgen_service.Request(preset_of(game, "knock"), "pawn", 1, 1, tmp_path), ImagesOnly())
    assert artgen_service.generate(artgen_service.Request(preset_of(game), "pawn", 1, 1, tmp_path), ImagesOnly())


def test_the_fake_flac_is_a_real_stream():
    data = artgen_generators.tiny_flac(0.2)
    assert artgen_generators.flac_info(data) == (44100, 2, pytest.approx(0.2))
    assert artgen_generators.flac_info(b"RIFF....") is None


# --- the checks of a cut-out

CLEAN_CUTOUT = png_of(draw(200, disc(100, 100, 60)))
CUT_BY_EDGE = png_of(draw(200, disc(100, 40, 60)))


class CutOutGenerator(FakeGenerator):
    """Answers with RGBA pictures in turn: a clean cut-out for an even seed, one cut by the frame's top edge for an odd one."""

    def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
        super().run(graph, on_progress, cancel)
        seed = next(n["inputs"]["seed"] for n in graph.values() if "seed" in n.get("inputs", {}))
        return [Image(CLEAN_CUTOUT if seed % 2 == 0 else CUT_BY_EDGE, f"fake_{seed}.png")]


def generate_cutouts(game, out, generator=None, preset="wood", count=2):
    events = []
    request = artgen_service.Request(preset_of(game, preset), "pawn", count, 10, out)
    results = artgen_service.generate(request, generator or CutOutGenerator(), events.append)
    return results, [e for e in events if e["kind"] == "image"]


def test_a_preset_has_the_default_checks_none_or_the_thresholds_it_names(game):
    assert preset_of(game).checks == artgen_checks.DEFAULTS
    write_preset(game, "off", {**PRESET, "checks": False})
    assert preset_of(game, "off").checks is None
    write_preset(game, "own", {**PRESET, "checks": {"maxFill": 0.5, "cut": 100}})
    own = preset_of(game, "own")
    assert own.checks == {**artgen_checks.DEFAULTS, "maxFill": 0.5, "cut": 100}
    assert own.data["checks"] == {"maxFill": 0.5, "cut": 100}, "the provenance names what the file says"
    write_preset(game, "empty", {**PRESET, "checks": {}})
    assert preset_of(game, "empty").checks == artgen_checks.DEFAULTS


@pytest.mark.parametrize("checks, message", [
    (True, "false or an object"),
    (None, "false or an object"),
    ([0.5], "false or an object"),
    ({"maxFil": 0.5}, "no threshold 'maxFil'"),
    ({"maxFill": "high"}, "'maxFill' must be a number from 0 to 1"),
    ({"maxFill": True}, "'maxFill' must be a number from 0 to 1"),
    ({"maxFill": 1.5}, "'maxFill' must be a number from 0 to 1"),
    ({"maxOffset": -0.1}, "'maxOffset' must be a number from 0 to 1"),
    ({"cut": 0}, "'cut' must be an integer from 1 to 255"),
    ({"cut": 128.5}, "'cut' must be an integer from 1 to 255"),
    ({"maxPieces": 0}, "'maxPieces' must be an integer 1 or more"),
])
def test_a_bad_checks_key_is_refused_naming_the_file(game, checks, message):
    path = write_preset(game, "wood", {**PRESET, "checks": checks})
    with pytest.raises(artgen_preset.PresetError, match=message) as error:
        preset_of(game)
    assert str(path) in str(error.value) and '"checks"' in str(error.value)


def test_a_draft_with_bad_checks_is_named_by_its_form(game):
    with pytest.raises(artgen_preset.PresetError, match="the draft of 'form'.*no threshold 'x'"):
        artgen_preset.parse_preset({**PRESET, "checks": {"x": 1}}, "draft", game, label="the draft of 'form'")


@pytest.mark.parametrize("checks", [False, {}])
def test_a_sound_preset_has_no_checks(game, checks):
    write_preset(game, "knock", {**AUDIO, "checks": checks})
    with pytest.raises(artgen_preset.PresetError, match='a sound has no "checks"'):
        preset_of(game, "knock")
    write_preset(game, "knock", AUDIO)
    assert preset_of(game, "knock").checks is None


def test_the_verdict_of_each_variant_is_in_its_provenance_and_its_event(game, tmp_path):
    results, events = generate_cutouts(game, tmp_path)
    passing, failing = results
    assert passing.provenance["checks"]["passed"] is True and passing.provenance["checks"]["failed"] == []
    assert failing.provenance["checks"]["passed"] is False and failing.provenance["checks"]["failed"] == ["edge"]
    assert failing.provenance["checks"]["thresholds"] == artgen_checks.DEFAULTS and "edgeRun" in failing.provenance["checks"]["metrics"]
    assert json.loads(failing.provenance_path.read_text(encoding="utf-8"))["checks"] == failing.provenance["checks"]
    assert [e["checks"] for e in events] == [{"passed": True, "failed": []}, {"passed": False, "failed": ["edge"]}]


def test_the_thresholds_of_the_preset_decide_the_verdict(game, tmp_path):
    write_preset(game, "loose", {**PRESET, "checks": {"maxEdgeRun": 0.9}})
    results, _ = generate_cutouts(game, tmp_path, preset="loose")
    assert [r.provenance["checks"]["passed"] for r in results] == [True, True]
    assert results[1].provenance["checks"]["thresholds"]["maxEdgeRun"] == 0.9
    assert results[1].provenance["presetData"]["checks"] == {"maxEdgeRun": 0.9}


def test_no_checks_for_an_opaque_picture_for_checks_false_and_for_a_sound(game, tmp_path):
    results, events = generate_cutouts(game, tmp_path / "a", FakeGenerator())
    assert all("checks" not in r.provenance for r in results) and all("checks" not in e for e in events)
    write_preset(game, "off", {**PRESET, "checks": False})
    results, events = generate_cutouts(game, tmp_path / "b", preset="off")
    assert all("checks" not in r.provenance for r in results) and all("checks" not in e for e in events)
    write_preset(game, "knock", AUDIO)
    results, events = generate_cutouts(game, tmp_path / "c", FakeGenerator(), preset="knock", count=1)
    assert "checks" not in results[0].provenance and "checks" not in events[0]


def test_a_picture_that_cannot_be_read_is_a_verdict_with_an_error_and_not_a_failed_run(game, tmp_path):
    class Truncated(FakeGenerator):
        def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
            return [Image(CLEAN_CUTOUT[:-40], "fake_1.png")]

    results, events = generate_cutouts(game, tmp_path, Truncated(), count=1)
    verdict = results[0].provenance["checks"]
    assert verdict["passed"] is None and "truncated" in verdict["error"]
    assert events[0]["checks"] == {"passed": None, "error": verdict["error"]}


# --- the check command


def test_cli_check_exit_codes_and_output(tmp_path, capsys):
    clean, cut, opaque, broken = (tmp_path / name for name in ("clean.png", "cut.png", "opaque.png", "broken.png"))
    clean.write_bytes(CLEAN_CUTOUT)
    cut.write_bytes(CUT_BY_EDGE)
    opaque.write_bytes(tiny_png(8, 8, (1, 2, 3)))
    broken.write_bytes(CLEAN_CUTOUT[:-40])
    assert artgen.main(["check", str(clean), str(opaque)]) == artgen.EXIT_OK
    assert capsys.readouterr().out.splitlines() == [f"{clean}: passed", f"{opaque}: not applicable (no alpha channel)"]
    assert artgen.main(["check", str(clean), str(cut)]) == artgen.EXIT_FAILED
    assert capsys.readouterr().out.splitlines()[-1] == f"{cut}: FAILED edge"
    assert artgen.main(["check", str(broken)]) == artgen.EXIT_FAILED
    assert "cannot be read" in capsys.readouterr().out


def test_cli_check_json_and_the_thresholds_of_a_preset(game, tmp_path, capsys):
    cut = tmp_path / "cut.png"
    cut.write_bytes(CUT_BY_EDGE)
    write_preset(game, "loose", {**PRESET, "checks": {"maxEdgeRun": 0.9}})
    assert artgen.main(["check", str(cut), "--preset", "loose", "--game-root", str(game), "--json"]) == artgen.EXIT_OK
    report = json.loads(capsys.readouterr().out)
    assert report[0]["path"] == str(cut) and report[0]["checks"]["passed"] is True
    assert report[0]["checks"]["thresholds"]["maxEdgeRun"] == 0.9
    write_preset(game, "off", {**PRESET, "checks": False})
    assert artgen.main(["check", str(cut), "--preset", "off", "--game-root", str(game)]) == artgen.EXIT_FAILED, "off in the preset: the defaults"


def test_cli_check_bad_arguments_exit_2(game, tmp_path, capsys):
    assert artgen.main(["check", str(tmp_path / "nope.png")]) == 2
    assert "nope.png" in capsys.readouterr().err
    cut = tmp_path / "cut.png"
    cut.write_bytes(CUT_BY_EDGE)
    assert artgen.main(["check", str(cut), "--preset", "missing", "--game-root", str(game)]) == 2
    assert "no preset 'missing'" in capsys.readouterr().err
    with pytest.raises(SystemExit) as exit_info:
        artgen.main(["check"])
    assert exit_info.value.code == 2


# --- the extra images of a run that fails its checks


class AlwaysCut(CutOutGenerator):
    """Every picture is cut by the frame's edge: no seed can pass."""

    def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
        FakeGenerator.run(self, graph, on_progress, cancel)
        return [Image(CUT_BY_EDGE, "fake.png")]


class FirstCut(CutOutGenerator):
    """Only the first picture is cut; the rest are clean whatever the seed."""

    def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
        first = not self.graphs
        FakeGenerator.run(self, graph, on_progress, cancel)
        return [Image(CUT_BY_EDGE if first else CLEAN_CUTOUT, "fake.png")]


def regenerate_cutouts(game, out, generator, count=2, seed=11, preset="wood", **keys):
    events = []
    request = artgen_service.Request(preset_of(game, preset), "pawn", count, seed, out, **keys)
    results = artgen_service.generate(request, generator, events.append)
    return results, events


def seeds_of(results):
    return [r.seed for r in results]


def test_nothing_extra_when_enough_pass_or_nothing_is_asked(game, tmp_path):
    generator = CutOutGenerator()
    results, events = regenerate_cutouts(game, tmp_path / "a", generator, count=2, seed=10, min_pass=1, extra=3)
    assert seeds_of(results) == [10, 11] and len(generator.graphs) == 2, "seed 10 passes, so one is enough"
    assert not any(r.extra for r in results) and all("extra" not in e for e in events)
    generator = CutOutGenerator()
    results, _ = regenerate_cutouts(game, tmp_path / "b", generator, count=2, seed=10, min_pass=2, extra=0)
    assert seeds_of(results) == [10, 11] and len(generator.graphs) == 2, "extra 0 is off"
    generator = CutOutGenerator()
    results, _ = regenerate_cutouts(game, tmp_path / "c", generator, count=2, seed=10)
    assert seeds_of(results) == [10, 11] and len(generator.graphs) == 2, "a preset that says nothing behaves as before"


def test_extra_images_go_on_the_next_seeds_until_enough_pass(game, tmp_path):
    results, events = regenerate_cutouts(game, tmp_path, CutOutGenerator(), count=2, seed=11, min_pass=2, extra=3)
    assert seeds_of(results) == [11, 12, 13, 14]
    assert [r.extra for r in results] == [False, False, True, True]
    assert [r.provenance["checks"]["passed"] for r in results] == [False, True, False, True]
    marked = [(e["kind"], e["image"], e["count"], e.get("extra")) for e in events if e["seed"] in (13, 14)]
    assert marked == [("step", 2, 3, True)] * 3 + [("image", 2, 3, True)] + [("step", 3, 4, True)] * 3 + [("image", 3, 4, True)]
    assert all("extra" not in e and e["count"] == 2 for e in events if e["seed"] in (11, 12))
    assert sorted(p.name for p in tmp_path.glob("*.png")) == [f"pawn_{seed}.png" for seed in (11, 12, 13, 14)]


def test_a_run_that_cannot_pass_ends_ok_after_exactly_the_extra_limit(game, tmp_path):
    generator = AlwaysCut()
    results, events = regenerate_cutouts(game, tmp_path, generator, count=2, seed=20, min_pass=1, extra=3)
    assert seeds_of(results) == [20, 21, 22, 23, 24] and len(generator.graphs) == 5
    assert [r.extra for r in results] == [False, False, True, True, True]
    assert artgen_service.count_passing(results) == 0
    assert [e["count"] for e in events if e["kind"] == "image"] == [2, 2, 3, 4, 5]


def test_the_first_extra_that_passes_ends_the_run(game, tmp_path):
    generator = FirstCut()
    results, _ = regenerate_cutouts(game, tmp_path, generator, count=2, seed=20, min_pass=2, extra=5)
    assert [(r.seed, r.extra) for r in results] == [(20, False), (21, False), (22, True)], "20 is cut, 21 passes, the extra 22 makes two"
    assert len(generator.graphs) == 3


def test_a_picture_without_alpha_or_unreadable_never_asks_for_an_extra(game, tmp_path):
    results, _ = regenerate_cutouts(game, tmp_path / "icon", FakeGenerator(), count=2, min_pass=2, extra=3)
    assert len(results) == 2 and artgen_service.count_passing(results) == 2

    class Truncated(FakeGenerator):
        def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
            return [Image(CLEAN_CUTOUT[:-40], "fake.png")]

    results, _ = regenerate_cutouts(game, tmp_path / "broken", Truncated(), count=2, min_pass=2, extra=3)
    assert len(results) == 2 and all(r.provenance["checks"]["passed"] is None for r in results)
    assert artgen_service.count_passing(results) == 2, "regenerating cannot cure an unreadable file"


def test_a_seed_whose_file_exists_is_skipped_and_its_file_is_left_alone(game, tmp_path):
    taken = tmp_path / "pawn_13.png"
    taken.write_bytes(b"an earlier image")
    (tmp_path / "pawn_14.artgen.json").write_text("{}", encoding="utf-8")
    results, _ = regenerate_cutouts(game, tmp_path, AlwaysCut(), count=2, seed=11, min_pass=2, extra=2)
    assert seeds_of(results) == [11, 12, 15, 16]
    assert taken.read_bytes() == b"an earlier image" and (tmp_path / "pawn_14.artgen.json").read_text(encoding="utf-8") == "{}"
    assert not (tmp_path / "pawn_14.png").exists()


def test_the_seed_picker_skips_files_stops_at_the_int64_limit_and_gives_up_after_the_probe_limit(tmp_path):
    assert artgen_service.free_seed(tmp_path, "s", 5) == 5
    (tmp_path / "s_5.png").write_bytes(b"")
    (tmp_path / "s_6.flac").write_bytes(b"")
    (tmp_path / "s_7.artgen.json").write_text("{}", encoding="utf-8")
    assert artgen_service.free_seed(tmp_path, "s", 5) == 8
    (tmp_path / "other_8.png").write_bytes(b"")
    assert artgen_service.free_seed(tmp_path, "s", 5) == 8, "another stem's file is not in the way"
    top = artgen_service.SEED_LIMIT
    assert artgen_service.free_seed(tmp_path, "s", top - 1) == top - 1
    assert artgen_service.free_seed(tmp_path, "s", top) is None
    (tmp_path / f"s_{top - 1}.png").write_bytes(b"")
    assert artgen_service.free_seed(tmp_path, "s", top - 1) is None, "never a seed of 2**63 or more"
    for seed in range(100, 100 + artgen_service.MAX_SEED_PROBES):
        (tmp_path / f"s_{seed}.png").write_bytes(b"")
    assert artgen_service.free_seed(tmp_path, "s", 100) is None
    assert artgen_service.free_seed(tmp_path, "s", 101) == 100 + artgen_service.MAX_SEED_PROBES


def test_extra_images_stop_when_no_seed_is_free(game, tmp_path, monkeypatch):
    monkeypatch.setattr(artgen_service, "free_seed", lambda *args: None)
    results, _ = regenerate_cutouts(game, tmp_path, AlwaysCut(), count=2, min_pass=2, extra=3)
    assert len(results) == 2


def test_the_requests_keys_override_the_presets_and_its_defaults_apply_without_them(game, tmp_path):
    write_preset(game, "again", {**PRESET, "regenerate": {"extra": 2, "minPass": 2}})
    assert preset_of(game, "again").regenerate == {"minPass": 2, "extra": 2}
    results, _ = regenerate_cutouts(game, tmp_path / "a", AlwaysCut(), preset="again")
    assert len(results) == 4, "the preset's extra 2 on top of the count 2"
    results, _ = regenerate_cutouts(game, tmp_path / "b", AlwaysCut(), preset="again", extra=0)
    assert len(results) == 2, "extra 0 of the request switches it off"
    results, _ = regenerate_cutouts(game, tmp_path / "c", AlwaysCut(), preset="again", extra=1)
    assert len(results) == 3
    results, _ = regenerate_cutouts(game, tmp_path / "d", CutOutGenerator(), preset="again", seed=10, min_pass=1)
    assert len(results) == 2, "min_pass 1 replaces the preset's 2: seed 10 passes"
    write_preset(game, "default-min", {**PRESET, "regenerate": {"extra": 3}})
    results, _ = regenerate_cutouts(game, tmp_path / "e", CutOutGenerator(), preset="default-min", seed=11)
    assert seeds_of(results) == [11, 12, 13, 14], "no minPass: the count wanted"
    write_preset(game, "big-min", {**PRESET, "regenerate": {"extra": 1, "minPass": 9}})
    results, _ = regenerate_cutouts(game, tmp_path / "f", AlwaysCut(), preset="big-min")
    assert len(results) == 3
    assert artgen_service.regeneration_of(artgen_service.Request(preset_of(game, "big-min"), "x", 2)) == (2, 1), "a minPass above the count is the count"


def test_a_request_with_an_extra_on_a_preset_with_none_regenerates(game, tmp_path):
    results, _ = regenerate_cutouts(game, tmp_path, AlwaysCut(), extra=2)
    assert len(results) == 4, "min_pass defaults to the count"


@pytest.mark.parametrize("regenerate, message", [
    (True, "must be an object"),
    ([2], "must be an object"),
    ({}, "\"extra\" is required"),
    ({"minPass": 1}, "\"extra\" is required"),
    ({"extra": 0}, "from 1 to 16"),
    ({"extra": 17}, "from 1 to 16"),
    ({"extra": True}, "from 1 to 16"),
    ({"extra": 2.0}, "from 1 to 16"),
    ({"extra": "2"}, "from 1 to 16"),
    ({"extra": 2, "minPass": 0}, "\"minPass\" must be an integer 1 or more"),
    ({"extra": 2, "minPass": True}, "\"minPass\" must be an integer 1 or more"),
    ({"extra": 2, "minPass": 1.5}, "\"minPass\" must be an integer 1 or more"),
    ({"extra": 2, "minPass": None}, "\"minPass\" must be an integer 1 or more"),
    ({"extra": 2, "min": 1}, "no key 'min'"),
])
def test_a_bad_regenerate_key_is_refused_naming_the_file(game, regenerate, message):
    path = write_preset(game, "wood", {**PRESET, "regenerate": regenerate})
    with pytest.raises(artgen_preset.PresetError, match=message) as error:
        preset_of(game)
    assert str(path) in str(error.value) and '"regenerate"' in str(error.value)


def test_regenerate_is_refused_for_a_draft_a_sound_and_a_preset_without_checks(game):
    with pytest.raises(artgen_preset.PresetError, match="the draft of 'form'.*\"extra\" is required"):
        artgen_preset.parse_preset({**PRESET, "regenerate": {}}, "draft", game, label="the draft of 'form'")
    write_preset(game, "knock", {**AUDIO, "regenerate": {"extra": 2}})
    with pytest.raises(artgen_preset.PresetError, match='a sound has no "regenerate"'):
        preset_of(game, "knock")
    write_preset(game, "off", {**PRESET, "checks": False, "regenerate": {"extra": 2}})
    with pytest.raises(artgen_preset.PresetError, match='"regenerate" needs the checks, and "checks" is false'):
        preset_of(game, "off")
    write_preset(game, "ok", {**PRESET, "regenerate": {"extra": 2}})
    assert preset_of(game, "ok").regenerate == {"minPass": None, "extra": 2}
    assert preset_of(game).regenerate is None


@pytest.mark.parametrize("keys, message", [
    ({"extra": True}, "extra must be an integer"),
    ({"extra": 2.0}, "extra must be an integer"),
    ({"extra": "2"}, "extra must be an integer"),
    ({"extra": -1}, "extra must be 0 to 16"),
    ({"extra": 17}, "extra must be 0 to 16"),
    ({"min_pass": True}, "min_pass must be an integer"),
    ({"min_pass": 1.5}, "min_pass must be an integer"),
    ({"min_pass": 0}, r"min_pass must be 1 to the count \(2\)"),
    ({"min_pass": 3}, r"min_pass must be 1 to the count \(2\)"),
])
def test_a_bad_extra_or_min_pass_of_a_request_is_refused_before_the_generator_is_touched(game, tmp_path, keys, message):
    generator = CutOutGenerator()
    request = artgen_service.Request(preset_of(game), "x", 2, 1, tmp_path / "out", **keys)
    with pytest.raises(artgen_service.RequestError, match=message):
        artgen_service.generate(request, generator)
    assert generator.graphs == [] and not (tmp_path / "out").exists()


def test_an_explicit_extra_is_refused_for_a_sound_and_for_a_preset_without_checks(game, tmp_path):
    write_preset(game, "knock", AUDIO)
    write_preset(game, "off", {**PRESET, "checks": False})
    for name, message in (("knock", "extra is for an image"), ("off", "extra needs the checks")):
        generator = FakeGenerator()
        request = artgen_service.Request(preset_of(game, name), "x", 1, 1, tmp_path / name, extra=2)
        with pytest.raises(artgen_service.RequestError, match=message):
            artgen_service.generate(request, generator)
        assert generator.graphs == [] and not (tmp_path / name).exists()
        request = artgen_service.Request(preset_of(game, name), "x", 1, 1, tmp_path / name, extra=0)
        assert len(artgen_service.generate(request, generator)) == 1, "extra 0 is not a request for one"


def test_a_cancel_during_an_extra_keeps_the_images_written(game, tmp_path):
    cancel = threading.Event()

    def progress(event):
        if event["kind"] == "step" and event.get("extra"):
            cancel.set()

    out = tmp_path / "o"
    request = artgen_service.Request(preset_of(game), "pawn", 1, 11, out, min_pass=1, extra=3)
    with pytest.raises(Cancelled):
        artgen_service.generate(request, AlwaysCut(), progress, cancel)
    assert sorted(p.name for p in out.iterdir()) == ["pawn_11.artgen.json", "pawn_11.png"]


def test_a_failure_of_the_generator_during_an_extra_passes_through(game, tmp_path):
    class FailsOnExtra(AlwaysCut):
        def run(self, graph, on_progress=None, cancel=None, timeout_s=0):
            if self.graphs:
                raise GenerationError("boom")
            return super().run(graph, on_progress, cancel)

    out = tmp_path / "o"
    request = artgen_service.Request(preset_of(game), "pawn", 1, 11, out, extra=3)
    with pytest.raises(GenerationError, match="boom"):
        artgen_service.generate(request, FailsOnExtra())
    assert [p.name for p in out.glob("*.png")] == ["pawn_11.png"]


def cli_cutouts(game, tmp_path, monkeypatch, generator, *flags):
    monkeypatch.setattr(artgen, "make_generator", lambda kind, url: generator)
    return ["generate", "--game-root", str(game), "--preset", "wood", "--subject", "pawn", "--count", "2", "--seed", "11",
            "--out", str(tmp_path / "o"), *flags]


def test_cli_generate_regenerates_with_its_flags_and_marks_the_extra_images(game, tmp_path, monkeypatch, capsys):
    argv = cli_cutouts(game, tmp_path, monkeypatch, CutOutGenerator(), "--min-pass", "2", "--extra", "3")
    assert artgen.main(argv) == artgen.EXIT_OK
    lines = capsys.readouterr().out.splitlines()
    images = [line for line in lines if line.startswith("[")]
    assert [line.split("]")[0] for line in images if "seed" not in line] == ["[1/2", "[2/2", "[3/3", "[4/4"]
    assert [line.endswith("(extra)") for line in images if "seed" not in line] == [False, False, True, True]
    assert len(lines) == len(images) and not any("wanted" in line for line in lines), "enough pass: no short line"


def test_cli_generate_json_lists_extra_and_passed_per_image_and_the_passing_count(game, tmp_path, monkeypatch, capsys):
    argv = cli_cutouts(game, tmp_path, monkeypatch, CutOutGenerator(), "--min-pass", "2", "--extra", "3", "--json")
    assert artgen.main(argv) == artgen.EXIT_OK
    events = [json.loads(line) for line in capsys.readouterr().out.splitlines()]
    assert [e.get("extra", False) for e in events if e["kind"] == "image"] == [False, False, True, True]
    done = events[-1]
    assert [(i["seed"], i["extra"], i["passed"]) for i in done["images"]] == [(11, False, False), (12, False, True), (13, True, False), (14, True, True)]
    assert done["passing"] == 2
    argv = cli_cutouts(game, tmp_path / "icons", monkeypatch, FakeGenerator(), "--json")
    assert artgen.main(argv) == artgen.EXIT_OK
    done = json.loads(capsys.readouterr().out.splitlines()[-1])
    assert [i["passed"] for i in done["images"]] == [None, None] and done["passing"] == 2, "no verdict is not failing"


def test_cli_generate_says_when_the_run_is_short_and_still_exits_0(game, tmp_path, monkeypatch, capsys):
    argv = cli_cutouts(game, tmp_path, monkeypatch, AlwaysCut(), "--min-pass", "2", "--extra", "1")
    assert artgen.main(argv) == artgen.EXIT_OK
    lines = capsys.readouterr().out.splitlines()
    assert lines[-1] == "only 0 of the 2 wanted images pass their checks"
    assert [line for line in lines if line.startswith("[3/3] ")][-1].endswith("(extra)")
    argv = cli_cutouts(game, tmp_path / "again", monkeypatch, CutOutGenerator(), "--min-pass", "2")
    assert artgen.main(argv) == artgen.EXIT_OK
    assert capsys.readouterr().out.splitlines()[-1] == "only 1 of the 2 wanted images pass their checks", "no extra asked: short is still said"


def test_cli_generate_refuses_a_bad_flag_like_any_request_error(game, tmp_path, monkeypatch, capsys):
    generator = CutOutGenerator()
    for flags, message in ((("--extra", "17"), "extra must be 0 to 16"), (("--min-pass", "3"), "min_pass must be 1 to the count"),
                           (("--extra", "x"), None)):
        argv = cli_cutouts(game, tmp_path, monkeypatch, generator, *flags)
        if message is None:
            with pytest.raises(SystemExit) as exit_info:
                artgen.main(argv)
            assert exit_info.value.code == 2
        else:
            assert artgen.main(argv) == artgen.EXIT_FAILED
            assert message in capsys.readouterr().err
    assert generator.graphs == [] and not (tmp_path / "o").exists()


# --- a run with a reference gets the checks and the extra images like any other

@pytest.mark.parametrize("reference", [REFERENCE, STYLE], ids=["init", "style"])
def test_a_reference_run_gets_the_verdicts_and_the_extra_images_of_its_request(game, picture, tmp_path, reference):
    generator = CutOutGenerator()
    events = []
    request = artgen_service.Request(preset_of(game), "pawn", 2, 11, tmp_path, reference=reference, game_root=game, min_pass=2, extra=3)
    results = artgen_service.generate(request, generator, events.append)
    assert seeds_of(results) == [11, 12, 13, 14], "11 and 13 are cut, 12 and 14 pass"
    assert [r.extra for r in results] == [False, False, True, True]
    assert [r.provenance["checks"]["passed"] for r in results] == [False, True, False, True]
    assert {r.provenance["reference"]["mode"] for r in results} == {reference["mode"]}
    assert [e["checks"]["passed"] for e in events if e["kind"] == "image"] == [False, True, False, True]
    assert len(generator.uploads) == 1, "the extra images reuse the one upload"


def test_a_style_run_of_a_cut_out_workflow_is_checked_and_regenerated_by_the_preset(game, picture, tmp_path):
    write_preset(game, "cut", {**PRESET, "workflow": "sdxl_cutout", "regenerate": {"extra": 3, "minPass": 2}})
    results = artgen_service.generate(reference_request(game, tmp_path, STYLE, preset="cut"), CutOutGenerator())
    assert seeds_of(results) == [40, 41, 42], "41 is cut, so the preset's regenerate adds 42, which passes"
    assert {r.provenance["workflow"] for r in results} == {"sdxl_ipadapter_cutout"}
    assert [r.provenance["checks"]["passed"] for r in results] == [True, False, True]
    assert results[0].provenance["checks"]["thresholds"] == artgen_checks.DEFAULTS


def test_a_style_run_that_cannot_pass_ends_ok_after_the_extra_limit(game, picture, tmp_path):
    request = artgen_service.Request(preset_of(game), "pawn", 1, 20, tmp_path, reference=STYLE, game_root=game, min_pass=1, extra=2)
    results = artgen_service.generate(request, AlwaysCut())
    assert seeds_of(results) == [20, 21, 22] and artgen_service.count_passing(results) == 0
