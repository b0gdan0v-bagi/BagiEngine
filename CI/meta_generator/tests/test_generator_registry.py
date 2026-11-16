"""The registration of every class in TypeRegistry that the template writes (reflection.gen.hpp.j2)."""

import textwrap

import pytest

HEADER = textwrap.dedent("""\
    struct IThing {
        BE_CLASS(IThing, FACTORY_BASE)
        virtual void Touch() = 0;
    };
    struct Plain {
        BE_CLASS(Plain)
        BE_REFLECT_FIELD int count = 0;
        BE_FUNCTION void Add(int amount, float scale);
        BE_FUNCTION int Total() const;
        BE_FUNCTION void Reset();
    };
    struct Thing : IThing {
        BE_CLASS(Thing)
        BE_REFLECT_FIELD int size = 0;
    };
    struct Derived : Plain {
        BE_CLASS(Derived)
    };
    struct Padding {};
    struct Shifted : Padding, Plain {
        BE_CLASS(Shifted)
    };
    struct Ping {
        BE_EVENT(Ping)
        BE_REFLECT_FIELD int id = 0;
    };
""")


@pytest.fixture
def generated(run_cli, tmp_path, include_gen):
    source = tmp_path / "src"
    source.mkdir()
    include_gen(source, "Things")
    (source / "Things.h").write_text(HEADER, encoding="utf-8")
    code, _, err = run_cli(source, "--warnings-as-errors")
    assert (code, err) == (0, "")
    return (tmp_path / "out" / "Things.gen.hpp").read_text(encoding="utf-8")


def registration(generated, name):
    """The statement that registers `name`, from its marker comment to the end of its namespace block."""
    start = generated.index(f"const bool _regType_{name} =")
    return generated[start:generated.index("}  // namespace", start)]


def traits(generated, name):
    start = generated.index(f"struct ReflectionTraits<{name}> {{")
    return generated[start:generated.index("\n};", start)]


def test_the_file_includes_what_the_registration_uses(generated):
    assert "#include <BECore/Reflection/TypeInfo.h>" in generated
    assert "#include <BECore/Reflection/TypeRegistry.h>" in generated


def test_every_class_and_event_registers(generated):
    for name in ("IThing", "Plain", "Thing", "Derived", "Shifted", "Ping"):
        assert registration(generated, name).count("TypeRegistry::GetInstance().Register(") == 1


def test_a_class_without_a_factory_names_no_factory_base(generated):
    assert registration(generated, "Plain").strip().endswith(
        "::BECore::MakeTypeInfo<Plain, ::BECore::ReflectedParentOf<Plain>>());")


def test_a_factory_class_names_the_base_that_gives_it_a_creator(generated):
    assert "MakeTypeInfo<Thing, ::BECore::ReflectedParentOf<Thing>, IThing>()" in registration(generated, "Thing")


def test_an_abstract_factory_base_registers_without_a_factory_base(generated):
    # It registers in no factory, so it has no creator; the type is listed all the same
    assert "MakeTypeInfo<IThing, ::BECore::ReflectedParentOf<IThing>>()" in registration(generated, "IThing")


def test_an_event_registers_with_no_parent_arguments(generated):
    assert registration(generated, "Ping").strip().endswith("::BECore::MakeTypeInfo<Ping>());")


def test_the_parent_is_written_when_it_is_reflected(generated):
    assert "using Parent = Plain;" in traits(generated, "Derived")
    assert "using Parent = IThing;" in traits(generated, "Thing")
    assert "using Parent =" not in traits(generated, "Plain")
    assert "using Parent =" not in traits(generated, "Ping")


def test_an_unreflected_first_base_gives_no_parent(generated):
    # The registry walks one parent per class: a reflected base that is not the first stays out of the chain
    assert "using Parent =" not in traits(generated, "Shifted")


def test_a_method_carries_its_parameter_names(generated):
    methods = traits(generated, "Plain")
    assert 'MethodInfo{ eastl::string_view{"Add"}, &Plain::Add, eastl::string_view{"amount,scale"} }' in methods
    assert 'ConstMethodInfo{ eastl::string_view{"Total"}, &Plain::Total }' in methods
    assert 'MethodInfo{ eastl::string_view{"Reset"}, &Plain::Reset }' in methods

