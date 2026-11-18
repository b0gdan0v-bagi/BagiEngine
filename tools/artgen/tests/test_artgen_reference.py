"""The reference picture of a run: its shape and weight, the workflow that runs a mode, the graph it makes and the reference key of
a preset."""

import copy
import hashlib
import json
import sys
from pathlib import Path

import pytest

ARTGEN_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARTGEN_DIR))

import artgen_preset  # noqa: E402
import artgen_reference  # noqa: E402
from artgen_generators import tiny_png  # noqa: E402
from artgen_reference import ReferenceUse  # noqa: E402

PRESET = {"preset": 1, "title": "Test", "workflow": "sdxl_basic", "prompt": "a {subject}", "size": [512, 768], "params": {"steps": 12}}
REFERENCE = {"mode": "init", "image": "assets/artgen/rook.png", "weight": 0.25}


@pytest.fixture
def game(tmp_path):
    root = tmp_path / "game"
    (root / "artgen" / "presets").mkdir(parents=True)
    return root


def write_preset(root, name, data):
    path = root / "artgen" / "presets" / f"{name}.json"
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def preset_with(game, **change):
    return artgen_preset.parse_preset({**PRESET, **change}, "wood", game, label="wood.json")


def write_workflow(game, name, graph, inputs):
    folder = game / "artgen" / "workflows"
    folder.mkdir(parents=True, exist_ok=True)
    (folder / f"{name}.json").write_text(json.dumps(graph), encoding="utf-8")
    (folder / f"{name}.inputs.json").write_text(json.dumps(inputs), encoding="utf-8")


def engine_workflow(name):
    folder = artgen_preset.ENGINE_WORKFLOWS
    return json.loads((folder / f"{name}.json").read_text(encoding="utf-8")), json.loads((folder / f"{name}.inputs.json").read_text(encoding="utf-8"))


def use_of(preset, mode="init", weight=0.25, game_root=None):
    return ReferenceUse(mode, weight, "artgen_ref_x.png", artgen_reference.effective_workflow(preset.workflow, mode, game_root))


# --- the shape and the weight


def test_a_reference_gets_the_default_weight():
    assert artgen_reference.parse_reference({"mode": "init", "image": "a.png"}, "x") == {"mode": "init", "image": "a.png", "weight": 0.4}
    assert artgen_reference.parse_reference(REFERENCE, "x") == REFERENCE


@pytest.mark.parametrize("data, message", [
    ("a.png", "must be an object"),
    ({"image": "a.png"}, "reference.mode"),
    ({"mode": "style", "image": "a.png"}, "reference.mode.*init"),
    ({"mode": 3, "image": "a.png"}, "reference.mode"),
    ({"mode": "init"}, "reference.image"),
    ({"mode": "init", "image": ""}, "reference.image"),
    ({"mode": "init", "image": 4}, "reference.image"),
    ({"mode": "init", "image": "a.png", "weight": "0.5"}, "reference.weight"),
    ({"mode": "init", "image": "a.png", "weight": True}, "reference.weight"),
    ({"mode": "init", "image": "a.png", "weight": 0}, "reference.weight"),
    ({"mode": "init", "image": "a.png", "weight": 1}, "reference.weight"),
    ({"mode": "init", "image": "a.png", "weight": -0.2}, "reference.weight"),
    ({"mode": "init", "image": "a.png", "strength": 0.5}, "no key 'strength'"),
])
def test_a_malformed_reference_is_refused_by_its_key(data, message):
    with pytest.raises(artgen_preset.PresetError, match=message) as error:
        artgen_reference.parse_reference(data, "the request")
    assert str(error.value).startswith("the request:")


def test_the_weight_of_init_is_the_part_that_stays():
    assert artgen_reference.weight_value("init", 0.4) == 0.6
    assert artgen_reference.weight_value("init", 0.25) == 0.75


# --- the preset's and the request's reference


def test_a_preset_with_a_reference_key_loads_and_keeps_it(game):
    path = write_preset(game, "wood", {**PRESET, "reference": {"mode": "init", "image": "assets/x.png"}})
    preset = artgen_preset.load_preset(path, game)
    assert preset.reference == {"mode": "init", "image": "assets/x.png", "weight": 0.4}
    assert preset.data["reference"] == {"mode": "init", "image": "assets/x.png"}, "the file's content stays as written"
    assert artgen_preset.parse_preset(PRESET, "wood", game).reference is None


@pytest.mark.parametrize("reference, message", [
    ("x.png", "must be an object"),
    ({"mode": "init", "image": "a.png", "weight": 2}, "reference.weight"),
    ({"mode": "style", "image": "a.png"}, "reference.mode"),
])
def test_a_malformed_reference_of_a_preset_is_refused_with_the_file_named(game, reference, message):
    path = write_preset(game, "bad", {**PRESET, "reference": reference})
    with pytest.raises(artgen_preset.PresetError, match=message) as error:
        artgen_preset.load_preset(path, game)
    assert "bad.json" in str(error.value)


def test_a_sound_preset_takes_no_reference(game):
    sfx = json.loads((artgen_preset.ENGINE_PRESETS / "sfx.json").read_text(encoding="utf-8"))
    with pytest.raises(artgen_preset.PresetError, match="a sound has no \"reference\""):
        artgen_preset.parse_preset({**sfx, "reference": REFERENCE}, "sfx", game, label="sfx.json")
    preset = artgen_preset.parse_preset(sfx, "sfx", game)
    with pytest.raises(artgen_preset.PresetError, match="makes a sound"):
        artgen_reference.reference_of(preset, REFERENCE)


def test_the_requests_reference_replaces_the_presets_whole(game):
    preset = preset_with(game, reference={"mode": "init", "image": "a.png", "weight": 0.7})
    assert artgen_reference.reference_of(preset, None)["weight"] == 0.7
    asked = artgen_reference.reference_of(preset, {"mode": "init", "image": "b.png"})
    assert asked == {"mode": "init", "image": "b.png", "weight": 0.4}, "the weight of the preset is not merged in"
    assert artgen_reference.reference_of(preset_with(game), None) is None
    with pytest.raises(artgen_preset.PresetError, match="the request: .*reference.image"):
        artgen_reference.reference_of(preset, {"mode": "init"})


# --- the workflow that runs a mode


def test_the_map_names_the_workflow_that_runs_a_mode(game):
    base = artgen_preset.load_workflow("sdxl_cutout", game)
    assert base.reference == {"init": "sdxl_img2img_cutout"}
    assert artgen_reference.effective_workflow(base, "init", game).name == "sdxl_img2img_cutout"
    assert artgen_reference.effective_workflow(artgen_preset.load_workflow("sdxl_basic"), "init").name == "sdxl_img2img"


def test_a_workflow_that_takes_the_picture_serves_its_own_mode(game):
    workflow = artgen_preset.load_workflow("sdxl_img2img", game)
    assert workflow.reference == {} and artgen_reference.effective_workflow(workflow, "init", game) is workflow


def test_a_mode_the_workflow_cannot_run_is_refused_naming_the_modes_it_has(game):
    with pytest.raises(artgen_preset.PresetError, match=r"'sao_sfx' cannot run the reference mode 'init' \(its modes: none\)"):
        artgen_reference.effective_workflow(artgen_preset.load_workflow("sao_sfx"), "init")
    with pytest.raises(artgen_preset.PresetError, match=r"'sdxl_basic' cannot run the reference mode 'structure' \(its modes: init\)"):
        artgen_reference.effective_workflow(artgen_preset.load_workflow("sdxl_basic"), "structure")


def test_a_preset_whose_workflow_cannot_run_its_reference_is_refused_at_load(game):
    graph, inputs = engine_workflow("sdxl_basic")
    del inputs["reference"]
    write_workflow(game, "plain", graph, inputs)
    with pytest.raises(artgen_preset.PresetError, match="wood.json: the workflow 'plain' cannot run"):
        preset_with(game, workflow="plain", reference=REFERENCE)


def test_the_engines_maps_keep_every_input_of_their_bases():
    found = artgen_preset.find_workflows(None)
    mapped_any = False
    for name, base in found.items():
        assert isinstance(base, artgen_preset.Workflow), f"{name}: {base}"
        for mode, target in base.reference.items():
            mapped_any = True
            variant = found[target]
            assert artgen_preset.REFERENCE_IMAGE_INPUT in variant.inputs
            assert artgen_reference.MODES[mode].weight_input in variant.inputs
            assert variant.kind == base.kind
            lacking = [k for k in base.inputs if k not in artgen_preset.RESERVED_KEYS and k not in variant.inputs]
            assert lacking == [], f"{target} lacks {lacking} of {name}"
    assert mapped_any


def test_the_img2img_graphs_lay_the_picture_under_the_alpha_and_end_like_their_bases():
    for base_name, variant_name in (("sdxl_basic", "sdxl_img2img"), ("sdxl_cutout", "sdxl_img2img_cutout")):
        base, variant = artgen_preset.load_workflow(base_name), artgen_preset.load_workflow(variant_name)
        assert variant.graph["3"]["inputs"]["latent_image"] == ["28", 0], "the sampler starts from the encoded picture"
        classes = [n["class_type"] for n in variant.graph.values()]
        for needed in ("LoadImage", "ImageScale", "InvertMask", "ImageCompositeMasked", "EmptyImage", "VAEEncode"):
            assert needed in classes
        assert "EmptyLatentImage" not in classes
        assert variant.graph[variant.inputs["output"]]["class_type"] == base.graph[base.inputs["output"]]["class_type"]
        ending = {k: v for k, v in base.graph.items() if k in ("8", "9", "11", "12", "13", "14")}
        for node, value in ending.items():
            assert {k: v for k, v in variant.graph[node].items() if k != "inputs"} == {k: v for k, v in value.items() if k != "inputs"}
    assert artgen_preset.load_workflow("sdxl_img2img").graph["3"]["inputs"]["denoise"] == 0.6


@pytest.mark.parametrize("change, message", [
    (lambda inputs, graph: inputs.pop("steps"), "does not name the input 'steps' of 'base'"),
    (lambda inputs, graph: inputs.pop("denoise"), "does not name the input 'denoise'"),
    (lambda inputs, graph: inputs.pop("reference_image"), "does not name the input 'reference_image'"),
    (lambda inputs, graph: inputs.update(reference={"init": "sdxl_basic"}), "has a reference map of its own"),
    (lambda inputs, graph: inputs.update(kind="audio"), "makes audio, not image"),
])
def test_a_mapped_workflow_that_lacks_what_its_base_has_is_refused(game, change, message):
    graph, inputs = engine_workflow("sdxl_img2img")
    change(inputs, graph)
    write_workflow(game, "variant", graph, inputs)
    base_graph, base_inputs = engine_workflow("sdxl_basic")
    base_inputs["reference"] = {"init": "variant"}
    write_workflow(game, "base", base_graph, base_inputs)
    with pytest.raises(artgen_preset.PresetError, match=message) as error:
        artgen_preset.load_workflow("base", game)
    assert "base.inputs.json" in str(error.value)


@pytest.mark.parametrize("reference", [["init"], {"style": "sdxl_img2img"}, {"init": ""}, {"init": 3}, {"init": "nope"}])
def test_a_malformed_map_is_refused(game, reference):
    graph, inputs = engine_workflow("sdxl_basic")
    inputs["reference"] = reference
    write_workflow(game, "base", graph, inputs)
    with pytest.raises(artgen_preset.PresetError, match="base.inputs.json"):
        artgen_preset.load_workflow("base", game)


def test_a_workflow_that_maps_itself_is_refused_not_looped(game):
    graph, inputs = engine_workflow("sdxl_img2img")
    inputs["reference"] = {"init": "sdxl_img2img"}
    write_workflow(game, "sdxl_img2img", graph, inputs)
    with pytest.raises(artgen_preset.PresetError, match="has a reference map of its own"):
        artgen_preset.load_workflow("sdxl_img2img", game)


def test_the_reference_key_is_not_an_input_of_the_form_nor_of_params(game):
    described = artgen_preset.load_workflow("sdxl_basic").describe()
    assert "reference" not in described["inputs"]
    with pytest.raises(artgen_preset.PresetError, match="set by the service"):
        preset_with(game, params={"reference": 1})
    with pytest.raises(artgen_preset.PresetError, match="set by the service"):
        preset_with(game, params={"reference_image": "x.png"})


# --- the graph


def test_a_graph_with_a_reference_sets_the_picture_and_the_denoise_and_leaves_the_template_alone(game):
    preset = preset_with(game)
    reference = use_of(preset, weight=0.25)
    templates = json.dumps([preset.workflow.graph, reference.workflow.graph], sort_keys=True)
    graph = artgen_preset.build_graph(preset, "knight", 7, file_prefix="artgen_x", reference=reference)
    assert graph["20"]["inputs"]["image"] == "artgen_ref_x.png"
    assert graph["3"]["inputs"]["denoise"] == 0.75
    assert graph["3"]["inputs"]["seed"] == 7 and graph["3"]["inputs"]["steps"] == 12
    assert graph["6"]["inputs"]["text"] == "a knight"
    for node in ("21", "24", "26"):
        assert (graph[node]["inputs"]["width"], graph[node]["inputs"]["height"]) == (512, 768)
    assert graph["9"]["inputs"]["filename_prefix"] == "artgen_x"
    assert json.dumps([preset.workflow.graph, reference.workflow.graph], sort_keys=True) == templates
    assert "20" not in artgen_preset.build_graph(preset, "knight", 7)


def test_a_graph_without_a_reference_is_what_it_was(game):
    preset = preset_with(game)
    graph = artgen_preset.build_graph(preset, "knight", 7)
    assert graph["5"]["inputs"]["width"] == 512 and "denoise" in graph["3"]["inputs"] and graph["3"]["inputs"]["denoise"] == 1.0


def test_a_denoise_in_params_collides_with_the_weight(game):
    write_preset(game, "direct", {**PRESET, "workflow": "sdxl_img2img", "params": {"denoise": 0.3}})
    preset = artgen_preset.load_preset(game / "artgen" / "presets" / "direct.json", game)
    with pytest.raises(artgen_preset.PresetError, match="\"denoise\" is set by the reference weight"):
        artgen_preset.build_graph(preset, "x", 1, reference=use_of(preset))


def test_a_reference_workflow_run_without_a_reference_is_refused(game):
    write_preset(game, "direct", {**PRESET, "workflow": "sdxl_img2img", "reference": REFERENCE})
    preset = artgen_preset.load_preset(game / "artgen" / "presets" / "direct.json", game)
    assert preset.workflow.name == "sdxl_img2img"
    assert artgen_preset.build_graph(preset, "x", 1, reference=use_of(preset))["3"]["inputs"]["denoise"] == 0.75
    with pytest.raises(artgen_preset.PresetError, match="needs a reference picture"):
        artgen_preset.build_graph(preset, "x", 1)


def test_a_game_workflow_with_its_own_map_runs_through_the_mapped_one(game):
    graph, inputs = engine_workflow("sdxl_img2img")
    graph = copy.deepcopy(graph)
    graph["3"]["inputs"]["steps"] = 99
    write_workflow(game, "variant", graph, inputs)
    base_graph, base_inputs = engine_workflow("sdxl_basic")
    base_inputs["reference"] = {"init": "variant"}
    write_workflow(game, "mine", base_graph, base_inputs)
    preset = preset_with(game, workflow="mine", params={})
    reference = use_of(preset, game_root=game)
    assert reference.workflow.name == "variant" and reference.workflow.source == "game"
    assert artgen_preset.build_graph(preset, "x", 1, reference=reference)["3"]["inputs"]["steps"] == 99


# --- the picture

PNG = tiny_png(4, 4, (200, 10, 10))
JPEG = b"\xff\xd8\xff\xe0" + b"\0" * 16
WEBP = b"RIFF\x24\0\0\0WEBPVP8 " + b"\0" * 16


def put(game, relative, data):
    path = game / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def test_a_picture_inside_the_game_root_is_read_with_its_hash_and_a_name_by_content(game):
    put(game, "assets/artgen/rook.png", PNG)
    image = artgen_reference.resolve_image(game, "assets/artgen/rook.png")
    assert image.data == PNG and image.sha256 == hashlib.sha256(PNG).hexdigest() and image.extension == ".png"
    assert image.upload_name == f"artgen_ref_{image.sha256[:16]}.png"
    put(game, "other/copy.png", PNG)
    assert artgen_reference.resolve_image(game, "other/copy.png").upload_name == image.upload_name


@pytest.mark.parametrize("data, extension", [(PNG, ".png"), (JPEG, ".jpg"), (WEBP, ".webp")])
def test_the_extension_comes_from_the_first_bytes_not_from_the_name(game, data, extension):
    put(game, "assets/x.dat", data)
    assert artgen_reference.resolve_image(game, "assets/x.dat").extension == extension


def test_an_absolute_path_of_any_picture_is_taken(game, tmp_path):
    path = put(tmp_path / "elsewhere", "pic.png", PNG)
    assert artgen_reference.resolve_image(game, str(path)).data == PNG
    assert artgen_reference.resolve_image(None, str(path)).data == PNG, "an absolute path needs no game root"


def test_a_relative_path_needs_the_game_root(game):
    with pytest.raises(artgen_reference.ReferenceImageError, match="no game root"):
        artgen_reference.resolve_image(None, "assets/x.png")


@pytest.mark.parametrize("image", ["../x.png", "a/../x.png", "assets/..", "a\\x.png", "C:x.png", "a/x\0.png"])
def test_a_relative_path_cannot_leave_the_game_root(game, tmp_path, image):
    put(tmp_path, "x.png", PNG)
    put(game, "a/x.png", PNG)
    with pytest.raises(artgen_reference.ReferenceImageError, match="must be a '/'-separated path|outside|not a file"):
        artgen_reference.resolve_image(game, image)


def test_a_link_out_of_the_game_root_is_refused(game, tmp_path):
    outside = put(tmp_path / "outside", "secret.png", PNG)
    try:
        (game / "assets").mkdir()
        (game / "assets" / "link.png").symlink_to(outside)
    except (OSError, NotImplementedError):
        pytest.skip("no symlinks here")
    with pytest.raises(artgen_reference.ReferenceImageError, match="outside the game root"):
        artgen_reference.resolve_image(game, "assets/link.png")


@pytest.mark.parametrize("make, message", [
    (lambda game: (game / "assets").mkdir() or "assets", "not a file"),
    (lambda game: "missing.png", "not a file"),
    (lambda game: put(game, "text.png", b"this is not a picture") and "text.png", "not a PNG, JPEG or WebP"),
    (lambda game: put(game, "empty.png", b"") and "empty.png", "not a PNG, JPEG or WebP"),
    (lambda game: put(game, "riff.png", b"RIFF\0\0\0\0WAVEfmt ") and "riff.png", "not a PNG, JPEG or WebP"),
    (lambda game: put(game, "big.png", PNG + b"\0" * artgen_reference.MAX_REFERENCE_BYTES) and "big.png", "the limit is"),
])
def test_what_is_no_small_picture_is_refused_naming_the_path(game, make, message):
    image = make(game)
    with pytest.raises(artgen_reference.ReferenceImageError, match=message) as refused:
        artgen_reference.resolve_image(game, image)
    assert f"'{image}'" in str(refused.value)
