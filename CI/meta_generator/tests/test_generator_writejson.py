"""The WriteJson that the template writes (reflection.gen.hpp.j2): the same object JsonSerializer saves, key by key."""

import textwrap

import pytest

from core.cache import MetadataCache

HEADER = textwrap.dedent("""\
    struct Plain {
        BE_CLASS(Plain)
        BE_REFLECT_FIELD int _b = 0;
        BE_REFLECT_FIELD int a = 0;
        BE_REFLECT_FIELD String _c;
    };
    struct Twins {
        BE_CLASS(Twins)
        BE_REFLECT_FIELD int _x = 0;
        BE_REFLECT_FIELD String x;
        BE_REFLECT_FIELD int y = 0;
    };
    struct Bare {
        BE_CLASS(Bare)
    };
    struct Child : Plain {
        BE_CLASS(Child)
        BE_REFLECT_FIELD int extra = 0;
    };
    struct Ping {
        BE_EVENT(Ping)
        BE_REFLECT_FIELD int id = 0;
        BE_REFLECT_FIELD float _at = 0;
    };
    struct Silent {
        BE_EVENT(Silent)
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


def writer(generated, name):
    """The body of `name::WriteJson`, from its signature to the closing brace."""
    start = generated.index(f"void {name}::WriteJson(::BECore::JsonWriter& w) const {{")
    return generated[start:generated.index("\n}\n", start)]


def calls(body):
    return [line.strip() for line in body.splitlines() if "SaveField" in line]


def test_the_file_includes_the_helpers(generated):
    assert "#include <BECore/Control/JsonWriteReflect.h>" in generated


def test_the_cache_version_regenerates_unchanged_sources():
    assert MetadataCache.CACHE_VERSION == "2.12"


def test_fields_are_written_in_the_order_of_their_serialized_names(generated):
    body = writer(generated, "Plain")
    assert body.index("w.BeginObject();") < body.index("SaveField") < body.index("w.EndObject();")
    assert calls(body) == [
        '::BECore::JsonWrite::SaveField(w, "\\"a\\":", "a", a);',
        '::BECore::JsonWrite::SaveField(w, "\\"b\\":", "b", _b);',
        '::BECore::JsonWrite::SaveField(w, "\\"c\\":", "c", _c);',
    ]


def test_of_two_fields_with_one_serialized_name_only_the_later_is_written(generated):
    assert calls(writer(generated, "Twins")) == [
        '::BECore::JsonWrite::SaveField(w, "\\"x\\":", "x", x);',
        '::BECore::JsonWrite::SaveField(w, "\\"y\\":", "y", y);',
    ]


def test_a_class_without_fields_writes_an_empty_object(generated):
    body = writer(generated, "Bare")
    assert "SaveField" not in body
    assert "w.BeginObject();" in body and "w.EndObject();" in body


def test_a_class_with_a_reflected_parent_writes_what_serialize_saves(generated):
    body = writer(generated, "Child")
    assert "WriteSerializedObject(w, [&](::BECore::ISerializer& s) { Serialize(s); });" in body
    assert "SaveField" not in body and "BeginObject" not in body


def test_an_event_writes_its_fields_in_sorted_order(generated):
    assert calls(writer(generated, "Ping")) == [
        '::BECore::JsonWrite::SaveField(w, "\\"at\\":", "at", _at);',
        '::BECore::JsonWrite::SaveField(w, "\\"id\\":", "id", id);',
    ]


def test_an_event_without_fields_has_no_definition(generated):
    assert "Silent::WriteJson" not in generated
