"""The codemod on small trees: every rule before / after, the places it must not touch, idempotence and the flags."""

from pathlib import Path
from typing import Dict, Sequence

import pytest

import codemod

ALL = list(codemod.FAMILIES)


def convert(source: str, families: Sequence[str] = ALL, is_test: bool = False) -> str:
    out, _, _ = codemod.transform(source, set(families), is_test)
    return out


def in_namespace(body: str) -> str:
    return "namespace BECore {\n" + body + "\n}\n"


def make_tree(root: Path, files: Dict[str, str]) -> None:
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


# ---- the rules ------------------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "before, after",
    [
        ("eastl::optional<int> a; std::optional<eastl::string> b;", "Optional<int> a; Optional<String> b;"),
        ("return eastl::nullopt;", "return std::nullopt;"),
        ("eastl::nullopt_t x; auto o = eastl::make_optional(3);", "std::nullopt_t x; auto o = std::make_optional(3);"),
        ("Optional<int> o{eastl::in_place, 3};", "Optional<int> o{std::in_place, 3};"),
        ("eastl::string_view a; std::string_view b;", "StringView a; StringView b;"),
        ("eastl::string s; eastl::string::size_type n = eastl::string::npos;", "String s; String::size_type n = String::npos;"),
        ("eastl::fixed_string<char, 32> a; eastl::fixed_string<char, 16, false> b;", "FixedString<32> a; FixedString<16, false> b;"),
        ("auto t = eastl::to_string(3);", "auto t = ToString(3);"),
        ("auto t = eastl::string(std::to_string(n).c_str());", "auto t = ToString(n);"),
        ("auto t = eastl::string{std::to_string(a + b).c_str()};", "auto t = ToString(a + b);"),
        ("eastl::vector<int> v; std::vector<eastl::string> w;", "Vector<int> v; Vector<String> w;"),
        ("eastl::fixed_vector<int, 4> a; eastl::fixed_vector<int, 4, true> b;", "SmallVector<int, 4> a; SmallVector<int, 4> b;"),
        ("eastl::fixed_vector<int, 4, false> a;", "FixedVector<int, 4> a;"),
        ("eastl::fixed_vector<eastl::pair<A, B>, 4> a;", "SmallVector<eastl::pair<A, B>, 4> a;"),
        ("eastl::fixed_vector<eastl::pair<A, B>, 4, false> a;", "FixedVector<eastl::pair<A, B>, 4> a;"),
        ("eastl::array<int, 3> a; eastl::span<const int> s; eastl::unique_ptr<X> p;", "Array<int, 3> a; Span<const int> s; UniquePtr<X> p;"),
        ("eastl::hash_map<eastl::string, int> a; eastl::unordered_map<int, int> b; std::unordered_map<int, int> c;", "HashMap<String, int> a; HashMap<int, int> b; HashMap<int, int> c;"),
        ("eastl::map<int, eastl::vector<int>> a;", "Map<int, Vector<int>> a;"),
        ("eastl::fixed_hash_map<int, int, 8> a;", "FixedHashMap<int, int, 8> a;"),
        ("eastl::vector<eastl::vector<eastl::pair<int, eastl::string>>> a;", "Vector<Vector<eastl::pair<int, String>>> a;"),
    ],
)
def test_rule(before: str, after: str) -> None:
    assert convert(in_namespace(before)) == in_namespace(after)


@pytest.mark.parametrize(
    "before, after",
    [
        ("auto v = std::string_view{s.data(), s.size()};", "auto v = ToStd(s);"),
        ("auto v = std::string_view(a.b.data(), a.b.size());", "auto v = ToStd(a.b);"),
        ("auto v = eastl::string_view(s.data(), s.size());", "auto v = FromStd(s);"),
        ("auto v = eastl::string_view(f(x, y).data(), f(x, y).size());", "auto v = FromStd(f(x, y));"),
        # the two arguments are not one object, or not that shape: only the type name moves
        ("auto v = eastl::string_view(a.data(), b.size());", "auto v = StringView(a.data(), b.size());"),
        ("auto v = eastl::string_view(s.data(), 3);", "auto v = StringView(s.data(), 3);"),
    ],
)
def test_view_shapes(before: str, after: str) -> None:
    assert convert(in_namespace(before)) == in_namespace(after)


def test_a_family_moves_only_its_own_names() -> None:
    source = in_namespace("eastl::string a; eastl::string_view b; eastl::vector<int> c; eastl::optional<int> d; eastl::hash_map<int, int> e;")
    assert convert(source, ["strings"]) == in_namespace("String a; eastl::string_view b; eastl::vector<int> c; eastl::optional<int> d; eastl::hash_map<int, int> e;")
    assert convert(source, ["maps", "optionals"]) == in_namespace("eastl::string a; eastl::string_view b; eastl::vector<int> c; Optional<int> d; HashMap<int, int> e;")


# ---- what it leaves -------------------------------------------------------------------------------------------------------------


def test_comments_and_literals_are_kept() -> None:
    source = in_namespace(
        '// eastl::string in a comment\n/* std::vector<int>\n eastl::optional */\nconst char* a = "eastl::string";\n'
        'const char* b = R"x(eastl::vector<int> )x";\nchar c = \'"\'; int d = 1\'000; eastl::string e;\n'
    )
    expected = in_namespace(
        '// eastl::string in a comment\n/* std::vector<int>\n eastl::optional */\nconst char* a = "eastl::string";\n'
        'const char* b = R"x(eastl::vector<int> )x";\nchar c = \'"\'; int d = 1\'000; String e;\n'
    )
    assert convert(source) == expected


def test_the_std_marker_keeps_its_line_and_the_next() -> None:
    source = in_namespace("std::string a;  // std: file path\n// std: the api takes it\nstd::string b;\nstd::string c;\neastl::string d;  // std: kept\n")
    assert convert(source) == in_namespace("std::string a;  // std: file path\n// std: the api takes it\nstd::string b;\nString c;\neastl::string d;  // std: kept\n")


def test_other_spellings_are_not_matched() -> None:
    source = in_namespace("eastl::string16 a; eastl::basic_string<char> b; Foo::eastl::string c; eastl::string_view_x d; eastl::vector_map<int, int> e; eastl::multimap<int, int> f;")
    assert convert(source) == source


def test_an_allocator_argument_is_reported_and_left() -> None:
    source = in_namespace("eastl::vector<int, MyAlloc> a; eastl::fixed_vector<int, 4, true, MyAlloc> b; eastl::fixed_string<char, 8, true, MyAlloc> c; eastl::hash_map<K, V, H, E, A> d;")
    out, applied, notes = codemod.transform(source, set(ALL), False)
    assert out == source
    assert applied == []
    assert len(notes) == 4


def test_optional_of_a_reference_is_reported_and_left() -> None:
    source = in_namespace("eastl::optional<Foo&> a;")
    out, _, notes = codemod.transform(source, {"optionals"}, False)
    assert out == source
    assert "reference" in notes[0].message


def test_std_to_string_is_reported_only() -> None:
    source = in_namespace("auto a = std::to_string(3);")
    out, applied, notes = codemod.transform(source, {"strings"}, False)
    assert out == source and applied == []
    assert [n.rule for n in notes] == ["std::to_string"]
    _, _, test_notes = codemod.transform(source, {"strings"}, True)
    assert test_notes == []


def test_a_lookup_with_a_temporary_is_reported() -> None:
    source = in_namespace("auto a = m.find(eastl::string(x)); auto b = m.contains(String{x}); auto c = m.find(x); auto d = m.at(std::string(y));")
    _, _, notes = codemod.transform(source, {"maps"}, False)
    assert [n.rule for n in notes] == ["map lookup"] * 3


def test_tests_keep_std_string_and_std_vector() -> None:
    source = in_namespace("std::string a; std::vector<int> b; eastl::string c; eastl::vector<int> d; std::unordered_map<int, int> e; std::optional<int> f;")
    assert convert(source, is_test=True) == in_namespace("std::string a; std::vector<int> b; String c; Vector<int> d; HashMap<int, int> e; Optional<int> f;")


def test_tests_keep_std_string_view_and_its_shape() -> None:
    source = in_namespace("std::string_view a{s.data(), s.size()}; eastl::string_view b; auto c = eastl::string_view(x.data(), x.size());")
    assert convert(source, is_test=True) == in_namespace("std::string_view a{s.data(), s.size()}; StringView b; auto c = FromStd(x);")


# ---- the namespace --------------------------------------------------------------------------------------------------------------


def test_names_outside_becore_are_qualified() -> None:
    source = "eastl::string a;\nnamespace Other {\neastl::vector<int> b;\n}\nnamespace BECore::Sub {\neastl::string c;\n}\neastl::string d;\n"
    expected = "BECore::String a;\nnamespace Other {\nBECore::Vector<int> b;\n}\nnamespace BECore::Sub {\nString c;\n}\nBECore::String d;\n"
    assert convert(source) == expected


def test_using_namespace_becore_makes_the_plain_name_enough() -> None:
    source = "using namespace BECore;\neastl::string a;\n"
    assert convert(source) == "using namespace BECore;\nString a;\n"


def test_a_namespace_that_only_starts_with_becore_does_not_count() -> None:
    assert convert("namespace BECoreTests {\neastl::string a;\n}\n") == "namespace BECoreTests {\nBECore::String a;\n}\n"


def test_shapes_are_qualified_outside_becore() -> None:
    assert convert("auto v = eastl::string_view(s.data(), s.size());\n") == "auto v = BECore::FromStd(s);\n"


# ---- the optional include -------------------------------------------------------------------------------------------------------


def test_the_optional_header_goes_when_no_eastl_optional_is_left() -> None:
    source = "#include <EASTL/optional.h>\n#include <EASTL/vector.h>\n" + in_namespace("eastl::optional<int> a;")
    assert convert(source, ["optionals"]) == "#include <EASTL/vector.h>\n" + in_namespace("Optional<int> a;")


def test_the_optional_header_stays_while_something_needs_it() -> None:
    source = "#include <EASTL/optional.h>\n" + in_namespace("eastl::optional<int> a;  // std: x\nOptional<int> b;")
    assert convert(source, ["optionals"]).startswith("#include <EASTL/optional.h>\n")


# ---- idempotence ----------------------------------------------------------------------------------------------------------------


SAMPLE = in_namespace(
    "eastl::vector<eastl::fixed_vector<eastl::pair<int, eastl::string>, 4, false>> a;\n"
    "auto v = eastl::string_view(eastl::string(x).data(), eastl::string(x).size());\n"
    "auto t = eastl::string(std::to_string(n).c_str());\n"
    "eastl::hash_map<eastl::string, eastl::optional<std::string_view>> m;\n"
    "eastl::fixed_string<char, 8> f;\n"
)


def test_a_second_pass_changes_nothing() -> None:
    once = convert(SAMPLE)
    assert "eastl::string" not in once.replace("eastl::pair", "")
    assert convert(once) == once


def test_a_shape_around_a_name_settles_in_the_same_run() -> None:
    assert convert(in_namespace("auto v = eastl::string_view(eastl::string(x).data(), eastl::string(x).size());")) == in_namespace("auto v = FromStd(String(x));")


def test_line_endings_are_kept() -> None:
    assert convert("namespace BECore {\r\neastl::string a;\r\n}\r\n") == "namespace BECore {\r\nString a;\r\n}\r\n"


# ---- the walk and the command ---------------------------------------------------------------------------------------------------


def test_the_zones_the_layer_and_the_traits_are_skipped(tmp_path: Path) -> None:
    text = "eastl::string a;\n"
    names = [
        "src/Modules/EABase/a.h",
        "src/Modules/fmt/a.h",
        "src/Modules/RmlUi/a.h",
        "external/x/a.h",
        "build/a.h",
        "build-debug/a.h",
        "src/Widgets/Icons/a.h",
        "src/Modules/BECore/Types/Strings.h",
        "src/Modules/BECore/pch.h",
        "src/Modules/BECore/Reflection/DataAccessor.h",
        "src/Modules/BECore/Reflection/SerializationTraits.h",
        "src/Modules/BECore/Reflection/PropertyVisitorDispatch.h",
        "tests/BECore/TypesTest.cpp",
        "src/Notes.txt",
    ]
    make_tree(tmp_path, {name: text for name in names})
    make_tree(tmp_path, {"src/Modules/BECore/Own.h": text, "src/Modules/RmlUiHost/a.h": text})
    result = codemod.run(tmp_path, ["strings"], write=True)
    assert sorted(result.changed_files) == ["src/Modules/BECore/Own.h", "src/Modules/RmlUiHost/a.h"]
    for name in names:
        assert (tmp_path / name).read_text() == text


def test_the_root_may_be_a_tests_folder(tmp_path: Path) -> None:
    tests = tmp_path / "tests"
    make_tree(tests, {"a.cpp": "std::string a;\n"})
    codemod.run(tests, ["strings"], write=True)
    assert (tests / "a.cpp").read_text() == "std::string a;\n"
    src = tmp_path / "src"
    make_tree(src, {"a.cpp": "std::string a;\n", "tests/b.cpp": "std::string b;\n"})
    codemod.run(src, ["strings"], write=True)
    assert (src / "a.cpp").read_text() == "BECore::String a;\n"
    assert (src / "tests" / "b.cpp").read_text() == "std::string b;\n"


def test_main_dry_run_writes_nothing(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    make_tree(tmp_path, {"a.h": in_namespace("eastl::string a;")})
    assert codemod.main(["--root", str(tmp_path), "--family", "strings", "--dry-run"]) == 0
    assert (tmp_path / "a.h").read_text() == in_namespace("eastl::string a;")
    out = capsys.readouterr().out
    assert "eastl::string -> String: 1 files, 1 replacements" in out
    assert "would change 1 files, 1 replacements" in out


def test_main_fail_on_change_exits_one_and_writes_nothing(tmp_path: Path) -> None:
    make_tree(tmp_path, {"a.h": in_namespace("eastl::string a;")})
    assert codemod.main(["--root", str(tmp_path), "--family", "strings", "--fail-on-change"]) == 1
    assert (tmp_path / "a.h").read_text() == in_namespace("eastl::string a;")


def test_main_writes_then_a_second_run_is_clean(tmp_path: Path) -> None:
    make_tree(tmp_path, {"a.h": in_namespace("eastl::string a; eastl::vector<int> b;"), "b.cpp": "std::optional<int> c;\n"})
    assert codemod.main(["--root", str(tmp_path), "--family", "all"]) == 0
    assert (tmp_path / "a.h").read_text() == in_namespace("String a; Vector<int> b;")
    assert (tmp_path / "b.cpp").read_text() == "Optional<int> c;\n".replace("Optional", "BECore::Optional")
    assert codemod.main(["--root", str(tmp_path), "--family", "all", "--dry-run", "--fail-on-change"]) == 0


def test_main_report_lists_the_notes(tmp_path: Path, capsys: pytest.CaptureFixture) -> None:
    make_tree(tmp_path, {"a.h": in_namespace("eastl::vector<int, A> a;")})
    assert codemod.main(["--root", str(tmp_path), "--family", "containers", "--dry-run", "--report"]) == 0
    assert "a.h:2: [eastl::vector -> Vector]" in capsys.readouterr().out


def test_main_rejects_a_bad_root_and_a_bad_family(tmp_path: Path) -> None:
    assert codemod.main(["--root", str(tmp_path / "none"), "--family", "all"]) == 2
    with pytest.raises(SystemExit):
        codemod.main(["--root", str(tmp_path), "--family", "nothing"])


def test_a_file_that_is_not_utf8_is_an_error_and_the_rest_goes_on(tmp_path: Path) -> None:
    make_tree(tmp_path, {"b.h": in_namespace("eastl::string a;")})
    (tmp_path / "a.h").write_bytes(b"\xff\xfe eastl::string")
    assert codemod.main(["--root", str(tmp_path), "--family", "strings"]) == 2
    assert (tmp_path / "b.h").read_text() == in_namespace("String a;")
