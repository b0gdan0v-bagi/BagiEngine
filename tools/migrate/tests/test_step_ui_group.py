"""The step 2 to 3 (steps/s0002_ui_group_children_to_components.py) over the fixture pairs of tests/fixtures/ui_group/: the output is exactly the `after` file, the
`after` migrates to itself, a failure fixture (a `before` with no `after`) fails with a reason that names the element and leaves the file as it was. The C++ twin test
(tests/BECore/ContentMigrationGroupTest.cpp) reads the same files and compares what the two forms do."""

import importlib.util
from pathlib import Path

import pytest

import migrate
import xmltext
from conftest import TESTS_DIR, TOOL_DIR

FIXTURES = TESTS_DIR / "fixtures" / "ui_group"
BUTTON_FIXTURES = TESTS_DIR / "fixtures" / "ui_button"


def load(file: str, name: str):
    spec = importlib.util.spec_from_file_location(name, TOOL_DIR / "steps" / file)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


step_one = load("s0001_ui_button_to_components.py", "s0001_under_test")
step_two = load("s0002_ui_group_children_to_components.py", "s0002_under_test")
shared = step_two.shared

BEFORE_FILES = sorted(FIXTURES.glob("*.before.xml"))
PAIRS = [p for p in BEFORE_FILES if p.with_name(p.name.replace(".before.", ".after.")).exists()]
FAILURES = [p for p in BEFORE_FILES if p not in PAIRS]


def after_of(before: Path) -> Path:
    return before.with_name(before.name.replace(".before.", ".after."))


def apply(text: str, *modules):
    doc = xmltext.parse(text)
    ctx = migrate.StepContext()
    for module in modules or (step_two,):
        module.run(doc, ctx)
    return doc.to_text(), ctx


def id_of(path: Path) -> str:
    return path.name.split(".")[0]


def without_comment(text: str) -> str:
    return text[: text.index("<!--")] + text[text.index("-->") + 3 :]


def group_children(doc: xmltext.Document):
    return doc.find_all(lambda e: e.name == "element" and e.get("type") == "UIButton" and shared.is_group_child(e))


def test_every_fixture_is_a_pair_or_a_failure_and_the_cases_are_there():
    names = {id_of(p) for p in BEFORE_FILES}
    for case in ("mahjong_setup", "segmented_plain", "segmented_icon", "dropdown_options", "dropdown_disabled_option", "plain_button_beside_a_group", "already_composed"):
        assert case in names
    assert {id_of(p) for p in FAILURES} == {"fail_segment_icon_above", "fail_option_has_components", "fail_segment_variant_text"}
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
def test_no_button_is_left_as_a_child_of_a_group(before):
    out = xmltext.parse(apply(before.read_text(encoding="utf-8"))[0])
    assert not group_children(out)


def test_a_button_that_is_not_a_group_child_is_not_matched():
    before = FIXTURES / "plain_button_beside_a_group.before.xml"
    output, ctx = apply(before.read_text(encoding="utf-8"))
    left = xmltext.parse(output).find_all(lambda e: e.get("type") == "UIButton")
    assert [e.get("name") for e in left] == ["Close", "Inside"]
    assert len(ctx.notes) == 3  # A, B and One


def test_a_dropdown_option_has_a_clicker_with_a_value_and_no_action():
    output, _ = apply((FIXTURES / "dropdown_options.before.xml").read_text(encoding="utf-8"))
    clickers = xmltext.parse(output).find_all(lambda e: e.get("type") == "UIInputClicker")
    assert len(clickers) == 5
    assert all(c.has("value") and not c.has("action") for c in clickers)


def test_selected_color_stays_on_the_element_and_selected_is_not_written():
    output, _ = apply((FIXTURES / "segmented_plain.before.xml").read_text(encoding="utf-8"))
    out = xmltext.parse(output)
    segments = out.find_all(lambda e: e.get("name") in ("LanguageEn", "SpeedFast"))
    assert len(segments) == 2 and all(e.get("selectedColor") == "40,110,60,255" for e in segments)
    assert "selected=" not in output


EXPECTED_REASONS = {
    "fail_segment_icon_above": ['button "Stacked"', 'iconPlacement="Above"', "by hand"],
    "fail_option_has_components": ['button "Mixed"', "<components> already"],
    "fail_segment_variant_text": ['button "A"', "variant sets text"],
}


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failure_names_the_element_and_the_line(before):
    text = before.read_text(encoding="utf-8")
    _, ctx = apply(text)
    assert len(ctx.failures) == 1
    for part in EXPECTED_REASONS[id_of(before)]:
        assert part in ctx.failures[0].text
    assert 'type="UIButton"' in text.splitlines()[ctx.failures[0].line - 1]


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


def test_a_file_at_version_1_goes_through_both_steps_to_the_fully_composed_text():
    source = (BUTTON_FIXTURES / "mahjong_setup.before.xml").read_text(encoding="utf-8")
    output, ctx = apply(source, step_one, step_two)
    assert not ctx.failures
    expected = (FIXTURES / "mahjong_setup.after.xml").read_text(encoding="utf-8")
    assert without_comment(output) == without_comment(expected)  # the two files differ in their header comment only
    assert not xmltext.parse(output).find_all(lambda e: e.get("type") == "UIButton")


def test_the_tool_runs_step_2_only_on_a_file_stamped_2_and_both_on_a_file_stamped_1(tmp_path, capsys):
    game = tmp_path / "game"
    nodes = game / "config" / "nodes"
    nodes.mkdir(parents=True)
    stamped = (FIXTURES / "segmented_plain.before.xml").read_text(encoding="utf-8").replace('<node name="Options" layer="30">', '<node name="Options" layer="30" contentVersion="2">', 1)
    assert 'contentVersion="2"' in stamped
    (nodes / "Two.xml").write_text(stamped, encoding="utf-8")
    (nodes / "One.xml").write_bytes((BUTTON_FIXTURES / "mahjong_setup.before.xml").read_bytes())
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert "config/nodes/Two.xml: 2 to 3 (ui-group-children-to-components): 5 elements" in out
    assert "config/nodes/One.xml: 1 to 2 (ui-button-to-components)" in out and "config/nodes/One.xml: 2 to 3 (ui-group-children-to-components): 4 elements" in out
    assert migrate.main([str(game), "--check"]) == 0
    assert ' contentVersion="3"' in (nodes / "Two.xml").read_text(encoding="utf-8")
