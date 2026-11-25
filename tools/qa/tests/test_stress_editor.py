"""The editor charters of BagiStress (tools/stress/qa/editor_charter.py) and the invariants members they rely on, with no editor: the constants against the shipped
SceneConfig.xml, the command line's exit codes, and the edit and reload sessions on the stand-in editor (the moves replaced, `qa_fake_game.py` serves `editor_state`
and Save). The real run is a person's or the loop's (`--charter all` on a build with the editor)."""

import copy
import importlib.util
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

import bagi_qa_checks as checks
import bagi_qa_scene_file as scene_file
import scenario
from bagi_qa_checks import InvariantsError, load_invariants

ENGINE = Path(__file__).resolve().parents[3]
INVARIANTS = ENGINE / "tools" / "stress" / "qa" / "invariants.py"
CHARTER = ENGINE / "tools" / "stress" / "qa" / "editor_charter.py"


@pytest.fixture(scope="module")
def module():
    return load_invariants(INVARIANTS)


@pytest.fixture(scope="module")
def charter():
    spec = importlib.util.spec_from_file_location("editor_charter_under_test", CHARTER)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


@pytest.fixture(scope="module")
def shipped(module):
    return scene_file.read_scene_file(ENGINE / module.editor_content_files[0])


def test_the_content_file_the_editor_writes_exists(module):
    assert module.editor_content_files == ("tools/stress/config/SceneConfig.xml",)
    assert (ENGINE / module.editor_content_files[0]).is_file()


def test_every_generated_node_is_a_node_of_its_scene_in_the_shipped_file(module, shipped):
    assert module.editor_generated
    for scene_name, nodes in module.editor_generated.items():
        scene = shipped.scene(scene_name)
        assert scene is not None, scene_name
        present = {node.get("name") for node in scene.iter("node")}
        assert set(nodes) <= present, (scene_name, nodes, present)


def test_the_added_defaults_are_attributes_of_the_transform(module):
    assert module.editor_added_defaults == {"TransformComponent": {"x": "0", "y": "0", "rotation": "0", "pivotX": "0.5", "pivotY": "0.5"}}


@pytest.mark.parametrize(
    "line",
    ['EDITOR_GENERATED = ["Field"]', 'EDITOR_GENERATED = {"Scene": "Field"}', 'EDITOR_GENERATED = {"Scene": [1]}', 'EDITOR_ADDED_DEFAULTS = {"Transform": ["rotation"]}', 'EDITOR_ADDED_DEFAULTS = {"Transform": {"rotation": 0}}'],
)
def test_a_member_of_the_wrong_shape_is_refused_by_the_loader(tmp_path, line):
    file = tmp_path / "invariants.py"
    file.write_text(line + "\n")
    with pytest.raises(InvariantsError):
        load_invariants(file)


def saved_like_the_editor(charter, module, shipped):
    """What the editor's Save writes for the edits of the charter, made by hand from the shipped file: the node renamed, the number and the layer set, the
    defaults added and a generated child under Field."""
    saved = copy.deepcopy(shipped.root)
    scene = next(element for element in saved.iter("scene") if element.get("name") == charter.SCENE_FILE_NAME)
    node = next(element for element in scene.iter("node") if element.get("name") == charter.EDITED_NODE)
    node.set("name", charter.RENAMED_NODE)
    node.set("layer", str(charter.EDITED_LAYER))
    transform = next(element for element in node.iter("component") if element.get("type") == charter.EDITED_COMPONENT)
    transform.set("x", str(charter.EDITED_NUMBER))
    for component in scene.iter("component"):
        for name, value in module.editor_added_defaults.get(component.get("type"), {}).items():
            component.attrib.setdefault(name, value)
    field = next(element for element in scene.iter("node") if element.get("name") == "Field")
    children = ElementTree.SubElement(field, "children")
    ElementTree.SubElement(ElementTree.SubElement(children, "node", name=charter.GENERATED_NODE), "components")
    return saved, scene


def test_the_charters_edits_fit_the_shipped_file_and_what_save_writes_passes_the_compare(charter, module, shipped):
    saved, _ = saved_like_the_editor(charter, module, shipped)
    assert charter.compare(ElementTree.tostring(shipped.root), ElementTree.tostring(saved), module) == []


def test_the_compare_still_sees_a_change_the_charter_did_not_ask_for(charter, module, shipped):
    saved, scene = saved_like_the_editor(charter, module, shipped)
    scene.set("viewWidth", "1")
    found = charter.compare(ElementTree.tostring(shipped.root), ElementTree.tostring(saved), module)
    assert [violation.name for violation in found] == ["editor.saved_scene_changed"]


def test_help_names_the_charters(charter, capsys):
    with pytest.raises(SystemExit) as stop:
        charter.main(["--help"])
    assert stop.value.code == 0
    assert "edit" in capsys.readouterr().out


def test_an_out_folder_inside_the_repository_is_exit_2(charter, capsys, qa_game_exe):
    assert charter.main(["--exe", str(qa_game_exe), "--out", str(ENGINE / "qa-out-never-written")]) == charter.EXIT_USAGE
    assert "inside a git work tree" in capsys.readouterr().out
    assert not (ENGINE / "qa-out-never-written").exists()


def test_a_missing_executable_is_skipped_with_77(charter, tmp_path, capsys):
    assert charter.main(["--exe", str(tmp_path / "nothing"), "--out", str(tmp_path / "out")]) == charter.EXIT_SKIPPED
    assert "SKIPPED" in capsys.readouterr().out


def test_an_executable_of_another_checkout_is_refused_and_the_content_file_is_left_alone(charter, tmp_path, qa_game_exe, capsys):
    content = ENGINE / "tools" / "stress" / "config" / "SceneConfig.xml"
    before = content.read_bytes()
    assert charter.main(["--exe", str(qa_game_exe), "--out", str(tmp_path / "out"), "--charter", "panels"]) == charter.EXIT_USAGE
    assert "not in the checkout" in capsys.readouterr().out
    assert content.read_bytes() == before


def test_no_scenario_of_the_stress_folder_starts_the_editor():
    # be_add_game_scenario_tests plays these through bagi_play.py, which has no guard for the content file an editor's Save writes
    folder = ENGINE / "tools" / "stress" / "tests" / "scenarios"
    files = sorted(folder.glob("*.jsonl"))
    assert files
    assert [file.name for file in files if "--editor" in scenario.load(file)[0].flags] == []


ORIGINAL = "<SceneManager>original</SceneManager>\n"
SAVED = "<SceneManager>saved by the editor</SceneManager>\n"
MOVES = ["widen_left_dock", "open_panel", "select_node", "open_component", "set_number_field", "rename_selected"]


@pytest.fixture
def stand_in(charter, tmp_path, monkeypatch):
    """A checkout of its own (a content file and an invariants module that names it) and the charter's moves replaced by no-ops: what is left is the
    charter's own handling of the overlay on the stand-in editor, whose Save writes the overlay's copy and, with QA_FAKE_WRITE_CHECKOUT, the checkout's."""
    root = tmp_path / "checkout"
    (root / ".claude" / "rules").mkdir(parents=True)
    (root / ".claude" / "rules" / "qa-project.md").write_text("rules\n")
    content_file = root / "config" / "SceneConfig.xml"
    content_file.parent.mkdir()
    content_file.write_text(ORIGINAL, encoding="utf-8")
    invariants = root / "invariants.py"
    invariants.write_text('EDITOR_CONTENT_FILES = ["config/SceneConfig.xml"]\n')
    monkeypatch.setattr(charter, "INVARIANTS", invariants)
    monkeypatch.setattr(charter, "ENGINE_ROOT", root)
    monkeypatch.setattr(charter, "RESET_ENTITIES", {"count": 1})
    monkeypatch.setattr(charter, "compare", lambda before, saved, invariants: [])
    for move in MOVES:
        monkeypatch.setattr(charter.editor, move, lambda *args, **kwargs: None)
    monkeypatch.setattr(charter.editor, "rows_matching", lambda session, text: [text])
    monkeypatch.setenv("QA_FAKE_EDITOR", "1")
    monkeypatch.setenv("QA_FAKE_DIRTY", "1")
    monkeypatch.setenv("QA_FAKE_CONTENT_FILE", str(content_file))
    monkeypatch.setenv("QA_FAKE_SAVE_TEXT", SAVED)
    return content_file, checks.load_invariants(invariants)


def test_the_edit_charter_reads_the_overlays_copy_and_the_checkout_stays_as_it_was(charter, stand_in, qa_game_exe, tmp_path):
    content_file, invariants = stand_in
    played, saved = charter.play_edit(qa_game_exe, tmp_path / "out", content_file, invariants)
    assert played.session.bugs == []
    assert saved == SAVED.encode()
    assert (played.session.content_overlay / "config" / "SceneConfig.xml").read_text() == SAVED
    assert content_file.read_text() == ORIGINAL


def test_the_reload_starts_on_the_overlay_the_edit_saved_into(charter, stand_in, qa_game_exe, tmp_path, monkeypatch):
    content_file, invariants = stand_in
    seen = tmp_path / "seen.txt"
    monkeypatch.setenv("QA_FAKE_OVERLAY_SEEN_OUT", str(seen))
    first, saved = charter.play_edit(qa_game_exe, tmp_path / "out", content_file, invariants)
    assert saved is not None
    assert seen.read_text().strip() == "no"
    second = charter.play_reload(qa_game_exe, tmp_path / "out", first.session.content_overlay)
    assert seen.read_text().strip() == "yes"
    assert second.session.content_overlay == first.session.content_overlay
    assert second.session.bugs == []
    assert content_file.read_text() == ORIGINAL


def test_an_editor_that_ignores_the_overlay_is_a_finding_and_the_file_is_put_back(charter, stand_in, qa_game_exe, tmp_path, monkeypatch):
    content_file, invariants = stand_in
    monkeypatch.setenv("QA_FAKE_WRITE_CHECKOUT", "1")
    played, _ = charter.play_edit(qa_game_exe, tmp_path / "out", content_file, invariants)
    assert [finding.name for finding in played.session.bugs] == ["editor.content_file_written"]
    assert content_file.read_text() == ORIGINAL


def test_a_run_that_leaves_the_checkouts_file_changed_is_exit_1(charter, stand_in, qa_game_exe, tmp_path, monkeypatch):
    content_file, _ = stand_in

    def writes_the_checkout(exe, out):
        content_file.write_text(SAVED)

    monkeypatch.setattr(charter, "play_panels", lambda exe, out: writes_the_checkout(exe, out))
    monkeypatch.setattr(charter, "report", lambda played, out: charter.EXIT_OK)
    assert charter.main(["--exe", str(qa_game_exe), "--out", str(tmp_path / "out"), "--charter", "panels"]) == charter.EXIT_BUG
    monkeypatch.setattr(charter, "play_panels", lambda exe, out: None)
    content_file.write_text(ORIGINAL)
    assert charter.main(["--exe", str(qa_game_exe), "--out", str(tmp_path / "out2"), "--charter", "panels"]) == charter.EXIT_OK
