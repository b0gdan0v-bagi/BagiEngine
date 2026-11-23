"""BE_FIELD_ATTRS(...): what the parser reads after BE_REFLECT_FIELD, and what it warns about."""

import pytest

from core.models import FieldAttrs, FieldData


def parse_fields(parse, body):
    classes, warnings = parse("struct Foo {\n    BE_CLASS(Foo)\n" + body + "\n};\n")
    assert len(classes) == 1
    return classes[0].fields, warnings


def one_field(parse, declaration):
    fields, warnings = parse_fields(parse, declaration)
    assert len(fields) == 1
    return fields[0], warnings


def messages(warnings):
    return [w.message for w in warnings]


# ---------------------------------------------------------------------------
# what is read
# ---------------------------------------------------------------------------

def test_every_attribute(parse):
    field, warnings = one_field(parse, (
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.05), tooltip("a, (b)"), '
        'readonly, hidden, required) float _x;'))
    assert (field.name, field.type_name) == ("_x", "float")
    assert field.attributes == FieldAttrs(
        has_range=True, range_min=0.0, range_max=1.0, step=0.05, tooltip="a, (b)",
        readonly=True, hidden=True, required=True)
    assert warnings == []


def test_no_factory_alone_is_not_a_field_attribute(parse):
    field, warnings = one_field(parse, "BE_REFLECT_FIELD BE_FIELD_ATTRS(no_factory) Vector<IntrusivePtr<Foo>> _x;")
    assert field.attributes.no_factory
    assert field.attributes.is_empty()
    assert warnings == []


def test_field_without_attributes_is_unchanged(parse):
    field, warnings = one_field(parse, "BE_REFLECT_FIELD float _x = 1.0f;")
    assert field.attributes == FieldAttrs()
    assert field.attributes.is_empty()
    assert warnings == []


@pytest.mark.parametrize("attrs, expected", [
    ("range(0, 1)", dict(has_range=True, range_min=0.0, range_max=1.0)),
    ("range(-1, 1)", dict(has_range=True, range_min=-1.0, range_max=1.0)),
    ("range(-1.5,+2.5)", dict(has_range=True, range_min=-1.5, range_max=2.5)),
    ("range(.5, 1.)", dict(has_range=True, range_min=0.5, range_max=1.0)),
    ("range(1e-3, 1E3)", dict(has_range=True, range_min=0.001, range_max=1000.0)),
    ("range(0.05f, 2.5F)", dict(has_range=True, range_min=0.05, range_max=2.5)),
    ("range(5, 5)", dict(has_range=True, range_min=5.0, range_max=5.0)),
    ("step(0.05f)", dict(step=0.05)),
    ("step(2)", dict(step=2.0)),
    ("step(1e-2)", dict(step=0.01)),
    ("readonly", dict(readonly=True)),
    ("hidden", dict(hidden=True)),
    ("required", dict(required=True)),
    ("no_factory", dict(no_factory=True)),
    ("readonly, required", dict(readonly=True, required=True)),
    ("tooltip(\"Speed\")", dict(tooltip="Speed")),
    ("tooltip(\"\")", dict(tooltip="")),
    ("readonly(), hidden()", dict(readonly=True, hidden=True)),
    ("hidden,", dict(hidden=True)),
    ("", dict()),
])
def test_attribute_values(parse, attrs, expected):
    field, warnings = one_field(parse, f"BE_REFLECT_FIELD BE_FIELD_ATTRS({attrs}) float _x;")
    assert field.attributes == FieldAttrs(**expected)
    assert warnings == []


@pytest.mark.parametrize("text", [
    r'a, b',
    r'(parenthesis) and )',
    r'say \"hi\"',
    r'back\\slash and \n newline',
    r'Скорость, м/с',
    r'it\'s // not a comment /* nor this */',
    r'{brace',
])
def test_tooltip_survives_as_written(parse, text):
    field, warnings = one_field(parse, f'BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("{text}"), readonly) float _x;')
    assert field.attributes.tooltip == text
    assert field.attributes.readonly
    assert warnings == []


def test_attributes_do_not_disturb_the_declaration(parse):
    fields, warnings = parse_fields(parse, "\n".join([
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1), step(0.1)) float _a = 0.5f;',
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) eastl::vector<int> _b = {1, 2};',
        'BE_REFLECT_FIELD float _c;',
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(required) Foo::Inner<int, 2> _d{};',
    ]))
    assert [(f.name, f.type_name) for f in fields] == [
        ("_a", "float"), ("_b", "eastl::vector<int>"), ("_c", "float"), ("_d", "Foo::Inner<int, 2>")]
    assert [f.attributes.is_empty() for f in fields] == [False, False, True, False]
    assert warnings == []


def test_multi_line_attribute_list(parse):
    fields, warnings = parse_fields(parse, "\n".join([
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(',
        '    range(0, 10),   // the allowed values',
        '    /* a hint: */ tooltip("Ten, at most"),',
        '    step(1)',
        ') int32_t _count = 3;',
        'BE_REFLECT_FIELD int32_t _next;',
    ]))
    assert [f.name for f in fields] == ["_count", "_next"]
    assert fields[0].attributes == FieldAttrs(
        has_range=True, range_min=0.0, range_max=10.0, step=1.0, tooltip="Ten, at most")
    assert warnings == []


def test_attribute_on_a_string_that_looks_like_markers(parse):
    field, warnings = one_field(parse, 'BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("BE_REFLECT_FIELD int x;")) int _x;')
    assert field.attributes.tooltip == "BE_REFLECT_FIELD int x;"
    assert warnings == []


def test_range_is_fine_on_integers_and_other_types(parse):
    fields, warnings = parse_fields(parse, "\n".join([
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 9)) uint8_t _a;',
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(step(0.5)) double _b;',
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("t"), readonly, hidden, required) bool _c;',
        'BE_REFLECT_FIELD BE_FIELD_ATTRS(tooltip("t"), readonly, hidden, required) String _d;',
    ]))
    assert [f.attributes.is_empty() for f in fields] == [False] * 4
    assert warnings == []


# ---------------------------------------------------------------------------
# cache
# ---------------------------------------------------------------------------

def test_cache_round_trip(parse):
    field, _ = one_field(parse, 'BE_REFLECT_FIELD BE_FIELD_ATTRS(range(-1, 1), step(0.25), tooltip("a \\" b"), hidden) float _x;')
    again = FieldData.from_dict(field.to_dict())
    assert again == field
    assert again.attributes == field.attributes


def test_a_cache_entry_without_attributes_reads_as_none():
    entry = FieldData.from_dict({"name": "a", "type_name": "int"})
    assert entry.attributes == FieldAttrs()


# ---------------------------------------------------------------------------
# warnings: the field is still reflected, without the bad attribute
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("attrs, expected_message, expected_attrs", [
    ("bogus", "unknown attribute 'bogus'", FieldAttrs()),
    ("bogus(1), hidden", "unknown attribute 'bogus'", FieldAttrs(hidden=True)),
    ("range(1)", "'range' takes two numbers", FieldAttrs()),
    ("range(0, 1, 2)", "'range' takes two numbers", FieldAttrs()),
    ("range(0, x)", "'range' takes two numbers", FieldAttrs()),
    ("range", "'range' takes two numbers", FieldAttrs()),
    ("range(\"a\", \"b\")", "'range' takes two numbers", FieldAttrs()),
    ("range(0x1, 5)", "'range' takes two numbers", FieldAttrs()),
    ("range(2, 1)", "min is greater than max", FieldAttrs()),
    ("step(0)", "must be greater than 0", FieldAttrs()),
    ("step(-0.5)", "must be greater than 0", FieldAttrs()),
    ("step", "'step' takes one number", FieldAttrs()),
    ("step(1, 2)", "'step' takes one number", FieldAttrs()),
    ("step(abc)", "'step' takes one number", FieldAttrs()),
    ("tooltip", "'tooltip' takes one string literal", FieldAttrs()),
    ("tooltip(5)", "'tooltip' takes one string literal", FieldAttrs()),
    ("tooltip(\"a\", \"b\")", "'tooltip' takes one string literal", FieldAttrs()),
    ("tooltip(\"a\" \"b\")", "'tooltip' takes one string literal", FieldAttrs()),
    ("tooltip(name)", "'tooltip' takes one string literal", FieldAttrs()),
    ("readonly(1)", "'readonly' takes no arguments", FieldAttrs()),
    ("hidden(true), required", "'hidden' takes no arguments", FieldAttrs(required=True)),
    ("hidden, hidden", "duplicate attribute 'hidden'", FieldAttrs(hidden=True)),
    ("range(0, 1), range(5, 6)", "duplicate attribute 'range'",
     FieldAttrs(has_range=True, range_min=0.0, range_max=1.0)),
    ("readonly hidden", "expected ','", FieldAttrs(readonly=True)),
    ("hidden; readonly", "expected ','", FieldAttrs(hidden=True)),
    ("hidden, 5", "unexpected '5'", FieldAttrs(hidden=True)),
    ("range(0, 1", "a closing parenthesis", None),
])
def test_bad_attribute_warns_on_the_marker_line(parse, attrs, expected_message, expected_attrs):
    source = f"BE_REFLECT_FIELD BE_FIELD_ATTRS({attrs}) float _x;"
    if expected_attrs is None:
        # an unbalanced list swallows the declaration: no field, one warning
        fields, warnings = parse_fields(parse, source)
        assert fields == []
    else:
        field, warnings = one_field(parse, source)
        assert field.name == "_x"
        assert field.attributes == expected_attrs
    assert len(warnings) == 1
    assert (warnings[0].kind, warnings[0].line) == ("field", 3)
    assert expected_message in warnings[0].message


@pytest.mark.parametrize("attr", ["range(0, 1)", "step(0.5)"])
@pytest.mark.parametrize("type_name, what", [
    ("bool", "a bool"),
    ("String", "a string"),
    ("PoolString", "a string"),
    ("ArenaString", "a string"),
    ("FixedString<32>", "a string"),
    ("eastl::string", "a string"),
])
def test_range_and_step_on_a_bool_or_a_string_warn(parse, attr, type_name, what):
    field, warnings = one_field(parse, f"BE_REFLECT_FIELD BE_FIELD_ATTRS({attr}, hidden) {type_name} _x;")
    assert field.attributes == FieldAttrs(hidden=True)
    assert len(warnings) == 1
    assert f"no meaning on {what} field" in warnings[0].message


def test_range_on_an_enum_warns(parse, tmp_path):
    from core.parser import create_parser
    path = tmp_path / "Enum.h"
    path.write_text("struct Foo {\n BE_CLASS(Foo)\n BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 2), readonly) Mode _m;\n};\n")
    parser = create_parser([], {"Mode"})
    classes, _ = parser.parse_file(path)
    assert classes[0].fields[0].attributes == FieldAttrs(readonly=True)
    assert [w.line for w in parser.last_warnings] == [3]
    assert "no meaning on an enum field" in parser.last_warnings[0].message


def test_several_problems_give_several_warnings_on_the_same_line(parse):
    field, warnings = one_field(parse, 'BE_REFLECT_FIELD BE_FIELD_ATTRS(bogus, step(0), hidden) float _x;')
    assert field.attributes == FieldAttrs(hidden=True)
    assert len(warnings) == 2
    assert {w.line for w in warnings} == {3}


@pytest.mark.parametrize("declaration", [
    "BE_FIELD_ATTRS(hidden) float _x;",
    "float _x BE_FIELD_ATTRS(hidden);",
])
def test_attributes_with_no_marker_right_before_them_warn(parse, declaration):
    fields, warnings = parse_fields(parse, declaration)
    assert any("BE_FIELD_ATTRS ignored: it must directly follow a BE_REFLECT_FIELD" in m for m in messages(warnings))
    assert all(f.attributes.is_empty() for f in fields)


def test_attributes_in_the_middle_of_a_declaration_warn(parse):
    fields, warnings = parse_fields(parse, "BE_REFLECT_FIELD float BE_FIELD_ATTRS(hidden) _x;")
    assert fields == []
    assert len(warnings) == 1
    assert "runs over the next marker 'BE_FIELD_ATTRS'" in warnings[0].message


def test_attributes_without_a_list_warn(parse):
    fields, warnings = parse_fields(parse, "BE_REFLECT_FIELD BE_FIELD_ATTRS float _x;\nBE_REFLECT_FIELD int _y;")
    assert [f.name for f in fields] == ["_y"]
    assert len(warnings) == 1
    assert "needs a parenthesised list" in warnings[0].message


def test_attributes_outside_a_class_warn(parse):
    classes, warnings = parse("BE_FIELD_ATTRS(hidden) float _x;\n")
    assert classes == []
    assert [(w.kind, w.line) for w in warnings] == [("field", 1)]


def test_a_define_of_the_macro_is_no_use(parse):
    classes, warnings = parse("#define BE_FIELD_ATTRS(...)\n// BE_FIELD_ATTRS(hidden) in a comment\n")
    assert classes == []
    assert warnings == []


def test_attributes_do_not_hide_a_missing_semicolon(parse):
    fields, warnings = parse_fields(parse, "BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) float _x\nBE_REFLECT_FIELD int _y;")
    assert fields == []
    assert any("runs over the next marker" in m for m in messages(warnings))
