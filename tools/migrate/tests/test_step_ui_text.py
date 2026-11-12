"""The step 4 to 5 (steps/s0004_ui_text_image_dropdown_to_components.py) over the fixture pairs of tests/fixtures/ui_text/: the output is exactly the `after` file, the `after`
migrates to itself, a failure fixture (a `before` with no `after`) fails with a reason that names the element and leaves the file as it was. There is no C++ twin of this step: a
`UILabel` and a `UITextComponent` draw through the same `UITextPainter`, a `UIImage` and a `UIImageComponent` through the same `UISprite::FitInto`, with the same attribute names."""

import importlib.util
import re
from pathlib import Path

import pytest

import migrate
import xmltext
from conftest import ENGINE_ROOT, TESTS_DIR, TOOL_DIR

FIXTURES = TESTS_DIR / "fixtures" / "ui_text"
UI_SOURCES = ENGINE_ROOT / "src" / "Modules" / "BECore" / "UI"
STEP_FILE = TOOL_DIR / "steps" / "s0004_ui_text_image_dropdown_to_components.py"

_spec = importlib.util.spec_from_file_location("s0004_ui_text_image_dropdown_to_components_under_test", STEP_FILE)
step_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(step_module)
shared = step_module.shared  # the mapping is in steps/_ui_text.py

BEFORE_FILES = sorted(FIXTURES.glob("*.before.xml"))
PAIRS = [p for p in BEFORE_FILES if p.with_name(p.name.replace(".before.", ".after.")).exists()]
FAILURES = [p for p in BEFORE_FILES if p not in PAIRS]

EXPECTED_REASONS = {
    "fail_label_components": ['label "Hinted"', "<components> already", "by hand"],
    "fail_dropdown_components": ['dropdown "Level"', "<components> already"],
    "fail_set_text": ['label "Title"', "variant sets text"],
    "fail_set_sprite": ['picture "Star"', "variant sets sprite"],
    "fail_set_dropdown_text": ['dropdown "Level"', "variant sets overflow"],
    "fail_set_tint_under_clicker": ['picture "PlayIcon"', "variant sets tint"],
}


def after_of(before: Path) -> Path:
    return before.with_name(before.name.replace(".before.", ".after."))


def apply(text: str):
    doc = xmltext.parse(text)
    ctx = migrate.StepContext()
    step_module.run(doc, ctx)
    return doc.to_text(), ctx


def id_of(path: Path) -> str:
    return path.name.split(".")[0]


def read(case: str, side: str = "before") -> str:
    return (FIXTURES / f"{case}.{side}.xml").read_text(encoding="utf-8")


def test_every_fixture_is_a_pair_or_a_failure_and_the_cases_are_there():
    names = {id_of(p) for p in BEFORE_FILES}
    for case in ("label_all_attributes", "label_no_attributes", "label_token_color", "picture", "dropdown_mahjong", "dropdown_placeholder", "nested", "scene_config", "already_composed", "picture_under_clicker"):
        assert case in names
    assert {id_of(p) for p in FAILURES} == set(EXPECTED_REASONS)
    assert not list(FIXTURES.glob("fail_*.after.xml"))


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_the_output_is_exactly_the_after_file(before):
    output, ctx = apply(before.read_text(encoding="utf-8"))
    assert not ctx.failures
    assert output == after_of(before).read_text(encoding="utf-8")  # the stamp is the tool's business: the fixtures keep the one of the `before`


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_the_after_file_migrates_to_itself(before):
    after = after_of(before).read_text(encoding="utf-8")
    output, ctx = apply(after)
    assert output == after
    assert not ctx.notes and not ctx.failures


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_no_label_or_picture_class_is_left_and_a_dropdown_keeps_its_type(before):
    doc = xmltext.parse(before.read_text(encoding="utf-8"))
    out = xmltext.parse(apply(before.read_text(encoding="utf-8"))[0])
    assert not out.find_all(lambda e: e.get("type") in (shared.LABEL_TYPE, shared.IMAGE_TYPE))
    assert [e.get("name") for e in out.find_all(lambda e: e.get("type") == "UIDropdown")] == [e.get("name") for e in doc.find_all(lambda e: e.get("type") == "UIDropdown")]


def test_one_note_per_element_and_a_composed_file_only_keeps_its_text():
    assert len(apply(read("scene_config"))[1].notes) == 3
    assert len(apply(read("label_token_color"))[1].notes) == 3
    text = read("already_composed")
    output, ctx = apply(text)
    assert output == text and not ctx.notes and not ctx.failures


def test_the_text_attributes_move_in_one_fixed_order_with_the_values_as_written():
    output, _ = apply(read("label_all_attributes"))
    component = xmltext.parse(output).find_all(lambda e: e.get("type") == "UITextComponent")[0]
    assert component.attribute_names() == ["type"] + list(shared.TEXT_ATTRIBUTES)
    assert component.get("textShadow") == "2,2,0,0,0,200" and component.get("lineSpacing") == "1.2"
    element = xmltext.parse(output).find_all(lambda e: e.get("name") == "Full")[0]
    assert element.attribute_names() == ["type", "name", "x", "y", "width", "height", "color", "visible", "flex", "fontSize"]


def test_a_picture_under_a_clicker_gets_its_state_tints_equal_to_its_tint_and_one_beside_it_does_not():
    out = xmltext.parse(apply(read("picture_under_clicker"))[0])
    components = {e.parent.parent.get("name"): e for e in out.find_all(lambda e: e.get("type") == "UIImageComponent")}
    states = list(shared.STATE_TINTS)
    expected = {"PungTile0": "255,255,255,255", "Chow0Tile0": "255,240,200,255", "Chow0Tile1": "@text"}  # no tint written: the default; a literal; a theme token, through a stack
    for name, tint in expected.items():
        assert [components[name].get(n) for n in states] == [tint] * 3
        assert components[name].get("tint") in (None, tint)
    assert [components["Beside"].has(n) for n in states] == [False] * 3
    assert len(apply(read("picture_under_clicker"))[1].notes) == 4


def test_a_label_without_any_text_attribute_is_still_given_its_component():
    out = xmltext.parse(apply(read("label_no_attributes"))[0])
    assert [c.attribute_names() for c in out.find_all(lambda e: e.get("type") == "UITextComponent")] == [["type"], ["type"]]


def test_a_dropdown_gets_the_constructor_defaults_unless_the_file_names_them_and_fades_while_disabled():
    out = xmltext.parse(apply(read("dropdown_placeholder"))[0])
    components = {e.parent.parent.get("name"): e for e in out.find_all(lambda e: e.get("type") == "UITextComponent") if e.parent.parent.get("type") == "UIDropdown"}
    assert (components["Level"].get("verticalAlign"), components["Level"].get("wrap"), components["Level"].get("align")) == ("Center", "true", "Center")
    assert (components["Bare"].get("verticalAlign"), components["Bare"].get("wrap"), components["Bare"].get("text")) == ("Center", "false", None)
    assert all(c.get("fadeWhenDisabled") == "true" for c in components.values())
    dropdown = out.find_all(lambda e: e.get("name") == "Level" and e.get("type") == "UIDropdown")[0]
    assert dropdown.get("arrowColor") == "255,255,255,255" and dropdown.get("disabled") == "true" and not dropdown.has("textColor") and not dropdown.has("text")


def test_a_placeholder_of_the_empty_string_stays_written():
    out = xmltext.parse(apply(read("dropdown_mahjong"))[0])
    component = out.find_all(lambda e: e.get("type") == "UITextComponent" and e.parent.parent.get("type") == "UIDropdown")[0]
    assert component.has("text") and component.get("text") == ""


def test_a_variant_that_sets_a_size_does_not_refuse():
    text = read("fail_set_text").replace(' text="Short"', "")
    output, ctx = apply(text)
    assert not ctx.failures and len(ctx.notes) == 1
    assert '<set element="Title" x="0" width="90%" fontSize="30" />' in output


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failure_names_the_element_and_the_line(before):
    text = before.read_text(encoding="utf-8")
    _, ctx = apply(text)
    assert len(ctx.failures) == 1
    for part in EXPECTED_REASONS[id_of(before)]:
        assert part in ctx.failures[0].text
    line = text.splitlines()[ctx.failures[0].line - 1]
    assert re.search(r'type="UI(Label|Image|Dropdown)"', line)


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failed_file_stays_byte_for_byte_and_the_run_exits_2(before, tmp_path, capsys):
    game = tmp_path / "game"
    target = game / "config" / "nodes" / "X.xml"
    target.parent.mkdir(parents=True)
    target.write_bytes(before.read_bytes())
    code = migrate.main([str(game)])
    captured = capsys.readouterr()
    assert code == 2
    assert target.read_bytes() == before.read_bytes()
    assert "config/nodes/X.xml:" in captured.err


def test_the_tool_runs_step_4_on_a_file_stamped_4_stamps_it_5_and_a_second_run_finds_nothing(tmp_path, capsys):
    game = tmp_path / "game"
    (game / "config").mkdir(parents=True)
    target = game / "config" / "SceneConfig.xml"
    target.write_bytes((FIXTURES / "scene_config.before.xml").read_bytes())
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert "config/SceneConfig.xml: 4 to 5 (ui-text-image-dropdown-to-components): 3 elements" in out and "to 4 (" not in out
    assert target.read_text(encoding="utf-8") == read("scene_config", "after").replace('contentVersion="4"', 'contentVersion="5"', 1)
    assert migrate.main([str(game)]) == 0
    assert "SceneConfig.xml" not in capsys.readouterr().out
    assert migrate.main([str(game), "--check"]) == 0


# ---- the lists against the C++ classes (`UILabel` and `UIImage` were removed; their fields are pinned from the classes at the removal) --------------------------------

_FIELD = re.compile(r"BE_REFLECT_FIELD\s+([^;]*?)\s*(?:=|\{|;)", re.DOTALL)


LABEL_FIELDS = {
    "text", "font", "fontFamily", "fontWeight", "textColor", "align", "verticalAlign", "overflow", "wrap", "lineSpacing", "minFontSize", "textEffects",
}  # fmt: skip
IMAGE_FIELDS = {"sprite", "tint", "stretch"}


def reflected(header: str) -> set:
    text = (UI_SOURCES / header).read_text(encoding="utf-8")
    names = set()
    for match in _FIELD.finditer(text):
        declaration = re.sub(r"<[^<>]*>", "", match.group(1)).split()
        names.add(declaration[-1].lstrip("*&"))
    return names


def test_every_reflected_field_of_the_three_classes_is_moved_by_the_step_or_listed_as_staying():
    label = LABEL_FIELDS
    image = IMAGE_FIELDS
    dropdown = reflected("UIDropdown.h")
    text_component = reflected("UITextComponent.h")
    image_component = reflected("UIImageComponent.h")
    assert label and image and dropdown and text_component and image_component

    effects = {"textOutline", "textShadow"}  # the two attributes of the field `textEffects`
    assert "textEffects" in label and "textEffects" in text_component
    assert set(shared.TEXT_ATTRIBUTES) == (label - {"textEffects"}) | effects
    assert set(shared.TEXT_ATTRIBUTES) <= (text_component - {"textEffects"}) | effects
    assert set(shared.IMAGE_ATTRIBUTES) == image
    assert set(shared.IMAGE_ATTRIBUTES) <= image_component
    # the dropdown is a plain element with a text component now: all its own fields stay on it
    assert dropdown == set(shared.DROPDOWN_ATTRIBUTES)
    element = reflected("UIElement.h")
    assert (set(shared.TEXT_ATTRIBUTES) | set(shared.IMAGE_ATTRIBUTES)) & element == set()


def test_the_defaults_written_for_a_dropdown_are_those_its_header_had():
    # The constructor of `UIDropdown` set these two while the header was a `UILabel`; it no longer does (the header is a `UITextComponent`), so the values are pinned here
    # and the dropdown tests (`UIDropdownTest`, `UIDropdownScreenTest`) hold the same two in their `<components>`
    assert dict(shared.DROPDOWN_TEXT_DEFAULTS) == {"verticalAlign": "Center", "wrap": "false"}
