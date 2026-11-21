"""schema_to_xsd on a type with `Value<T>` fields: the forms the loader reads are accepted, a wrong type or key is found (xmllint judges)."""

import json
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest
from schema_support import FIXTURES, needs_xmllint

import schema_to_xsd as xsd
from schema_common import XS_NAMESPACE

ROOT = "root"
ACCEPTED = [
    '<root damage="12"/>',
    '<root><damage value="12"/></root>',
    '<root><damage from="caster.attack"/></root>',
    '<root><damage expr="caster.attack * 2"/></root>',
    '<root><damage kind="Slot" from=".."/></root>',
    '<root><damage><tree type="CalcBinary"/></damage></root>',
    '<root damage="1" flag="true" mode="Hard"/>',
    '<root><flag value="false"/><mode value="Easy"/></root>',
    '<root><pos from="caster.position"/></root>',
    '<root><pos x="1" y="2" z="3"/></root>',
    '<root><pos expr="caster.velocity * 2"/></root>',
    '<root><pos><nested a="1"/></pos></root>',
    '<root><point x="1" y="2"/></root>',
    '<root><point from="caster.position"/></root>',
    '<root><point><tree type="CalcBinary"/></point></root>',
    '<root><scores><item value="1"/><item from="caster.attack"/></scores></root>',
    '<root><widgets><widget type="Gauge" power="3"/><widget type="Gauge"><power from="a.b"/></widget></widgets></root>',
]
REJECTED = [
    ("damage_text", '<root damage="abc"/>', "'abc' is not a valid value of the atomic type"),
    ("damage_over_range", '<root damage="500"/>', "'500' is greater than the maximum value allowed"),
    ("value_text", '<root><damage value="abc"/></root>', "'abc' is not a valid value"),
    ("flag_maybe", '<root><flag value="maybe"/></root>', "'maybe' is not a valid value of the atomic type 'xs:boolean'"),
    ("mode_unknown", '<root mode="Medium"/>', "'Medium' is not an element of the set"),
    ("kind_other", '<root><damage kind="Other"/></root>', "'Other' is not an element of the set"),
    ("surprise_child", '<root><damage from="x"><surprise/></damage></root>', "Element 'surprise': This element is not expected"),
    ("point_z", '<root><point z="1"/></root>', "attribute 'z' is not allowed"),
    ("point_value", '<root><point value="1"/></root>', "attribute 'value' is not allowed"),
    ("item_text", '<root><scores><item value="x"/></scores></root>', "'x' is not a valid value"),
    ("gauge_text", '<root><widgets><widget type="Gauge" power="x"/></widgets></root>', "'x' is not a valid value"),
]


@pytest.fixture
def values_schema() -> dict:
    return json.loads((FIXTURES / "schema_values.json").read_text(encoding="utf-8"))


def stats_xsd(schema: dict) -> str:
    return xsd.build_xsd(schema, "Stats", ROOT)


def complex_type(text: str, name: str) -> ET.Element:
    return next(node for node in ET.fromstring(text).iter(f"{{{XS_NAMESPACE}}}complexType") if node.get("name") == name)


def named(node: ET.Element, tag: str) -> dict[str, ET.Element]:
    return {child.get("name"): child for child in node.iter(f"{{{XS_NAMESPACE}}}{tag}")}


def element_type(text: str, owner: str, name: str) -> ET.Element:
    return complex_type(text, named(complex_type(text, owner), "element")[name].get("type"))


def restrictions_of(text: str, simple_name: str) -> list[tuple[str, str]]:
    node = next(node for node in ET.fromstring(text).iter(f"{{{XS_NAMESPACE}}}simpleType") if node.get("name") == simple_name)
    return [(child.tag.rsplit("}", 1)[1], child.get("value")) for child in node.iter() if child.get("value") is not None]


def test_the_same_schema_gives_the_same_bytes(values_schema):
    first = stats_xsd(values_schema)
    assert stats_xsd(json.loads(json.dumps(values_schema))) == first
    assert stats_xsd(values_schema) == first


def test_a_value_of_an_attribute_type_is_the_plain_attribute_and_an_element_of_one_name(values_schema):
    text = stats_xsd(values_schema)
    root = complex_type(text, "root_Stats")
    attributes = named(root, "attribute")
    damage = attributes["damage"]
    assert damage.get("use") == "optional"  # `required` of the field is dropped: the element may stand for it
    assert named(root, "element")["damage"].get("minOccurs") == "0"
    assert [item[0] for item in restrictions_of(text, damage.get("type"))] == ["minInclusive", "maxInclusive"]  # the range of a plain field
    assert attributes["flag"].get("type") == "xs:boolean"
    form = element_type(text, "root_Stats", "damage")
    form_attributes = named(form, "attribute")
    assert set(form_attributes) == {"value", "from", "expr", "kind"}
    assert form_attributes["value"].get("type") == damage.get("type")
    assert form_attributes["from"].get("type") == "xs:string" and form_attributes["expr"].get("type") == "xs:string"
    assert [value for tag, value in restrictions_of(text, form_attributes["kind"].get("type")) if tag == "enumeration"] == ["Const", "Slot", "Program"]
    assert list(named(form, "element")) == ["tree"] and named(form, "element")["tree"].get("type") == "xs:anyType"
    assert [node.text for node in ET.fromstring(text).iter(f"{{{XS_NAMESPACE}}}documentation")] == ["Hit points taken", "Hit points taken"]


def test_a_value_of_an_opaque_type_has_no_plain_attribute_and_is_open(values_schema):
    text = stats_xsd(values_schema)
    root = complex_type(text, "root_Stats")
    assert "pos" not in named(root, "attribute")
    form = element_type(text, "root_Stats", "pos")
    assert set(named(form, "attribute")) == {"from", "expr", "kind"}
    assert len(list(form.iter(f"{{{XS_NAMESPACE}}}anyAttribute"))) == 1 and len(list(form.iter(f"{{{XS_NAMESPACE}}}any"))) == 1


def test_a_value_of_a_reflected_type_holds_its_members_optional_and_the_reserved_keys_win(values_schema):
    text = stats_xsd(values_schema)
    assert "point" not in named(complex_type(text, "root_Stats"), "attribute")
    form = element_type(text, "root_Stats", "point")
    attributes = named(form, "attribute")
    assert set(attributes) == {"x", "y", "from", "expr", "kind"}  # the member `from` of Point is the key; there is no `value`
    assert all(node.get("use") == "optional" for node in attributes.values())  # `x` of Point is required for a plain field only
    assert attributes["from"].get("type") == "xs:string"
    assert not list(form.iter(f"{{{XS_NAMESPACE}}}any"))


def test_a_list_of_values_has_items_of_the_same_forms(values_schema):
    item = element_type(stats_xsd(values_schema), "root_Stats", "scores")
    content = complex_type(stats_xsd(values_schema), named(item, "element")["item"].get("type"))
    assert set(named(content, "attribute")) == {"value", "from", "expr", "kind"}


def test_a_factory_element_keeps_both_forms_of_a_value_field(values_schema):
    text = stats_xsd(values_schema)
    factory = complex_type(text, "factory_Widget")
    assert named(factory, "attribute")["power"].get("use") == "optional"
    assert "power" in named(factory, "element")


def test_a_later_field_of_the_name_replaces_both_forms(values_schema):
    values_schema["types"][2]["fields"].append({"name": "damage", "type": {"kind": "string"}, "attributes": {}})
    root = complex_type(stats_xsd(values_schema), "root_Stats")
    assert named(root, "attribute")["damage"].get("type") == "xs:string"
    assert "damage" not in named(root, "element")


def test_a_value_that_holds_its_own_type_ends(values_schema):
    values_schema["types"][1]["fields"].append({"name": "again", "type": {"kind": "value", "element": {"kind": "object", "object": "Point"}}, "attributes": {}})
    assert "again" in stats_xsd(values_schema)


def validate(text: str, schema: dict, tmp_path: Path) -> list[str]:
    schema_file = tmp_path / "Stats.xsd"
    schema_file.write_text(stats_xsd(schema), encoding="utf-8")
    document = tmp_path / "doc.xml"
    document.write_text(text, encoding="utf-8")
    return xsd.validate_with_xmllint(schema_file, document)


@needs_xmllint
@pytest.mark.parametrize("text", ACCEPTED)
def test_every_form_the_loader_reads_is_accepted(values_schema, tmp_path, text):
    assert validate(text, values_schema, tmp_path) == []


@needs_xmllint
@pytest.mark.parametrize(("label", "text", "message"), REJECTED, ids=[entry[0] for entry in REJECTED])
def test_a_wrong_type_or_key_is_found(values_schema, tmp_path, label, text, message):
    findings = validate(text, values_schema, tmp_path)
    assert findings, f"{label} was accepted"
    assert message in " ".join(findings)
