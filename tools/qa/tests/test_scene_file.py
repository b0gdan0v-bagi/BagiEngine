import pytest

import bagi_qa_scene_file as scene_file
from bagi_qa_scene_file import ContentGuard, ContentGuardError, SceneFileError, compare_scene_files, read_scene_file

SCENE = """<scene type="Scene" name="{name}" viewWidth="{width}" viewHeight="1080">
  <nodes>
    <node name="Field">
      <components>
        <component type="TransformComponent" x="{x}" y="0" />
        <component type="StressComponent" />
      </components>
      {field_children}
    </node>
    <node name="Run" layer="{layer}">
      <components>
        <component type="TransformComponent" x="0" y="0" />
      </components>
    </node>
  </nodes>
</scene>"""


def scene(name, width="1920", x="0", layer="1", field_children=""):
    return SCENE.format(name=name, width=width, x=x, layer=layer, field_children=field_children)


def document(*scenes, version="5"):
    return f'<?xml version="1.0"?>\n<SceneManager contentVersion="{version}">\n  <!-- a comment -->\n  <scenes>\n' + "\n".join(scenes) + "\n  </scenes>\n</SceneManager>\n"


def base():
    return document(scene("A"), scene("B"))


def names(violations):
    return [violation.name for violation in violations]


def compare(after, edits=(), before=None, scene_name="A", **kwargs):
    return compare_scene_files((before or base()).encode(), after.encode() if isinstance(after, str) else after, scene_name, list(edits), **kwargs)


def test_a_file_equals_itself():
    assert compare(base()) == []


def test_a_comment_with_double_dashes_does_not_make_a_file_unreadable():
    shipped = base().replace("<!-- a comment -->", "<!-- chosen by --stress-scene -->")
    assert compare(shipped.replace("--stress-scene", "- -stress-scene"), before=shipped) == []


def test_comments_and_layout_are_not_changes():
    spaced = base().replace("\n  <scenes>", "\n\n\n<scenes>").replace("<!-- a comment -->", "<!-- another one -->")
    assert compare(spaced) == []


def test_an_attribute_that_changed_without_an_edit_is_a_scene_change():
    (violation,) = compare(document(scene("A", width="1280"), scene("B")))
    assert violation.name == "editor.saved_scene_changed"
    assert "@viewWidth" in violation.message and "'1920'" in violation.message and "'1280'" in violation.message


def test_a_component_attribute_names_the_path_of_the_node():
    (violation,) = compare(document(scene("A", x="5"), scene("B")))
    assert violation.name == "editor.saved_scene_changed"
    assert "Field/TransformComponent@x" in violation.message


def test_a_default_the_writer_adds_is_tolerated_only_with_the_allowed_value():
    plain = '<component type="TransformComponent" x="0" y="0" />'
    written = base().replace(plain, plain.replace("/>", 'rotation="0" />'), 1)
    assert names(compare(written)) == ["editor.saved_scene_changed"]
    assert compare(written, added_defaults={"TransformComponent": {"rotation": "0"}}) == []
    (violation,) = compare(written.replace('rotation="0"', 'rotation="9"'), added_defaults={"TransformComponent": {"rotation": "0"}})
    assert "@rotation" in violation.message and "'9'" in violation.message


def test_a_default_the_writer_leaves_out_is_tolerated_only_with_the_allowed_value():
    plain = '<component type="TransformComponent" x="0" y="0" />'
    written = base().replace(plain, '<component type="TransformComponent" y="0" />', 1)
    assert names(compare(written)) == ["editor.saved_scene_changed"]
    assert compare(written, added_defaults={"TransformComponent": {"x": "0"}}) == []
    assert names(compare(written, added_defaults={"TransformComponent": {"x": "1"}})) == ["editor.saved_scene_changed"]


def test_a_lost_component_is_a_scene_change():
    after = base().replace('        <component type="StressComponent" />\n', "", 1)
    (violation,) = compare(after)
    assert violation.name == "editor.saved_scene_changed" and "Field/StressComponent" in violation.message


def test_nodes_in_another_order_are_a_scene_change():
    after = document(scene("A").replace('<node name="Field">', '<node name="Tmp">').replace('<node name="Run" layer="1">', '<node name="Field">').replace('<node name="Tmp">', '<node name="Run" layer="1">'), scene("B"))
    assert "editor.saved_scene_changed" in names(compare(after))


def test_the_expected_edits_are_not_changes():
    after = document(scene("A", x="7", layer="3"), scene("B"))
    edits = [
        {"kind": "component-attribute", "path": "Field", "component": "TransformComponent", "name": "x", "value": "7"},
        {"kind": "attribute", "path": "Run", "name": "layer", "value": "3"},
    ]
    assert compare(after, edits) == []


def test_a_missing_edit_is_named_and_not_also_a_change():
    after = document(scene("A"), scene("B"))
    (violation,) = compare(after, [{"kind": "component-attribute", "path": "Field", "component": "TransformComponent", "name": "x", "value": "7"}])
    assert violation.name == "editor.saved_edit_missing"
    assert "'0'" in violation.message and "'7'" in violation.message


def test_an_edit_with_another_value_is_missing_and_an_unrelated_change_still_shows():
    after = document(scene("A", x="8", width="1"), scene("B"))
    found = compare(after, [{"kind": "component-attribute", "path": "Field", "component": "TransformComponent", "name": "x", "value": "7"}])
    assert names(found).count("editor.saved_edit_missing") == 1
    assert any("@viewWidth" in violation.message for violation in found if violation.name == "editor.saved_scene_changed")


def test_a_rename_and_an_added_node_are_edits():
    renamed = scene("A").replace('<node name="Run" layer="1">', '<node name="Go" layer="1">')
    added = renamed.replace("</nodes>", '<node name="New"><components><component type="TransformComponent" x="1" y="1" /></components></node></nodes>')
    edits = [{"kind": "node-name", "path": "Run", "new_name": "Go"}, {"kind": "node-added", "path": "New"}]
    assert compare(document(added, scene("B")), edits) == []
    found = compare(document(renamed, scene("B")), edits)
    assert names(found) == ["editor.saved_edit_missing"] and "New" in found[0].message


def test_a_child_added_under_a_node_is_found_by_its_path():
    child = '<children><node name="Kid"><components /></node></children>'
    after = document(scene("A", field_children=child), scene("B"))
    assert compare(after, [{"kind": "node-added", "path": "Field/Kid"}]) == []
    assert names(compare(after)) == ["editor.saved_scene_changed"]


def test_children_of_ignored_nodes_are_skipped_and_nothing_else():
    children = '<children><node name="Item"><components /></node><node name="Item"><components /></node></children>'
    after = document(scene("A", field_children=children), scene("B"))
    assert compare(after, ignore_children_of={"Field"}) == []
    assert names(compare(after, ignore_children_of={"Run"})) == ["editor.saved_scene_changed"]
    changed = document(scene("A", field_children=children, layer="2"), scene("B"))
    assert names(compare(changed, ignore_children_of={"Field"})) == ["editor.saved_scene_changed"]


def test_another_scene_that_changed_moved_vanished_or_appeared():
    assert names(compare(document(scene("A"), scene("B", width="1")))) == ["editor.saved_other_scene_changed"]
    assert names(compare(document(scene("B"), scene("A")))) == ["editor.saved_other_scene_changed"]
    assert names(compare(document(scene("A")))) == ["editor.saved_other_scene_changed"]
    assert names(compare(document(scene("A"), scene("B"), scene("C")))) == ["editor.saved_other_scene_changed"]


def test_the_root_version_is_the_root_changed():
    (violation,) = compare(document(scene("A"), scene("B"), version="6"))
    assert violation.name == "editor.saved_root_changed" and "contentVersion" in violation.message


def test_a_truncated_file_or_one_without_scenes_is_unreadable():
    assert names(compare(base()[:100])) == ["editor.saved_file_unreadable"]
    assert names(compare(b"")) == ["editor.saved_file_unreadable"]
    assert names(compare('<SceneManager contentVersion="5"><scenes /></SceneManager>')) == ["editor.saved_file_unreadable"]


def test_a_missing_saved_file_is_unreadable(tmp_path):
    assert names(compare_scene_files(base().encode(), tmp_path / "gone.xml", "A", [])) == ["editor.saved_file_unreadable"]


def test_a_request_that_names_nothing_is_an_error_not_a_finding():
    with pytest.raises(SceneFileError):
        compare(base(), [{"kind": "attribute", "path": "Nope", "name": "layer", "value": "1"}])
    with pytest.raises(SceneFileError):
        compare(base(), scene_name="Z")
    with pytest.raises(SceneFileError):
        compare(base(), [{"kind": "node-added", "path": "Field"}])
    with pytest.raises(SceneFileError):
        compare_scene_files(b"<a", base().encode(), "A", [])


def test_the_edited_scene_missing_from_the_save_misses_every_edit():
    found = compare(document(scene("B")), [{"kind": "attribute", "path": "Run", "name": "layer", "value": "3"}])
    assert sorted(names(found)) == ["editor.saved_edit_missing", "editor.saved_other_scene_changed"]


def test_read_scene_file_gives_the_root_and_the_scenes_in_order(tmp_path):
    file = tmp_path / "SceneConfig.xml"
    file.write_text(base(), encoding="utf-8")
    read = read_scene_file(file)
    assert read.root_attributes == {"contentVersion": "5"} and read.scene_names == ["A", "B"]
    with pytest.raises(SceneFileError):
        read_scene_file(tmp_path / "gone.xml")
    file.write_text("<SceneManager", encoding="utf-8")
    with pytest.raises(SceneFileError):
        read_scene_file(file)


def make_guard(tmp_path, *contents):
    files = []
    for index, content in enumerate(contents):
        file = tmp_path / "content" / f"file{index}.xml"
        file.parent.mkdir(exist_ok=True)
        if content is not None:
            file.write_bytes(content)
        files.append(file)
    return ContentGuard(files, tmp_path / "backup"), files


def test_the_guard_restores_changed_bytes_and_leaves_an_unchanged_file_alone(tmp_path):
    guard, (changed, same) = make_guard(tmp_path, b"old", b"same")
    guard.arm()
    changed.write_bytes(b"new, and longer")
    mtime = same.stat().st_mtime_ns
    assert guard.release() == [changed.resolve()]
    assert changed.read_bytes() == b"old" and same.read_bytes() == b"same" and same.stat().st_mtime_ns == mtime


def test_the_guard_removes_a_file_that_was_created(tmp_path):
    guard, (created,) = make_guard(tmp_path, None)
    guard.arm()
    created.write_bytes(b"made by the session")
    assert guard.release() == [created.resolve()]
    assert not created.exists()


def test_the_guard_restores_a_deleted_file_and_is_idempotent(tmp_path):
    guard, (file,) = make_guard(tmp_path, b"old")
    assert guard.release() == []  # never armed
    guard.arm()
    file.unlink()
    assert guard.release() == [file.resolve()] and file.read_bytes() == b"old"
    file.write_bytes(b"later work")
    assert guard.release() == [] and file.read_bytes() == b"later work"


def test_the_guard_refuses_a_second_arm_and_a_backup_folder_in_a_work_tree(tmp_path, monkeypatch):
    guard, _ = make_guard(tmp_path, b"old")
    guard.arm()
    with pytest.raises(ContentGuardError, match="armed"):
        guard.arm()
    guard.release()
    monkeypatch.setattr(scene_file.png_ops, "inside_git_work_tree", lambda folder: True)
    inside, _ = make_guard(tmp_path, b"old")
    with pytest.raises(ContentGuardError, match="git work tree"):
        inside.arm()
    assert not inside.armed
