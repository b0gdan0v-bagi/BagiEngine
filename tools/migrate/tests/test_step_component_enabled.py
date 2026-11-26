"""The step 5 to 6 (steps/s0005_component_enabled.py) over the fixtures of tests/fixtures/component_enabled/: the output is exactly the `after` file, the `after` migrates to
itself, a failure fixture (a `before` with no `after`) fails and leaves the file as it was."""

import importlib.util

import pytest

import migrate
import xmltext
from conftest import TESTS_DIR, TOOL_DIR

FIXTURES = TESTS_DIR / "fixtures" / "component_enabled"
STEP_FILE = TOOL_DIR / "steps" / "s0005_component_enabled.py"

_spec = importlib.util.spec_from_file_location("s0005_component_enabled_under_test", STEP_FILE)
step_module = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(step_module)

PAIRS = ("scene_config", "node_file")
FAILURE = FIXTURES / "fail_game_class.before.xml"


def read(case: str, side: str = "before") -> str:
    return (FIXTURES / f"{case}.{side}.xml").read_text(encoding="utf-8")


def apply(text: str):
    doc = xmltext.parse(text)
    ctx = migrate.StepContext()
    step_module.run(doc, ctx)
    return doc.to_text(), ctx


def node_with(component: str) -> str:
    return f'<node name="N"><components>{component}</components></node>'


@pytest.mark.parametrize("case", PAIRS)
def test_the_output_is_exactly_the_after_file(case):
    output, ctx = apply(read(case))
    assert not ctx.failures
    assert output == read(case, "after")


@pytest.mark.parametrize("case", PAIRS)
def test_the_after_file_migrates_to_itself(case):
    output, ctx = apply(read(case, "after"))
    assert output == read(case, "after")
    assert not ctx.notes and not ctx.failures


def test_one_note_per_component_with_its_type_and_the_value_that_was_written():
    notes = [note.text for note in apply(read("scene_config"))[1].notes]
    assert [text.split(":")[0] for text in notes] == ["TransformComponent", "QuadRendererComponent", "TextComponent", "QuadRendererComponent", "UICanvasComponent"]
    assert 'enabled="False"' in notes[2]


@pytest.mark.parametrize("value", ["true", "1"])
def test_a_value_the_loader_reads_as_true_stays_on_a_class_of_a_game_too(value):
    text = node_with(f'<component type="DoorComponent" enabled="{value}"/>')
    output, ctx = apply(text)
    assert output == text and not ctx.notes and not ctx.failures


@pytest.mark.parametrize(
    "text",
    [
        '<element type="UIElement"><components><component type="UITextComponent" enabled="false"/></components></element>',
        '<node name="N"><component type="QuadRendererComponent" enabled="false"/></node>',
        '<node name="N"><data><components><component type="QuadRendererComponent" enabled="false"/></components></data></node>',
    ],
)
def test_only_a_component_in_the_components_of_a_node_is_matched(text):
    output, ctx = apply(text)
    assert output == text and not ctx.notes and not ctx.failures


@pytest.mark.parametrize(
    "text",
    [
        node_with('<component type="DoorComponent" enabled="0"/>'),
        '<SceneManager><scenes><scene><nodes><node ref="nodes/Door"><components><component type="DoorComponent" enabled="false"/></components></node></nodes></scene></scenes></SceneManager>',
    ],
)
def test_a_class_of_a_game_is_refused_inline_and_in_an_override(text):
    _, ctx = apply(text)
    assert len(ctx.failures) == 1 and not ctx.notes
    assert '"DoorComponent"' in ctx.failures[0].text and "rename the field" in ctx.failures[0].text


def test_a_failed_file_stays_byte_for_byte_and_the_run_names_the_line(tmp_path, capsys):
    target = tmp_path / "config" / "nodes" / "Door.xml"
    target.parent.mkdir(parents=True)
    target.write_bytes(FAILURE.read_bytes())
    assert migrate.main([str(tmp_path)]) == 2
    assert target.read_bytes() == FAILURE.read_bytes()  # the engine component beside it is not written either
    assert 'config/nodes/Door.xml:5: scene-component-enabled: component "DoorComponent"' in capsys.readouterr().err


def test_the_tool_runs_step_5_on_a_file_stamped_5_stamps_it_and_a_second_run_finds_nothing(tmp_path, capsys):
    target = tmp_path / "config" / "SceneConfig.xml"
    target.parent.mkdir()
    target.write_bytes((FIXTURES / "scene_config.before.xml").read_bytes())
    latest = migrate.build_registry().latest(migrate.ENGINE_TRACK)
    assert migrate.main([str(tmp_path)]) == 0
    assert "config/SceneConfig.xml: 5 to 6 (scene-component-enabled): 5 elements" in capsys.readouterr().out
    assert target.read_text(encoding="utf-8") == read("scene_config", "after").replace('contentVersion="5"', f'contentVersion="{latest}"', 1)
    assert migrate.main([str(tmp_path)]) == 0
    assert "SceneConfig.xml" not in capsys.readouterr().out
    assert migrate.main([str(tmp_path), "--check"]) == 0
