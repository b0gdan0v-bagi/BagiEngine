"""Which pointer fields make the generated file inline FactoryTraits (`factory_deps`)."""

import textwrap

import pytest

INLINED = "struct FactoryTraits<BECore::IThing>"

HOLDER = """\
    struct Holder {{
        BE_CLASS(Holder)
        BE_REFLECT_FIELD {attributes}Vector<{pointer}<IThing>> _things;
    }};
"""


def generate_holder(run_cli, tmp_path, include_gen, pointer, attributes=""):
    source = tmp_path / "src"
    source.mkdir()
    include_gen(source, "IThing", "Thing", "Holder")
    (source / "IThing.h").write_text(textwrap.dedent("""\
        namespace BECore {
            struct IThing {
                BE_CLASS(IThing, FACTORY_BASE)
            };
        }
    """), encoding="utf-8")
    (source / "Thing.h").write_text(textwrap.dedent("""\
        namespace BECore {
            struct Thing : IThing {
                BE_CLASS(Thing)
            };
        }
    """), encoding="utf-8")
    (source / "Holder.h").write_text(
        "namespace BECore {\n" + HOLDER.format(pointer=pointer, attributes=attributes) + "}\n", encoding="utf-8")
    code, _, err = run_cli(source, "--warnings-as-errors")
    assert (code, err) == (0, "")
    return (tmp_path / "out" / "Holder.gen.hpp").read_text(encoding="utf-8")


@pytest.mark.parametrize("pointer", ["IntrusivePtr", "IntrusivePtrAtomic", "IntrusivePtrNonAtomic"])
def test_a_pointer_to_a_factory_base_inlines_its_traits(run_cli, tmp_path, include_gen, pointer):
    assert INLINED in generate_holder(run_cli, tmp_path, include_gen, pointer)


def test_no_factory_keeps_the_traits_out_and_writes_no_attributes(run_cli, tmp_path, include_gen):
    generated = generate_holder(run_cli, tmp_path, include_gen, "IntrusivePtr", "BE_FIELD_ATTRS(no_factory) ")
    assert INLINED not in generated
    assert "FieldAttributes{" not in generated
    assert '::BECore::Ser::Load(d, _things, "things");' in generated
