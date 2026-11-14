"""The C++ initializer of a field's attributes in the generated ReflectionTraits (format_field_attributes)."""

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
