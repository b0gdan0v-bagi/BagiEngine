"""The C++ initializer of a field's attributes in the generated ReflectionTraits (format_field_attributes)."""

import textwrap

import pytest

from core.generator import format_field_attributes
from core.models import FieldAttrs


def test_no_attributes_write_nothing():
    assert format_field_attributes(FieldAttrs()) == ''


def test_every_attribute_in_declaration_order():
    attrs = FieldAttrs(has_range=True, range_min=-10.0, range_max=1000.0, step=0.05, tooltip='say \\"hi\\" café',
                       readonly=True, hidden=True, required=True)
    assert format_field_attributes(attrs) == (
        '::BECore::FieldAttributes{ .hasRange = true, .rangeMin = -10.0, .rangeMax = 1000.0, .step = 0.05, '
        '.tooltip = ::BECore::StringView{"say \\"hi\\" café"}, .readOnly = true, .hidden = true, .required = true }')


def test_only_the_set_ones_are_written():
    assert format_field_attributes(FieldAttrs(hidden=True)) == '::BECore::FieldAttributes{ .hidden = true }'
    assert format_field_attributes(FieldAttrs(step=1e-05)) == '::BECore::FieldAttributes{ .step = 1e-05 }'


# ---------------------------------------------------------------------------
# the generated Deserialize passes the attributes of a field that has some
# ---------------------------------------------------------------------------

LOAD_WITH_ATTRIBUTES = ('::BECore::Ser::Load(d, count, "count", '
                        'std::get<0>(::BECore::ReflectionTraits<Plain>::fields).attributes);')
LOAD_WITHOUT_ATTRIBUTES = '::BECore::Ser::Load(d, plain, "plain");'


def generate(run_cli, tmp_path, source, include_gen, header):
    include_gen(source, 'Plain')
    (source / "Plain.h").write_text(header, encoding="utf-8")
    code, _, err = run_cli(source, "--warnings-as-errors")
    assert (code, err) == (0, "")
    return (tmp_path / "out" / "Plain.gen.hpp").read_text(encoding="utf-8")


@pytest.fixture
def source(tmp_path):
    directory = tmp_path / "src"
    directory.mkdir()
    return directory


def test_a_class_loads_a_field_with_attributes_through_its_traits(run_cli, tmp_path, source, include_gen):
    generated = generate(run_cli, tmp_path, source, include_gen, textwrap.dedent("""\
        struct Plain {
            BE_CLASS(Plain)
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 9), required) int count = 0;
            BE_REFLECT_FIELD int plain = 0;
        };
    """))
    assert LOAD_WITH_ATTRIBUTES in generated
    assert LOAD_WITHOUT_ATTRIBUTES in generated
    assert generated.count("::BECore::Ser::Load(") == 2


def test_an_event_loads_a_field_with_attributes_through_its_traits(run_cli, tmp_path, source, include_gen):
    generated = generate(run_cli, tmp_path, source, include_gen, textwrap.dedent("""\
        struct Plain {
            BE_EVENT(Plain)
            BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 9)) int count = 0;
            BE_REFLECT_FIELD int plain = 0;
        };
    """))
    assert LOAD_WITH_ATTRIBUTES in generated
    assert LOAD_WITHOUT_ATTRIBUTES in generated
    assert generated.count("::BECore::Ser::Load(") == 2


def test_a_derived_class_loads_its_own_fields_by_their_own_index(run_cli, tmp_path, source, include_gen):
    generated = generate(run_cli, tmp_path, source, include_gen, textwrap.dedent("""\
        struct Base {
            BE_CLASS(Base)
        };
        struct Plain : Base {
            BE_CLASS(Plain)
            BE_REFLECT_FIELD int plain = 0;
            BE_REFLECT_FIELD BE_FIELD_ATTRS(hidden) int count = 0;
        };
    """))
    assert LOAD_WITHOUT_ATTRIBUTES in generated
    assert ('::BECore::Ser::Load(d, count, "count", '
            'std::get<1>(::BECore::ReflectionTraits<Plain>::fields).attributes);') in generated
