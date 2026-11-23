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
    struct Grandchild : Child {
        BE_CLASS(Grandchild)
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


def body_of(generated, signature):
    start = generated.index(signature)
    return generated[start:generated.index("\n}\n", start)]


def writer(generated, name):
    """The body of `name::WriteJson`, from its signature to the closing brace."""
    return body_of(generated, f"void {name}::WriteJson(::BECore::JsonWriter& w) const {{")


def members(generated, name):
    """The body of `name::WriteJsonMembers` of a BE_CLASS."""
    return body_of(
        generated,
        f"void {name}::WriteJsonMembers(::BECore::JsonWriter& w, ::BECore::JsonWrite::MemberMerge* outer) const {{",
    )


def calls(body):
    return [line.strip() for line in body.splitlines() if "SaveField" in line]


def test_the_file_includes_the_helpers(generated):
    assert "#include <BECore/Control/JsonWriteReflect.h>" in generated


def test_the_cache_version_regenerates_unchanged_sources():
    assert MetadataCache.CACHE_VERSION == "2.14"


def test_a_class_writes_its_members_in_the_order_of_their_serialized_names_each_guarded_by_the_merge(generated):
    body = members(generated, "Plain")
    assert [line.strip() for line in body.splitlines() if "Before" in line or "SaveField" in line] == [
        'if (outer == nullptr || outer->Before(w, "a")) {',
        '::BECore::JsonWrite::SaveField(w, "\\"a\\":", "a", this->a);',
        'if (outer == nullptr || outer->Before(w, "b")) {',
        '::BECore::JsonWrite::SaveField(w, "\\"b\\":", "b", this->_b);',
        'if (outer == nullptr || outer->Before(w, "c")) {',
        '::BECore::JsonWrite::SaveField(w, "\\"c\\":", "c", this->_c);',
    ]
    assert body.index('Before(w, "c")') < body.index("outer->Finish(w);")


def test_the_root_class_hands_the_rest_to_the_merge_and_write_json_opens_the_object(generated):
    assert "if (outer != nullptr) {\n        outer->Finish(w);" in members(generated, "Plain")
    body = writer(generated, "Plain")
    assert "SaveField" not in body
    assert body.index("w.BeginObject();") < body.index("Plain::WriteJsonMembers(w, nullptr);") < body.index("w.EndObject();")


def test_of_two_fields_with_one_serialized_name_only_the_later_is_written(generated):
    assert calls(members(generated, "Twins")) == [
        '::BECore::JsonWrite::SaveField(w, "\\"x\\":", "x", this->x);',
        '::BECore::JsonWrite::SaveField(w, "\\"y\\":", "y", this->y);',
    ]


def test_a_class_without_fields_writes_an_empty_object(generated):
    body = writer(generated, "Bare")
    assert "SaveField" not in body
    assert "w.BeginObject();" in body and "w.EndObject();" in body
    assert "SaveField" not in members(generated, "Bare")


def test_a_class_with_a_reflected_parent_merges_its_sorted_keys_into_its_parent(generated):
    body = members(generated, "Child")
    assert 'static constexpr ::BECore::StringView kKeys[] = {\n        "extra",\n    };' in body
    assert "case 0:" in body and '::BECore::JsonWrite::SaveField(writer, "\\"extra\\":", "extra", this->extra);' in body
    assert "::BECore::JsonWrite::MemberMerge merge(kKeys, std::size(kKeys), writeMember, outer);" in body
    assert "Plain::WriteJsonMembers(w, &merge);" in body
    assert "WriteSerializedObject" not in generated.split("Child::WriteJson(")[1].split("\n}\n")[0]
    assert "Child::WriteJsonMembers(w, nullptr);" in writer(generated, "Child")


def test_a_child_without_fields_still_passes_the_merge_up(generated):
    assert "Child::WriteJsonMembers(w, &merge);" in members(generated, "Grandchild")
    assert "MemberMerge merge(nullptr, 0, writeMember, outer);" in members(generated, "Grandchild")


def test_an_event_writes_its_fields_in_sorted_order(generated):
    assert calls(writer(generated, "Ping")) == [
        '::BECore::JsonWrite::SaveField(w, "\\"at\\":", "at", _at);',
        '::BECore::JsonWrite::SaveField(w, "\\"id\\":", "id", id);',
    ]


def test_an_event_without_fields_has_no_definition(generated):
    assert "Silent::WriteJson" not in generated
