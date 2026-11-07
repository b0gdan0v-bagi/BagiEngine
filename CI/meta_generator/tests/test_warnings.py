"""A reflection marker the parser cannot turn into metadata is reported, never dropped silently."""

import importlib.util
import json
import textwrap
from pathlib import Path

import pytest

from core.cache import MetadataCache
from core.models import FileMetadata, ParseWarning
from core.parser import create_parser

TOOL_DIR = Path(__file__).resolve().parents[1]


def kinds_and_lines(warnings):
    return [(w.kind, w.line) for w in warnings]


# ---------------------------------------------------------------------------
# clean input
# ---------------------------------------------------------------------------

def test_clean_header_has_no_warnings(parse):
    classes, warnings = parse("""
        struct Player {
            BE_CLASS(Player)
            BE_REFLECT_FIELD int health = 100;
            BE_REFLECT_FIELD eastl::vector<int> items;
            BE_FUNCTION void Hit(int damage);
            BE_FUNCTION virtual void Update() = 0;
        };
    """)
    assert [c.name for c in classes] == ["Player"]
    assert len(classes[0].fields) == 2
    assert len(classes[0].methods) == 2
    assert warnings == []


def test_header_without_markers_has_no_warnings(parse):
    classes, warnings = parse("""
        class Plain { int _x = 0; };
    """)
    assert classes == []
    assert warnings == []


# ---------------------------------------------------------------------------
# (a) class markers
# ---------------------------------------------------------------------------

def test_class_marker_with_final_is_reported(parse):
    classes, warnings = parse("""
        class Foo final : public Bar {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]
    assert "BE_CLASS(Foo)" in warnings[0].message


def test_class_marker_with_export_macro_is_reported(parse):
    classes, warnings = parse("""
        class MY_API Foo {
            BE_CLASS(Foo)
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]


def test_event_marker_without_class_is_reported(parse):
    classes, warnings = parse("""
        // a header with only a stray marker
        BE_EVENT(Stray)
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2)]
    assert "BE_EVENT(Stray)" in warnings[0].message


def test_class_marker_naming_another_class_is_reported(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Other)
            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert classes == []
    assert ("class", 2) in kinds_and_lines(warnings)
    assert "'Foo'" in warnings[0].message


def test_outer_marker_after_a_nested_class_marker_is_reported(parse):
    classes, warnings = parse("""
        struct Outer {
            struct Inner {
                BE_CLASS(Inner)
            };
            BE_CLASS(Outer)
        };
    """)
    assert [c.name for c in classes] == ["Inner"]
    assert kinds_and_lines(warnings) == [("class", 5)]


def test_two_classes_in_one_file_are_not_warned(parse):
    classes, warnings = parse("""
        struct A { BE_CLASS(A) BE_REFLECT_FIELD int a = 0; };
        struct B { BE_CLASS(B) BE_REFLECT_FIELD int b = 0; };
    """)
    assert [c.name for c in classes] == ["A", "B"]
    assert warnings == []


# ---------------------------------------------------------------------------
# (b) field markers
# ---------------------------------------------------------------------------

def test_array_field_is_reported(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD float _m[16];
            BE_REFLECT_FIELD int ok = 0;
        };
    """)
    assert [f.name for f in classes[0].fields] == ["ok"]
    assert kinds_and_lines(warnings) == [("field", 3)]


def test_field_outside_a_class_is_reported(parse):
    classes, warnings = parse("""
        BE_REFLECT_FIELD int lonely = 0;
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("field", 1)]
    assert "outside every class" in warnings[0].message


def test_field_in_a_class_without_a_marker_is_reported(parse):
    classes, warnings = parse("""
        struct NotReflected {
            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("field", 2)]
    assert "'NotReflected'" in warnings[0].message


def test_field_in_the_unparsed_class_gets_its_own_warning(parse):
    classes, warnings = parse("""
        class Foo final {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD int a = 0;
        };
    """)
    assert classes == []
    assert kinds_and_lines(warnings) == [("class", 2), ("field", 3)]


# ---------------------------------------------------------------------------
# (c) method markers
# ---------------------------------------------------------------------------

def test_inline_body_swallowing_the_next_marker_is_reported_once(parse):
    classes, warnings = parse("""
        struct IWidget {
            BE_CLASS(IWidget)
            BE_FUNCTION virtual void Update() = 0;
            BE_FUNCTION virtual void DrawScreen() {}
            BE_FUNCTION virtual void Draw() = 0;
        };
    """)
    assert kinds_and_lines(warnings) == [("method", 4)]
    assert "next marker" in warnings[0].message
    # The merge itself is still there (one method for two markers); the warning is what this test pins.
    assert len(classes[0].methods) == 2


def test_inline_body_alone_is_reported(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_FUNCTION void Run() { Step(); }
        };
    """)
    assert kinds_and_lines(warnings) == [("method", 3)]
    assert "inline body" in warnings[0].message


def test_braced_default_argument_is_not_an_inline_body(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_FUNCTION void Run(std::pair<int, int> p = std::pair<int, int>{1, 2});
        };
    """)
    assert [m.name for m in classes[0].methods] == ["Run"]
    assert warnings == []


def test_method_without_return_type_is_reported(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_FUNCTION Foo();
        };
    """)
    assert classes[0].methods == []
    assert kinds_and_lines(warnings) == [("method", 3)]


def test_method_outside_a_class_is_reported(parse):
    classes, warnings = parse("""
        BE_FUNCTION void Free();
    """)
    assert kinds_and_lines(warnings) == [("method", 1)]


# ---------------------------------------------------------------------------
# (d) nested classes
# ---------------------------------------------------------------------------

def test_field_in_a_nested_class_is_reported(parse):
    classes, warnings = parse("""
        struct Outer {
            BE_CLASS(Outer)
            struct Inner {
                BE_REFLECT_FIELD int x = 0;
            };
            BE_REFLECT_FIELD int y = 0;
        };
    """)
    assert kinds_and_lines(warnings) == [("field", 4)]
    assert "'Inner'" in warnings[0].message and "'Outer'" in warnings[0].message


def test_method_in_a_nested_class_is_reported(parse):
    classes, warnings = parse("""
        struct Outer {
            BE_CLASS(Outer)
            struct Inner {
                BE_FUNCTION void Go();
            };
        };
    """)
    assert kinds_and_lines(warnings) == [("method", 4)]


# ---------------------------------------------------------------------------
# what is not a marker
# ---------------------------------------------------------------------------

def test_markers_in_comments_do_not_warn(parse):
    classes, warnings = parse("""
        // BE_CLASS(Nope)
        /* BE_REFLECT_FIELD int a;
           BE_FUNCTION void F(); */
        struct Foo {
            BE_CLASS(Foo)
            // BE_REFLECT_FIELD int hidden;
            BE_REFLECT_FIELD int shown = 0;  // BE_FUNCTION void Nope();
        };
    """)
    assert [f.name for f in classes[0].fields] == ["shown"]
    assert warnings == []


def test_markers_in_defines_do_not_warn(parse):
    classes, warnings = parse("""
        #define BE_CLASS(ClassName, ...) \\
            static int Meta();
        #define BE_REFLECT_FIELD [[clang::annotate("reflect_field")]]
        #define BE_FUNCTION      /* BE_FUNCTION */
        #define MY_FIELD BE_REFLECT_FIELD
        #if 1
        struct Foo { BE_CLASS(Foo) };
        #endif
    """)
    assert [c.name for c in classes] == ["Foo"]
    assert warnings == []


def test_markers_in_strings_do_not_warn(parse):
    classes, warnings = parse("""
        struct Foo {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD const char* a = "BE_REFLECT_FIELD int b;";
            BE_REFLECT_FIELD const char* c = "// not a comment";
            BE_REFLECT_FIELD int d = 0;
        };
        const char* kRaw = R"x(BE_CLASS(Raw) )" )x";
    """)
    assert [f.name for f in classes[0].fields] == ["a", "c", "d"]
    assert warnings == []


def test_line_numbers_survive_comments_and_strings(parse):
    classes, warnings = parse("""
        /* one
           two
           three */
        const char* kText = "a
        // b";
        struct Foo {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD float _m[4];
        };
    """)
    assert kinds_and_lines(warnings) == [("field", 8)]


# ---------------------------------------------------------------------------
# cache
# ---------------------------------------------------------------------------

def test_warnings_survive_a_cache_round_trip(tmp_path):
    header = tmp_path / "Foo.h"
    header.write_text("struct Foo {\n BE_CLASS(Foo)\n BE_REFLECT_FIELD float _m[4];\n};\n", encoding="utf-8")
    parser = create_parser([], set())
    classes, enums = parser.parse_file(header)

    cache = MetadataCache(tmp_path / "cache" / "metadata_cache.json")
    cache.update_file(header, classes, enums, parser.last_warnings)
    cache.save()

    reloaded = MetadataCache(tmp_path / "cache" / "metadata_cache.json")
    assert reloaded.load()
    meta = reloaded.files[str(header.resolve())]
    assert meta.warnings == parser.last_warnings
    assert meta.warnings[0].line == 3


def test_update_file_without_warnings_still_works(tmp_path):
    header = tmp_path / "Foo.h"
    header.write_text("struct Foo {};\n", encoding="utf-8")
    cache = MetadataCache(tmp_path / "metadata_cache.json")
    cache.update_file(header, [], [])
    assert cache.files[str(header.resolve())].warnings == []


def test_file_metadata_from_dict_tolerates_a_missing_warnings_key():
    meta = FileMetadata.from_dict({"path": "a.h", "content_hash": "x", "last_scanned": "now"})
    assert meta.warnings == []
    assert ParseWarning.from_dict(ParseWarning(3, "field", "m").to_dict()) == ParseWarning(3, "field", "m")


def test_an_old_cache_version_is_discarded(tmp_path):
    path = tmp_path / "metadata_cache.json"
    path.write_text(json.dumps({"version": "2.1", "files": {"a.h": {"path": "a.h"}}}), encoding="utf-8")
    cache = MetadataCache(path)
    assert cache.load() is False
    assert cache.files == {}


# ---------------------------------------------------------------------------
# the command line
# ---------------------------------------------------------------------------

def run_main(monkeypatch, capsys, source_dir, tmp_path, *extra):
    pytest.importorskip("jinja2")
    # `meta_generator` alone names the package directory when pytest puts CI/ on sys.path
    spec = importlib.util.spec_from_file_location("meta_generator_cli", TOOL_DIR / "meta_generator.py")
    meta_generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(meta_generator)

    argv = [
        "meta_generator.py",
        "--source-dir", str(source_dir),
        "--output-dir", str(tmp_path / "out"),
        "--cache-dir", str(tmp_path / "cache"),
        *extra,
    ]
    monkeypatch.setattr("sys.argv", argv)
    code = meta_generator.main()
    captured = capsys.readouterr()
    return code, captured.err


@pytest.fixture
def broken_source(tmp_path):
    source = tmp_path / "src"
    source.mkdir()
    (source / "Foo.h").write_text(textwrap.dedent("""\
        class Foo final : public Bar {
            BE_CLASS(Foo)
            BE_REFLECT_FIELD float _m[16];
        };
    """), encoding="utf-8")
    return source


def test_warnings_do_not_change_the_exit_code(monkeypatch, capsys, tmp_path, broken_source):
    code, err = run_main(monkeypatch, capsys, broken_source, tmp_path)
    assert code == 0
    assert "Foo.h:2: warning: reflection: BE_CLASS(Foo)" in err


def test_warnings_as_errors_exits_with_one(monkeypatch, capsys, tmp_path, broken_source):
    code, err = run_main(monkeypatch, capsys, broken_source, tmp_path, "--warnings-as-errors")
    assert code == 1
    assert "warning: reflection:" in err


def test_warnings_are_printed_again_from_the_cache(monkeypatch, capsys, tmp_path, broken_source):
    _, first = run_main(monkeypatch, capsys, broken_source, tmp_path)
    _, second = run_main(monkeypatch, capsys, broken_source, tmp_path)
    assert first and second == first


def test_clean_source_exits_with_zero_even_as_errors(monkeypatch, capsys, tmp_path):
    source = tmp_path / "src"
    source.mkdir()
    (source / "Ok.h").write_text("struct Ok {\n BE_CLASS(Ok)\n BE_REFLECT_FIELD int a = 0;\n};\n", encoding="utf-8")
    code, err = run_main(monkeypatch, capsys, source, tmp_path, "--warnings-as-errors")
    assert code == 0
    assert err == ""
