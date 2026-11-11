"""The step 1 to 2 (steps/s0001_ui_button_to_components.py) over the fixture pairs of tests/fixtures/ui_button/: the output is exactly the `after` file, the
`after` migrates to itself, a failure fixture (a `before` with no `after`) fails with a reason that names the element and leaves the file as it was. The C++ twin test
(tests/BECore/ContentMigrationButtonTest.cpp) reads the same files and compares what the two forms do."""

import importlib.util
import re
from pathlib import Path

import pytest

import migrate
import xmltext
from conftest import ENGINE_ROOT, TESTS_DIR, TOOL_DIR

FIXTURES = TESTS_DIR / "fixtures" / "ui_button"
UI_SOURCES = ENGINE_ROOT / "src" / "Modules" / "BECore" / "UI"
STEP_FILE = TOOL_DIR / "steps" / "s0001_ui_button_to_components.py"

_spec = importlib.util.spec_from_file_location("s0001_ui_button_to_components_under_test", STEP_FILE)
step_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(step_module)

BEFORE_FILES = sorted(FIXTURES.glob("*.before.xml"))
PAIRS = [p for p in BEFORE_FILES if p.with_name(p.name.replace(".before.", ".after.")).exists()]
FAILURES = [p for p in BEFORE_FILES if p not in PAIRS]


def after_of(before: Path) -> Path:
    return before.with_name(before.name.replace(".before.", ".after."))


def apply(text: str):
    doc = xmltext.parse(text)
    ctx = migrate.StepContext()
    step_module.run(doc, ctx)
    return doc.to_text(), ctx


def id_of(path: Path) -> str:
    return path.name.split(".")[0]


def test_every_fixture_is_a_pair_or_a_failure_and_the_cases_are_there():
    names = {id_of(p) for p in BEFORE_FILES}
    for case in ("mahjong_hints_button", "mahjong_hand_over_action", "mahjong_setup", "match3_menu_hud", "blocker", "text_attributes", "disabled_tooltip", "with_children", "icon_only", "already_composed"):
        assert case in names
    assert {id_of(p) for p in FAILURES} == {"fail_icon_with_text", "fail_set_text", "fail_interactive_off", "fail_has_components"}
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
def test_nothing_of_a_button_is_left_where_it_was_but_the_buttons(before):
    """Every UIButton of a `before` file is composed, except a segment of a group, which the step leaves."""
    doc = xmltext.read(before)
    out = xmltext.parse(apply(before.read_text(encoding="utf-8"))[0])
    left = [e for e in out.find_all(lambda e: e.get("type") == "UIButton")]
    expected = [e for e in doc.find_all(lambda e: e.get("type") == "UIButton") if step_module._group_segment(e)]
    assert [e.get("name") for e in left] == [e.get("name") for e in expected]


EXPECTED_REASONS = {
    "fail_icon_with_text": ['button "Play"', "icon and a caption", "by hand"],
    "fail_set_text": ['button "Go"', "variant sets text"],
    "fail_interactive_off": ['button "Dead"', 'interactive="false"'],
    "fail_has_components": ['button "Mixed"', "<components> already"],
}


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failure_names_the_element_and_the_line(before):
    _, ctx = apply(before.read_text(encoding="utf-8"))
    assert len(ctx.failures) == 1
    failure = ctx.failures[0]
    for part in EXPECTED_REASONS[id_of(before)]:
        assert part in failure.text
    line = before.read_text(encoding="utf-8").splitlines()[failure.line - 1]
    assert 'type="UIButton"' in line


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


def test_the_tool_migrates_a_game_with_the_real_step_and_stamps_every_file(tmp_path, capsys):
    game = tmp_path / "game"
    nodes = game / "config" / "nodes"
    nodes.mkdir(parents=True)
    (nodes / "Hints.xml").write_bytes((FIXTURES / "mahjong_hints_button.before.xml").read_bytes())
    (nodes / "Done.xml").write_bytes((FIXTURES / "already_composed.before.xml").read_bytes())
    (game / "config" / "SceneConfig.xml").write_bytes((FIXTURES / "match3_menu_hud.before.xml").read_bytes())
    assert migrate.main([str(game), "--check"]) == 1
    capsys.readouterr()
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert "config/nodes/Hints.xml: 1 to 2 (ui-button-to-components): 1 element" in out
    assert "config/SceneConfig.xml: 1 to 2 (ui-button-to-components): 4 elements" in out
    hints = (nodes / "Hints.xml").read_text(encoding="utf-8")
    assert '<node name="HintsButton" layer="6" contentVersion="2">' in hints
    assert hints.replace(' contentVersion="2"', "", 1) == (FIXTURES / "mahjong_hints_button.after.xml").read_text(encoding="utf-8")
    assert (nodes / "Done.xml").read_text(encoding="utf-8").count('contentVersion="2"') == 1
    assert migrate.main([str(game), "--check"]) == 0


def test_line_endings_and_the_missing_final_newline_stay(tmp_path):
    text = '<node name="N">\r\n  <components>\r\n    <element type="UIButton" name="B" text="Go" action="A"/>\r\n  </components>\r\n</node>'
    output, ctx = apply(text)
    assert not ctx.failures
    assert output == (
        '<node name="N">\r\n  <components>\r\n    <element type="UIElement" name="B">\r\n      <components>\r\n'
        '        <component type="UITextComponent" text="Go" align="Center" verticalAlign="Center" wrap="false"/>\r\n'
        '        <component type="UIInputClicker" action="A"/>\r\n      </components>\r\n    </element>\r\n  </components>\r\n</node>'
    )


def test_a_set_of_the_layout_on_a_button_is_fine_and_a_set_of_another_canvas_is_not_looked_at():
    text = (
        "<node><components>"
        '<component type="UICanvasComponent"><elements><element type="UIButton" name="Go" text="Go"/></elements>'
        '<variants><variant><overrides><set element="Go" x="1" width="50%" fontSize="30" visible="false"/></overrides></variant></variants></component>'
        '<component type="UICanvasComponent"><variants><variant><overrides><set element="Go" text="Other canvas"/></overrides></variant></variants></component>'
        "</components></node>"
    )
    _, ctx = apply(text)
    assert not ctx.failures and len(ctx.notes) == 1


def test_an_unnamed_button_is_composed_and_its_icon_child_has_no_name():
    output, ctx = apply('<node><element type="UIButton" icon="i" width="48" height="48"/></node>')
    assert not ctx.failures
    assert "name=" not in output
    assert 'sprite="i"' in output


def test_the_icon_keeps_what_the_old_button_drew():
    """A transparent iconTint falls back to textColor, a tint with an alpha wins, a size of 0 or a malformed one is the share of the rect."""
    cases = {
        '<element type="UIButton" icon="i" iconTint="9,9,9,0" textColor="1,2,3,255"/>': 'tint="1,2,3,255"',
        '<element type="UIButton" icon="i" iconTint="9,9,9,200" textColor="1,2,3,255"/>': 'tint="9,9,9,200"',
        '<element type="UIButton" icon="i" iconTint="#102030"/>': 'tint="#102030"',
        '<element type="UIButton" icon="i" iconSize="0"/>': 'width="60%" height="60%"',
        '<element type="UIButton" icon="i" iconSize="-4"/>': 'width="60%" height="60%"',
        '<element type="UIButton" icon="i" iconSize="30"/>': 'width="30" height="30" maxWidth="100%" maxHeight="100%"',
    }
    for before, expected in cases.items():
        output, ctx = apply(f"<node>{before}</node>")
        assert not ctx.failures
        assert expected in output, before
    output, _ = apply('<node><element type="UIButton" icon="i"/></node>')
    assert "tint=" not in output


# ---- the lists against the C++ classes --------------------------------------------------------------------------------------

_FIELD = re.compile(r"BE_REFLECT_FIELD\s+([^;]*?)\s*(?:=|\{|;)", re.DOTALL)


def reflected(header: str) -> set:
    text = (UI_SOURCES / header).read_text(encoding="utf-8")
    names = set()
    for match in _FIELD.finditer(text):
        declaration = re.sub(r"<[^<>]*>", "", match.group(1)).split()
        names.add(declaration[-1].lstrip("*&"))
    return names


def test_the_lists_of_the_step_are_the_reflected_fields_of_the_classes():
    label = reflected("UILabel.h")
    button = reflected("UIButton.h")
    text_component = reflected("UITextComponent.h")
    clicker = reflected("UIInputClicker.h")
    element = reflected("UIElement.h")
    assert label and button and text_component and clicker and element

    effects = {"textOutline", "textShadow"}  # the two attributes of the field `textEffects`
    assert "textEffects" in label and "textEffects" in text_component
    assert set(step_module.TEXT_ATTRIBUTES) == (label - {"textEffects"}) | effects
    assert set(step_module.TEXT_ATTRIBUTES) <= (text_component - {"textEffects"}) | effects
    assert set(step_module.CLICKER_ATTRIBUTES) == clicker
    assert "text" in reflected("UITooltipComponent.h") and step_module.TOOLTIP_ATTRIBUTE == "tooltip"
    # every field the button adds to its label is accounted for: a clicker field, the tooltip or the icon
    assert button == set(step_module.CLICKER_ATTRIBUTES) | {step_module.TOOLTIP_ATTRIBUTE} | set(step_module.ICON_ATTRIBUTES)
    # what stays on the element is not moved, except `interactive`, which the clicker replaces
    moved = set(step_module.TEXT_ATTRIBUTES) | set(step_module.CLICKER_ATTRIBUTES) | set(step_module.ICON_ATTRIBUTES) | {step_module.TOOLTIP_ATTRIBUTE}
    assert moved & element == set()
    assert set(step_module.DROPPED_ATTRIBUTES) == {"interactive"} and "interactive" in element
    assert {n for n, _ in step_module.BUTTON_TEXT_DEFAULTS} <= set(step_module.TEXT_ATTRIBUTES)


def test_the_defaults_written_are_those_of_the_constructor_of_uibutton():
    source = (UI_SOURCES / "UIButton.cpp").read_text(encoding="utf-8")
    constructor = source[source.index("UIButton::UIButton()") :].split("}", 1)[0]
    assert "align = TextAlign::Center" in constructor
    assert "verticalAlign = UIVerticalAlign::Center" in constructor
    assert "wrap = false" in constructor
    assert dict(step_module.BUTTON_TEXT_DEFAULTS) == {"align": "Center", "verticalAlign": "Center", "wrap": "false"}


def test_the_icon_share_is_that_of_uibutton():
    header = (UI_SOURCES / "UIButton.h").read_text(encoding="utf-8")
    assert "kIconOnlyShare = 0.6f" in header
    assert step_module.ICON_ONLY_SHARE == "60%"
