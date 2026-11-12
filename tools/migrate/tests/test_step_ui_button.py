"""The step 1 to 2 (steps/s0001_ui_button_to_components.py) over the fixture pairs of tests/fixtures/ui_button/: the output is exactly the `after` file, the
`after` migrates to itself, a failure fixture (a `before` with no `after`) fails with a reason that names the element and leaves the file as it was. The C++ test (tests/BECore/ContentMigrationAfterTest.cpp) loads the `after` files and checks the composed form."""

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
shared = step_module.shared  # the mapping and its lists are in steps/_ui_button.py

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
    for case in ("mahjong_hints_button", "mahjong_hand_over_action", "mahjong_setup", "match3_menu_hud", "blocker", "text_attributes", "disabled_tooltip", "with_children", "already_composed", "icon_attributes_without_icon", "long_caption", "icon_only", "icon_text"):
        assert case in names
    assert {id_of(p) for p in FAILURES} == {"fail_icon_above", "fail_icon_with_children", "fail_icon_variant_font_size", "fail_set_text", "fail_interactive_off", "fail_has_components"}
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
    expected = [e for e in doc.find_all(lambda e: e.get("type") == "UIButton") if shared.is_group_child(e)]
    assert [e.get("name") for e in left] == [e.get("name") for e in expected]


EXPECTED_REASONS = {
    "fail_icon_above": ['button "Stacked"', 'iconPlacement="Above"', "by hand"],
    "fail_icon_with_children": ['button "Badged"', "has an icon and <children>", "by hand"],
    "fail_icon_variant_font_size": ['button "Play"', "variant sets fontSize"],
    "fail_set_text": ['button "Go"', "variant sets text"],
    "fail_interactive_off": ['button "Dead"', 'interactive="false"'],
    "fail_has_components": ['button "Mixed"', "<components> already"],
}


@pytest.mark.parametrize("before", FAILURES, ids=id_of)
def test_a_failure_names_the_element_and_the_line(before):
    text = before.read_text(encoding="utf-8")
    _, ctx = apply(text)
    assert len(ctx.failures) == (2 if id_of(before) == "fail_icon_with_children" else 1)  # every refused button is reported
    failure = ctx.failures[0]
    for part in EXPECTED_REASONS[id_of(before)]:
        assert part in failure.text
    for each in ctx.failures:
        assert 'type="UIButton"' in text.splitlines()[each.line - 1]


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


def test_the_tool_migrates_a_game_with_the_real_steps_and_stamps_every_file(tmp_path, capsys):
    game = tmp_path / "game"
    nodes = game / "config" / "nodes"
    nodes.mkdir(parents=True)
    (nodes / "Hints.xml").write_bytes((FIXTURES / "mahjong_hints_button.before.xml").read_bytes())
    (nodes / "Done.xml").write_bytes((TESTS_DIR / "fixtures" / "ui_group" / "already_composed.after.xml").read_bytes())
    (game / "config" / "SceneConfig.xml").write_bytes((FIXTURES / "match3_menu_hud.before.xml").read_bytes())
    assert migrate.main([str(game), "--check"]) == 1
    capsys.readouterr()
    assert migrate.main([str(game)]) == 0
    out = capsys.readouterr().out
    assert "config/nodes/Hints.xml: 1 to 2 (ui-button-to-components): 1 element" in out
    assert "config/SceneConfig.xml: 1 to 2 (ui-button-to-components): 4 elements" in out
    hints = (nodes / "Hints.xml").read_text(encoding="utf-8")
    stamp = f' contentVersion="{migrate.build_registry().latest(migrate.ENGINE_TRACK)}"'
    assert f'<node name="HintsButton" layer="6"{stamp}>' in hints
    assert hints.replace(stamp, "", 1) == (FIXTURES / "mahjong_hints_button.after.xml").read_text(encoding="utf-8")
    assert (nodes / "Done.xml").read_text(encoding="utf-8").count(stamp) == 1
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


def test_a_set_of_the_layout_on_an_icon_button_is_fine_and_a_set_of_its_font_size_is_only_fine_without_a_caption():
    def overrides(attributes: str, button: str) -> str:
        return (
            f'<node><components><component type="UICanvasComponent"><elements><element type="UIButton" name="Go" {button}/></elements>'
            f'<variants><variant><overrides><set element="Go" {attributes}/></overrides></variant></variants></component></components></node>'
        )

    _, ctx = apply(overrides('x="1" width="50%" visible="false" anchor="Center"', 'icon="i" text="Go"'))
    assert not ctx.failures and len(ctx.notes) == 1
    _, ctx = apply(overrides('fontSize="30"', 'icon="i"'))
    assert not ctx.failures
    for attributes in ('fontSize="30"', 'text="Go!"', 'icon="j"', 'iconTint="1,2,3,4"', 'disabled="true"'):
        _, ctx = apply(overrides(attributes, 'icon="i" text="Go"'))
        assert len(ctx.failures) == 1, attributes


def test_an_icon_only_button_with_a_placement_above_is_composed_but_one_with_a_caption_is_not():
    _, ctx = apply('<node><element type="UIButton" name="B" icon="i" iconPlacement="Above"/></node>')
    assert not ctx.failures
    _, ctx = apply('<node><element type="UIButton" name="B" icon="i" iconPlacement="Above" text="x"/></node>')
    assert len(ctx.failures) == 1


def test_an_empty_text_is_no_caption_and_the_children_of_an_unnamed_button_have_no_name():
    output, ctx = apply('<node><element type="UIButton" icon="i" text=""/></node>')
    assert not ctx.failures
    assert "UITextComponent" not in output and "Caption" not in output and "name=" not in output


def test_the_layout_attributes_of_an_icon_button_are_refused_one_by_one():
    for attribute in ("layout", "direction", "spacing", "padding", "crossAlign", "wrapChildren"):
        _, ctx = apply(f'<node><element type="UIButton" name="B" icon="i" {attribute}="x"/></node>')
        assert len(ctx.failures) == 1 and attribute in ctx.failures[0].text, attribute
    _, ctx = apply('<node><element type="UIButton" name="B" text="x" spacing="4" padding="2"/></node>')
    assert not ctx.failures  # a button without an icon keeps them on the element as before


def test_an_unnamed_button_is_composed():
    output, ctx = apply('<node><element type="UIButton" width="48" height="48"/></node>')
    assert not ctx.failures
    assert "name=" not in output and 'type="UIInputClicker"' in output


# ---- the lists against the C++ classes --------------------------------------------------------------------------------------

# The reflected fields of `UILabel` and of what `UIButton` added to it, taken from the classes when they were removed (slice 5 of `ui-button-composition`); the component headers are still read
LABEL_FIELDS = {
    "text", "font", "fontFamily", "fontWeight", "textColor", "align", "verticalAlign", "overflow", "wrap", "lineSpacing", "minFontSize", "textEffects",
}  # fmt: skip
BUTTON_ADDED_FIELDS = {"action", "value", "disabled", "icon", "iconTint", "iconPlacement", "iconSize", "tooltip"}
# The numbers of `UIButton` at the removal: the forms of an icon (now the constants of `UIImageComponent`) and the defaults of its constructor
BUTTON_CONSTRUCTOR_DEFAULTS = {"align": "Center", "verticalAlign": "Center", "wrap": "false"}

_FIELD = re.compile(r"BE_REFLECT_FIELD\s+([^;]*?)\s*(?:=|\{|;)", re.DOTALL)


def reflected(header: str) -> set:
    text = (UI_SOURCES / header).read_text(encoding="utf-8")
    names = set()
    for match in _FIELD.finditer(text):
        declaration = re.sub(r"<[^<>]*>", "", match.group(1)).split()
        names.add(declaration[-1].lstrip("*&"))
    return names


def test_the_lists_of_the_step_are_the_reflected_fields_of_the_classes():
    label = LABEL_FIELDS
    button = BUTTON_ADDED_FIELDS
    text_component = reflected("UITextComponent.h")
    clicker = reflected("UIInputClicker.h")
    element = reflected("UIElement.h")
    assert label and button and text_component and clicker and element

    effects = {"textOutline", "textShadow"}  # the two attributes of the field `textEffects`
    assert "textEffects" in label and "textEffects" in text_component
    assert set(shared.TEXT_ATTRIBUTES) == (label - {"textEffects"}) | effects
    assert set(shared.TEXT_ATTRIBUTES) <= (text_component - {"textEffects"}) | effects
    assert set(shared.CLICKER_ATTRIBUTES) == clicker - {"stateLook"}  # no button had it: only step 3 writes it, for a slider
    assert "text" in reflected("UITooltipComponent.h") and shared.TOOLTIP_ATTRIBUTE == "tooltip"
    # every field the button adds to its label is accounted for: a clicker field, the tooltip or the icon
    assert button == set(shared.CLICKER_ATTRIBUTES) | {shared.TOOLTIP_ATTRIBUTE} | set(shared.ICON_ATTRIBUTES)
    # what stays on the element is not moved, except `interactive`, which the clicker replaces
    moved = set(shared.TEXT_ATTRIBUTES) | set(shared.CLICKER_ATTRIBUTES) | set(shared.ICON_ATTRIBUTES) | {shared.TOOLTIP_ATTRIBUTE}
    assert moved & element == set()
    assert set(shared.DROPPED_ATTRIBUTES) == {"interactive"} and "interactive" in element
    assert {n for n, _ in shared.BUTTON_TEXT_DEFAULTS} <= set(shared.TEXT_ATTRIBUTES)


def test_the_forms_of_an_icon_are_the_constants_of_the_image_component():
    header = (UI_SOURCES / "UIImageComponent.h").read_text(encoding="utf-8")

    def constant(name: str) -> float:
        return float(re.search(rf"{name}\s*=\s*([0-9.]+)f", header).group(1))

    assert shared.ICON_ONLY_SHARE == f"{round(constant('kIconOnlyShare') * 100)}%"
    assert shared.ICON_WITH_TEXT_SHARE == f"{round(constant('kIconWithTextShare') * 100)}%"
    assert float(shared.ICON_PADDING) == constant("kIconPadding")
    assert float(shared.ICON_GAP) == constant("kIconGap")


def test_the_defaults_written_are_those_the_constructor_of_uibutton_had():
    assert dict(shared.BUTTON_TEXT_DEFAULTS) == BUTTON_CONSTRUCTOR_DEFAULTS
