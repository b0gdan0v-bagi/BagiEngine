"""schema_to_xsd on a hand-written schema that has every shape; xmllint is the judge of the XML (skipped when it is absent)."""

import json
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest
from schema_support import FIXTURES, needs_xmllint

import schema_to_xsd as xsd
from schema_common import XS_NAMESPACE

SETTINGS_ROOT = "root"
INVALID_FILES = [
    ("typo_attribute.xml", "attribute 'volum' is not allowed"),
    ("bad_enum.xml", "'Medium' is not an element of the set"),
    ("out_of_range.xml", "'1.5' is greater than the maximum value allowed"),
    ("unknown_child.xml", "Element 'surprise': This element is not expected"),
    ("missing_required.xml", "attribute 'level' is required"),
    ("too_long_text.xml", "exceeds the allowed maximum length of '8'"),
    ("bad_widget_type.xml", "'Slider' is not an element of the set"),
    ("bad_item_range.xml", "'101' is greater than the maximum value allowed"),
    ("bad_variant_index.xml", "'3' is greater than the maximum value allowed ('2')"),
]


def settings_xsd(schema: dict, tmp_path: Path) -> Path:
    path = tmp_path / "Settings.xsd"
    path.write_text(xsd.build_xsd(schema, "Settings", SETTINGS_ROOT), encoding="utf-8")
    return path


def declared(text: str, tag: str) -> list[ET.Element]:
    return list(ET.fromstring(text).iter(f"{{{XS_NAMESPACE}}}{tag}"))


def complex_type(text: str, name: str) -> ET.Element:
    return next(node for node in declared(text, "complexType") if node.get("name") == name)


def test_the_xsd_is_well_formed_and_declares_the_root_element(schema):
    text = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    assert text.startswith('<?xml version="1.0" encoding="UTF-8"?>')
    assert [node.get("name") for node in ET.fromstring(text).findall(f"{{{XS_NAMESPACE}}}element")] == [SETTINGS_ROOT]


def test_the_same_schema_gives_the_same_bytes(schema):
    reordered = json.loads(json.dumps(schema))
    first = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    assert xsd.build_xsd(reordered, "Settings", SETTINGS_ROOT) == first
    assert xsd.build_xsd(schema, "Settings", SETTINGS_ROOT) == first


def test_a_scalar_is_an_attribute_of_the_xsd_type_of_its_kind(schema):
    attributes = {node.get("name"): node for node in complex_type(xsd.build_xsd(schema, "Settings", SETTINGS_ROOT), "root_Settings").findall(f"{{{XS_NAMESPACE}}}attribute")}
    assert attributes["enabled"].get("type") == "xs:boolean"
    assert attributes["id"].get("type") == "xs:unsignedLong"
    assert attributes["nickname"].get("type") == "xs:string"
    assert attributes["level"].get("use") == "required" and attributes["enabled"].get("use") == "optional"


def test_a_range_and_a_capacity_become_restrictions_and_a_tooltip_an_annotation(schema):
    text = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    restrictions = {(node.get("base"), tuple(sorted((child.tag.rsplit("}", 1)[1], child.get("value")) for child in node))) for node in declared(text, "restriction")}
    assert ("xs:float", (("maxInclusive", "1"), ("minInclusive", "0"))) in restrictions
    assert ("xs:int", (("maxInclusive", "10"), ("minInclusive", "1"))) in restrictions
    assert ("xs:string", (("maxLength", "8"),)) in restrictions
    assert [node.text for node in declared(text, "documentation")] == ["From silent to loud"]


def test_an_enum_is_one_simple_type_however_often_it_is_used(schema):
    schema["types"][0]["fields"].append({"name": "again", "type": {"kind": "enum", "values": ["Easy", "Hard"]}, "attributes": {}})
    schema["types"][5]["fields"].append({"name": "second", "type": {"kind": "enum", "values": ["Easy", "Hard"]}, "attributes": {}})
    text = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    enums = [node for node in declared(text, "simpleType") if [child.get("value") for child in node.iter(f"{{{XS_NAMESPACE}}}enumeration")] == ["Easy", "Hard"]]
    assert len(enums) == 1


def test_a_factory_element_has_the_union_of_its_derived_types_all_optional(schema):
    text = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    factory = complex_type(text, "factory_Widget")
    attributes = {node.get("name"): node for node in factory.findall(f"{{{XS_NAMESPACE}}}attribute")}
    assert set(attributes) == {"type", "enabled", "priority", "text", "size"}
    assert attributes["type"].get("use") == "required"  # Widget is abstract: no element without a type
    assert all(attributes[name].get("use") == "optional" for name in ("enabled", "priority", "text", "size"))
    assert attributes["size"].get("type") == "xs:string"  # a float in Button and an int in Label: no one type fits both
    assert [node.get("name") for node in factory.iter(f"{{{XS_NAMESPACE}}}element")] == ["style"]


def test_a_factory_that_can_make_its_base_does_not_need_a_type(schema):
    attributes = {node.get("name"): node for node in complex_type(xsd.build_xsd(schema, "Settings", SETTINGS_ROOT), "factory_Leaf").findall(f"{{{XS_NAMESPACE}}}attribute")}
    assert attributes["type"].get("use") == "optional"


def test_a_list_names_its_elements_by_the_factory_and_falls_back_on_the_class_name(schema):
    text = xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    widgets = next(node for node in declared(text, "element") if node.get("name") == "widgets")
    assert [node.get("name") for node in complex_type(text, widgets.get("type")).iter(f"{{{XS_NAMESPACE}}}element")] == ["widget"]
    children = next(node for node in declared(text, "element") if node.get("name") == "children")
    assert [node.get("name") for node in complex_type(text, children.get("type")).iter(f"{{{XS_NAMESPACE}}}element")] == ["leaf"]


@pytest.mark.parametrize(("base", "element"), [("IComponent", "component"), ("SceneNode", "node"), ("Scene", "scene"), ("UIElement", "element"), ("ILogSink", "sink"), ("Widget", "widget")])
def test_the_conventional_element_name_is_the_last_word_of_the_class(base, element):
    assert xsd.conventional_element_name(base) == element


def test_a_content_root_may_carry_the_content_version(schema):
    assert "contentVersion" not in xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)
    assert 'name="contentVersion"' in xsd.build_xsd(schema, "Settings", "SceneManager")


def test_a_type_that_holds_itself_ends(schema):
    text = xsd.build_xsd(schema, "Leaf", SETTINGS_ROOT)
    assert 'type="factory_Leaf"' in text


def test_a_wrapper_puts_the_type_one_element_down(schema):
    text = xsd.build_xsd(schema, "Point", SETTINGS_ROOT, wrapper="point")
    assert [node.get("name") for node in complex_type(text, "root_Point").iter(f"{{{XS_NAMESPACE}}}element")] == ["point"]


def test_an_unknown_type_and_another_schema_version_are_refused(schema):
    with pytest.raises(xsd.SchemaError, match="no type 'Nothing'"):
        xsd.build_xsd(schema, "Nothing", SETTINGS_ROOT)
    schema["version"] = 2
    with pytest.raises(xsd.SchemaError, match="version 2"):
        xsd.build_xsd(schema, "Settings", SETTINGS_ROOT)


@needs_xmllint
def test_the_valid_file_passes(schema, tmp_path):
    assert xsd.validate_with_xmllint(settings_xsd(schema, tmp_path), FIXTURES / "valid.xml") == []


@needs_xmllint
@pytest.mark.parametrize(("name", "message"), INVALID_FILES)
def test_a_wrong_file_is_found_with_its_line(schema, tmp_path, name, message):
    findings = xsd.validate_with_xmllint(settings_xsd(schema, tmp_path), FIXTURES / name)
    assert findings, f"{name} was accepted"
    assert message in " ".join(findings)
    assert f"{name}:" in findings[0]


def write_config_dir(tmp_path: Path) -> Path:
    config = tmp_path / "config"
    config.mkdir()
    shutil.copy(FIXTURES / "valid.xml", config / "Settings.xml")
    (config / "ByRoot.xml").write_text('<Point x="1" y="2"/>', encoding="utf-8")
    (config / "Bound.xml").write_text('<root><point x="1"/></root>', encoding="utf-8")
    (config / "Plain.xml").write_text("<root/>", encoding="utf-8")
    (config / "Broken.xml").write_text("<root>", encoding="utf-8")
    return config


def test_a_file_is_typed_by_its_binding_then_its_stem_then_its_root_element(schema, tmp_path):
    config = write_config_dir(tmp_path)
    results = xsd.convert_config_dir(schema, config, tmp_path / "out", {"Bound": xsd.Binding("Point", "point")})
    by_stem = {result.stem: result for result in results}
    assert [result.stem for result in results] == sorted(by_stem)
    assert by_stem["Settings"].type_name == "Settings" and by_stem["Settings"].xsd == tmp_path / "out" / "Settings.xsd"
    assert by_stem["ByRoot"].type_name == "Point" and by_stem["ByRoot"].root == "Point"
    assert by_stem["Bound"].type_name == "Point"
    assert sorted(path.name for path in (tmp_path / "out").iterdir()) == ["Bound.xsd", "ByRoot.xsd", "Settings.xsd"]


def test_a_file_without_a_type_is_listed_with_the_reason(schema, tmp_path):
    results = {result.stem: result for result in xsd.convert_config_dir(schema, write_config_dir(tmp_path), tmp_path / "out")}
    assert results["Plain"].xsd is None and "read by hand" in results["Plain"].reason
    assert results["Broken"].xsd is None and results["Broken"].reason.startswith("does not parse")
    (tmp_path / "again").mkdir()
    bad = xsd.convert_config_dir(schema, write_config_dir(tmp_path / "again"), tmp_path / "out2", {"Plain": xsd.Binding("Nothing")})
    assert next(result for result in bad if result.stem == "Plain").reason == "bound to 'Nothing', which the schema does not have"


@needs_xmllint
def test_the_files_of_a_folder_are_checked_against_their_own_xsd(schema, tmp_path):
    config = write_config_dir(tmp_path)
    results = xsd.convert_config_dir(schema, config, tmp_path / "out")
    assert xsd.check_files(results, config) == {}
    (config / "Settings.xml").write_text((FIXTURES / "typo_attribute.xml").read_text(encoding="utf-8"), encoding="utf-8")
    findings = xsd.check_files(results, config)
    assert list(findings) == ["Settings"] and "'volum'" in findings["Settings"][0]


def test_a_binding_is_a_stem_a_type_and_maybe_an_element():
    assert xsd.parse_binding("LoggerConfig=LoggerManager") == ("LoggerConfig", xsd.Binding("LoggerManager"))
    assert xsd.parse_binding("SDLWindowConfig=SDLWindowConfig@window") == ("SDLWindowConfig", xsd.Binding("SDLWindowConfig", "window"))
    for text in ("Stem", "=Type", "Stem="):
        with pytest.raises(ValueError, match="Stem=Type"):
            xsd.parse_binding(text)


def test_the_vscode_block_names_each_covered_file_and_its_xsd(schema, tmp_path):
    config = write_config_dir(tmp_path)
    results = xsd.convert_config_dir(schema, config, tmp_path / "out")
    block = xsd.vscode_settings(results, config)
    assert block.startswith('"xml.fileAssociations": [')
    associations = json.loads(block.split(": ", 1)[1])
    assert {entry["pattern"] for entry in associations} == {"**/config/ByRoot.xml", "**/config/Settings.xml"}
    assert all(Path(entry["systemId"]).is_file() for entry in associations)


def test_the_command_line_prints_the_counts_and_the_block(schema, tmp_path, monkeypatch, capsys):
    config = write_config_dir(tmp_path)
    schema_path = tmp_path / "schema.json"
    schema_path.write_text(json.dumps(schema), encoding="utf-8")
    monkeypatch.setattr("sys.argv", ["schema_to_xsd.py", str(schema_path), "--config-dir", str(config), "--out-dir", str(tmp_path / "out"), "--bind", "Bound=Point@point", "--vscode-settings"])
    assert xsd.main() == 0
    output = capsys.readouterr().out
    assert "covered 3, not covered 2" in output
    assert "not covered Plain.xml: " in output and "xml.fileAssociations" in output


def test_the_command_line_refuses_a_schema_it_cannot_read(tmp_path, monkeypatch, capsys):
    schema_path = tmp_path / "schema.json"
    schema_path.write_text('{"version": 9}', encoding="utf-8")
    monkeypatch.setattr("sys.argv", ["schema_to_xsd.py", str(schema_path), "--config-dir", str(tmp_path), "--out-dir", str(tmp_path / "out")])
    assert xsd.main() == 2
    assert "version 9" in capsys.readouterr().err
