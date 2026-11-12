"""The step 3 to 4 (steps/s0003_ui_toggle_slider_to_components.py) over the fixture pairs of tests/fixtures/ui_value/: the output is exactly the `after` file, the `after` migrates
to itself, a failure fixture (a `before` with no `after`) fails with a reason that names the element and leaves the file as it was. The C++ test (tests/BECore/ContentMigrationAfterTest.cpp) loads the `after` files and checks the composed form."""

import importlib.util
from pathlib import Path

import pytest

import migrate
import xmltext
from conftest import TESTS_DIR, TOOL_DIR

FIXTURES = TESTS_DIR / "fixtures" / "ui_value"
BUTTON_FIXTURES = TESTS_DIR / "fixtures" / "ui_button"
GROUP_FIXTURES = TESTS_DIR / "fixtures" / "ui_group"


def load(file: str, name: str):
    spec = importlib.util.spec_from_file_location(name, TOOL_DIR / "steps" / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


step_one = load("s0001_ui_button_to_components.py", "s0001_under_test")
step_two = load("s0002_ui_group_children_to_components.py", "s0002_under_test")
step_three = load("s0003_ui_toggle_slider_to_components.py", "s0003_under_test")
shared = step_three.shared

BEFORE_FILES = sorted(FIXTURES.glob("*.before.xml"))
PAIRS = [p for p in BEFORE_FILES if p.with_name(p.name.replace(".before.", ".after.")).exists()]
FAILURES = [p for p in BEFORE_FILES if p not in PAIRS]


def after_of(before: Path) -> Path:
    return before.with_name(before.name.replace(".before.", ".after."))


def apply(text: str, *modules):
    doc = xmltext.parse(text)
    ctx = migrate.StepContext()
    for module in modules or (step_three,):
        module.run(doc, ctx)
    return doc.to_text(), ctx


def id_of(path: Path) -> str:
    return path.name.split(".")[0]


def classes_left(doc: xmltext.Document):
    return doc.find_all(lambda e: e.get("type") in ("UIToggle", "UISlider"))


def test_every_fixture_is_a_pair_or_a_failure_and_the_cases_are_there():
    names = {id_of(p) for p in BEFORE_FILES}
    for case in ("toggle_plain", "toggle_options", "toggle_literal_color", "toggle_token_color", "slider_full", "slider_disabled", "beside_a_set", "mahjong_declare", "already_composed"):
        assert case in names
    assert {id_of(p) for p in FAILURES} == set(EXPECTED_REASONS)
    assert not list(FIXTURES.glob("fail_*.after.xml"))


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_the_output_is_exactly_the_after_file(before):
    output, ctx = apply(before.read_text(encoding="utf-8"))
    assert not ctx.failures
    assert output == after_of(before).read_text(encoding="utf-8")


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_the_after_file_migrates_to_itself(before):
    after = after_of(before).read_text(encoding="utf-8")
    output, ctx = apply(after)
    assert output == after
    assert not ctx.notes and not ctx.failures


@pytest.mark.parametrize("before", PAIRS, ids=id_of)
def test_no_toggle_or_slider_class_is_left(before):
    out = xmltext.parse(apply(before.read_text(encoding="utf-8"))[0])
    assert not classes_left(out)


def test_one_note_per_element_and_a_file_with_none_only_keeps_its_text():
    _, ctx = apply((FIXTURES / "toggle_literal_color.before.xml").read_text(encoding="utf-8"))
    assert len(ctx.notes) == 5
    text = (FIXTURES / "already_composed.before.xml").read_text(encoding="utf-8")
    output, ctx = apply(text)
    assert output == text and not ctx.notes


def test_a_toggle_named_sound_gets_sound_caption_and_sound_switch_and_an_unnamed_one_unnamed_children():
    output, _ = apply((FIXTURES / "toggle_plain.before.xml").read_text(encoding="utf-8"))
    names = [e.get("name") for e in xmltext.parse(output).find_all(lambda e: e.name == "element")]
    assert names == ["Sound", "SoundCaption", "SoundSwitch", None, None, None]


def test_the_disabled_flag_goes_to_the_clicker_and_the_slider_clicker_keeps_the_plate_still():
    output, _ = apply((FIXTURES / "slider_disabled.before.xml").read_text(encoding="utf-8"))
    clickers = xmltext.parse(output).find_all(lambda e: e.get("type") == "UIInputClicker")
    assert [(c.get("stateLook"), c.get("disabled")) for c in clickers] == [("false", "true"), (None, "1")]


def test_the_derived_track_is_the_text_colour_at_a_third_of_alpha_and_the_thumb_the_text_colour():
    output, _ = apply((FIXTURES / "toggle_literal_color.before.xml").read_text(encoding="utf-8"))
    switches = {e.parent.parent.get("name"): e for e in xmltext.parse(output).find_all(lambda e: e.get("type") == "UISwitchComponent")}
    assert (switches["DarkSwitch"].get("trackColor"), switches["DarkSwitch"].get("thumbColor")) == ("190,196,190,89", "190,196,190,255")
    assert (switches["TranslucentSwitch"].get("trackColor"), switches["TranslucentSwitch"].get("thumbColor")) == ("32,64,96,44", "32,64,96,128")
    assert switches["WhiteSwitch"].attribute_names() == ["type"]
    assert (switches["GivenSwitch"].get("trackColor"), switches["GivenSwitch"].get("thumbColor")) == ("1,2,3,255", "10,20,30,255")
    assert (switches["TransparentSwitch"].get("trackColor"), switches["TransparentSwitch"].get("thumbColor")) == ("10,20,30,89", "10,20,30,255")


def test_a_variant_that_sets_a_size_does_not_refuse():
    output, ctx = apply((FIXTURES / "beside_a_set.before.xml").read_text(encoding="utf-8"))
    assert not ctx.failures and len(ctx.notes) == 2
    assert '<set element="Sound" height="68" width="300" />' in output


def test_with_alpha_cuts_in_single_precision_like_the_cpp_one():
    assert shared.with_alpha((1, 2, 3, 255), 0.35) == (1, 2, 3, 89)  # 255 * 0.35f is 89.25
    assert shared.with_alpha((1, 2, 3, 128), 0.35)[3] == 44
    assert shared.with_alpha((1, 2, 3, 100), 0.35)[3] == 35  # 100 * 0.35f is 35.0 in single precision but 34.99999... in double
    assert shared.with_alpha((1, 2, 3, 200), 0.5)[3] == 100
    assert shared.with_alpha((1, 2, 3, 0), 0.35)[3] == 0


@pytest.mark.parametrize(
    "text,expected",
    [
        ("1,2,3,4", ("literal", (1, 2, 3, 4))),
        (" 1 , 2 ,3,4 ", ("literal", (1, 2, 3, 4))),
        ("#102030", ("literal", (16, 32, 48, 255))),
        ("#10203040", ("literal", (16, 32, 48, 64))),
        ("@text", ("token", "@text")),
        ("1,2,3", None),
        ("1,2,3,256", None),
        ("+1,2,3,4", None),
        ("#12345", None),
        ("red", None),
        ("@", None),
    ],
)
def test_parse_color_reads_the_forms_the_engine_reads(text, expected):
    assert shared.parse_color(text) == expected


EXPECTED_REASONS = {
    "fail_toggle_components": ['toggle "Mixed"', "<components> already"],
    "fail_toggle_children": ['toggle "Parent"', "<children>"],
    "fail_toggle_layout": ['toggle "Padded"', "spacing, padding", "stack"],
    "fail_toggle_interactive_off": ['toggle "Quiet"', 'interactive="false"'],
    "fail_toggle_variant_text": ['toggle "Sound"', "variant sets text"],
    "fail_toggle_token_without_track": ['toggle "Themed"', "theme token", "give a trackColor"],
    "fail_slider_variant_bind": ['slider "Volume"', "variant sets maxValue"],
    "fail_slider_children": ['slider "Volume"', "<children>"],
}


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failure_names_the_element_and_the_line(before):
    text = before.read_text(encoding="utf-8")
    _, ctx = apply(text)
    assert len(ctx.failures) == 1
    for part in EXPECTED_REASONS[id_of(before)]:
        assert part in ctx.failures[0].text
    assert 'type="UI' in text.splitlines()[ctx.failures[0].line - 1] and ("UIToggle" in text.splitlines()[ctx.failures[0].line - 1] or "UISlider" in text.splitlines()[ctx.failures[0].line - 1])


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


def test_a_file_at_version_1_runs_steps_1_2_and_3_in_order(tmp_path, capsys):
    game = tmp_path / "game"
    nodes = game / "config" / "nodes"
    nodes.mkdir(parents=True)
    one = (BUTTON_FIXTURES / "mahjong_setup.before.xml").read_text(encoding="utf-8")
    toggle = '<element type="UIToggle" name="Sound" bind="a" action="A" width="300" height="56" text="Sound" />'
    marker = "<children>"
    assert marker in one
    (nodes / "One.xml").write_text(one.replace(marker, marker + "\n" + toggle, 1), encoding="utf-8")
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert out.index("1 to 2 (ui-button-to-components)") < out.index("2 to 3 (ui-group-children-to-components)") < out.index("3 to 4 (ui-toggle-slider-to-components): 1 element")
    text = (nodes / "One.xml").read_text(encoding="utf-8")
    assert ' contentVersion="4"' in text and not xmltext.parse(text).find_all(lambda e: e.get("type") in ("UIToggle", "UIButton"))
    assert migrate.main([str(game), "--check"]) == 0


def test_the_tool_runs_step_3_only_on_a_file_stamped_3_and_a_second_run_finds_nothing(tmp_path, capsys):
    game = tmp_path / "game"
    nodes = game / "config" / "nodes"
    nodes.mkdir(parents=True)
    target = nodes / "Three.xml"
    target.write_bytes((FIXTURES / "toggle_plain.before.xml").read_bytes())
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert "config/nodes/Three.xml: 3 to 4 (ui-toggle-slider-to-components): 2 elements" in out and "to 3 (" not in out
    assert ' contentVersion="4"' in target.read_text(encoding="utf-8")
    assert migrate.main([str(game)]) == 0
    assert "Three.xml" not in capsys.readouterr().out


def test_a_file_at_3_with_no_toggle_or_slider_only_gets_the_stamp(tmp_path):
    game = tmp_path / "game"
    target = game / "config" / "nodes" / "Plain.xml"
    target.parent.mkdir(parents=True)
    source = (GROUP_FIXTURES / "already_composed.after.xml").read_text(encoding="utf-8")
    stamped = source.replace('layer="5">', 'layer="5" contentVersion="3">', 1) if 'contentVersion="3"' not in source else source
    target.write_text(stamped, encoding="utf-8")
    assert migrate.main([str(game)]) == 0
    assert target.read_text(encoding="utf-8") == stamped.replace('contentVersion="3"', 'contentVersion="4"', 1)
