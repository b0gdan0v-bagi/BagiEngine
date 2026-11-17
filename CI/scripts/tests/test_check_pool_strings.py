"""check_pool_strings.py on small trees made in a temp folder."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_pool_strings as pool  # noqa: E402


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def found(root: Path, excludes=(), allowlists=()) -> list[str]:
    return pool.check(root, list(excludes), [Path(a) for a in allowlists])


def call(argument: str, function: str = "Intern") -> str:
    return f"void F() {{\n    auto x = PoolString::{function}({argument});\n}}\n"


def where(problem: str) -> tuple[str, str, str]:
    """(path, line, rule id) of a line of the output."""
    path, line, rest = problem.split(":", 2)
    return path, line, rest.split("]")[0].strip(" [")


def loose(count: int) -> str:
    return "".join(f'void F{i}() {{ Use("t{i}"_intern); }}\n' for i in range(count))


# ---- rule `literal`

@pytest.mark.parametrize(
    "argument",
    [
        '"x"',
        'StringView{"x"}',
        'StringView("x")',
        'eastl::string_view{"x"}',
        'std::string_view{"x"}',
        'string_view{"x"}',
        '"x"sv',
        'String("x")',
        'std::string{"x"}',
        '"a" "b"',
        'cond ? "a" : "b"',
        'cond ? StringView{"a"} : "b"',
        'a ? b ? "a" : "c" : "d"',
        '  "x"  ',
    ],
)
def test_a_literal_argument_of_intern_is_found(tmp_path, argument):
    write(tmp_path, "src/Foo.cpp", call(argument))
    problems = found(tmp_path)
    assert [where(p) for p in problems] == [("src/Foo.cpp", "2", "literal")]


def test_the_argument_on_the_next_line_is_found(tmp_path):
    write(tmp_path, "src/Foo.cpp", 'void F() {\n    auto x = PoolString::Intern(\n        "x");\n}\n')
    assert [where(p) for p in found(tmp_path)] == [("src/Foo.cpp", "2", "literal")]


def test_the_becore_qualifier_is_found(tmp_path):
    write(tmp_path, "src/Foo.cpp", call('"x"').replace("PoolString::", "BECore::PoolString::"))
    assert [where(p) for p in found(tmp_path)] == [("src/Foo.cpp", "2", "literal")]


def test_the_global_qualifier_is_found(tmp_path):
    write(tmp_path, "src/Foo.cpp", call('"x"').replace("PoolString::", "::BECore::PoolString::"))
    assert len(found(tmp_path)) == 1


def test_find_of_a_literal_is_found_in_a_source_file(tmp_path):
    write(tmp_path, "src/Foo.cpp", call('"x"', "Find") + call('StringView{"x"}', "Find"))
    assert [where(p) for p in found(tmp_path)] == [("src/Foo.cpp", "2", "literal"), ("src/Foo.cpp", "5", "literal")]


def test_find_of_a_view_of_a_literal_passes_in_a_test_file(tmp_path):
    write(tmp_path, "tests/FooTest.cpp", call('StringView{"x"}', "Find") + call('string_view{"x"}', "Find"))
    assert found(tmp_path) == []


def test_a_nested_test_folder_is_a_test_file(tmp_path):
    write(tmp_path, "tools/stress/tests/FooTest.cpp", call('StringView{"x"}', "Find"))
    assert found(tmp_path) == []


def test_find_of_a_plain_literal_and_intern_of_a_view_fail_in_a_test_file(tmp_path):
    write(tmp_path, "tests/FooTest.cpp", call('"x"', "Find") + call('StringView{"x"}'))
    assert [where(p) for p in found(tmp_path)] == [("tests/FooTest.cpp", "2", "literal"), ("tests/FooTest.cpp", "5", "literal")]


def test_find_of_a_string_of_a_literal_fails_in_a_test_file(tmp_path):
    write(tmp_path, "tests/FooTest.cpp", call('String("x")', "Find"))
    assert len(found(tmp_path)) == 1


@pytest.mark.parametrize(
    "code",
    [
        'PoolString::Intern(Format("x{}", n))',
        'PoolString::Intern(StringView(buf))',
        'PoolString::Intern(StringView{buf, 3})',
        'PoolString::Intern(name)',
        'PoolString::Intern(prefix + "x")',
        'PoolString::Intern(cond ? "a" : name)',
        'PoolString::Intern(cond ? name : "b")',
        'PoolString::Intern(StringView{})',
        'PoolString::Intern(Pick("x"))',
        'pool.storage.Intern("x")',
        'attrs.Find("text")',
        'UIStrings::Find("x")',
        'Details::PoolString::Intern("x")',
        'PoolString::Intern(String("x") + name)',
        'PoolString::InternBatch(texts, out)',
    ],
)
def test_other_arguments_and_other_functions_are_not_found(tmp_path, code):
    write(tmp_path, "src/Foo.cpp", f"void F() {{\n    auto x = {code};\n}}\n")
    assert found(tmp_path) == []


def test_a_call_inside_a_string_literal_is_no_hit(tmp_path):
    write(tmp_path, "src/Foo.cpp", 'const char* kDoc = "PoolString::Intern(\\"x\\")";\n')
    assert found(tmp_path) == []


def test_brackets_in_a_string_do_not_end_the_argument(tmp_path):
    write(tmp_path, "src/Foo.cpp", call('Pick(")", "x")'))
    assert found(tmp_path) == []


# ---- rule `duplicate`

def test_the_same_text_in_two_source_files_is_found_and_names_both(tmp_path):
    write(tmp_path, "src/A.cpp", 'void A() {\n    Use("same"_intern);\n}\n')
    write(tmp_path, "src/B.cpp", 'void B() {\n\n    Use("same"_intern);\n}\n')
    problems = found(tmp_path)
    assert len(problems) == 1
    assert where(problems[0]) == ("src/A.cpp", "2", "duplicate")
    assert '"same"' in problems[0]
    assert "src/A.cpp:2" in problems[0] and "src/B.cpp:3" in problems[0]


def test_the_same_text_twice_in_one_file_is_not_found(tmp_path):
    write(tmp_path, "src/A.cpp", 'void A() { Use("same"_intern); Use("same"_intern); }\n')
    assert found(tmp_path) == []


def test_the_same_text_in_a_test_and_a_source_file_is_not_found(tmp_path):
    write(tmp_path, "src/A.cpp", 'void A() { Use("same"_intern); }\n')
    write(tmp_path, "tests/ATest.cpp", 'void T() { Use("same"_intern); }\n')
    write(tmp_path, "tests/BTest.cpp", 'void U() { Use("same"_intern); }\n')
    assert found(tmp_path) == []


def test_two_different_texts_are_not_found(tmp_path):
    write(tmp_path, "src/A.cpp", 'void A() { Use("one"_intern); }\n')
    write(tmp_path, "src/B.cpp", 'void B() { Use("two"_intern); }\n')
    assert found(tmp_path) == []


def test_named_constants_count_for_the_duplicate_rule(tmp_path):
    write(tmp_path, "src/A.cpp", "namespace { struct Ids { static constexpr auto k = \"same\"_intern; }; }\n")
    write(tmp_path, "src/B.cpp", "namespace { struct Ids { static constexpr auto k = \"same\"_intern; }; }\n")
    assert [where(p) for p in found(tmp_path)] == [("src/A.cpp", "1", "duplicate")]


# ---- rule `loose`

def test_three_loose_in_a_source_file_are_found_at_the_third(tmp_path):
    write(tmp_path, "src/A.cpp", loose(3))
    problems = found(tmp_path)
    assert [where(p) for p in problems] == [("src/A.cpp", "3", "loose")]
    assert "lines 1, 2, 3" in problems[0]


def test_two_loose_in_a_source_file_pass(tmp_path):
    write(tmp_path, "src/A.cpp", loose(2))
    assert found(tmp_path) == []


NAMED = (
    "namespace {\n"
    "    struct Ids {\n"
    '        static constexpr auto a = "n0"_intern;\n'
    '        static constexpr auto b = "n1"_intern;\n'
    '        inline static constexpr auto c = "n2"_intern;\n'
    '        static constexpr PoolString d = "n3"_intern;\n'
    "    };\n"
    '    inline constexpr auto e = "n4"_intern;\n'
    "    constexpr auto f =\n"
    '        "n5"_intern;\n'
    '    constexpr PoolString g{"n6"_intern};\n'
    '    constexpr PoolString h {\n        "n7"_intern };\n'
    '    constexpr auto i = "n8"_intern, j = "n9"_intern;\n'
    "}\n"
)


def test_named_constants_in_any_form_pass_however_many(tmp_path):
    write(tmp_path, "src/A.cpp", NAMED)
    assert found(tmp_path) == []


def test_named_constants_are_not_counted_with_the_loose_ones(tmp_path):
    write(tmp_path, "src/A.cpp", NAMED + loose(2))
    assert found(tmp_path) == []
    write(tmp_path, "src/A.cpp", NAMED + loose(3))
    assert [where(p) for p in found(tmp_path)] == [("src/A.cpp", str(NAMED.count("\n") + 3), "loose")]


def test_a_non_constexpr_initializer_is_loose(tmp_path):
    write(tmp_path, "src/A.cpp", 'static const auto a = "t0"_intern;\nstatic auto b = "t1"_intern;\nstatic auto c = "t2"_intern;\n')
    assert [where(p) for p in found(tmp_path)] == [("src/A.cpp", "3", "loose")]


def test_a_constexpr_statement_before_a_loose_one_does_not_name_it(tmp_path):
    write(tmp_path, "src/A.cpp", 'constexpr int kA = 1;\nvoid F() { Use("t0"_intern); }\nvoid G() { Use("t1"_intern); }\nvoid H() { Use("t2"_intern); }\n')
    assert [where(p) for p in found(tmp_path)] == [("src/A.cpp", "4", "loose")]


def test_a_test_file_with_five_loose_passes(tmp_path):
    write(tmp_path, "tests/ATest.cpp", loose(5))
    assert found(tmp_path) == []


# ---- rule `header`

def test_a_loose_intern_in_a_header_is_found_for_each_place(tmp_path):
    write(tmp_path, "src/A.h", '#pragma once\ninline void F() { Use("h0"_intern); }\ninline void G() { Use("h1"_intern); }\n')
    assert [where(p) for p in found(tmp_path)] == [("src/A.h", "2", "header"), ("src/A.h", "3", "header")]


@pytest.mark.parametrize("name", ["A.hpp", "A.hh", "A.hxx"])
def test_the_other_header_suffixes_are_headers(tmp_path, name):
    write(tmp_path, f"src/{name}", 'inline void F() { Use("h0"_intern); }\n')
    assert [where(p)[2] for p in found(tmp_path)] == ["header"]


def test_a_struct_member_and_a_namespace_of_constants_in_a_header_pass(tmp_path):
    write(
        tmp_path,
        "src/A.h",
        '#pragma once\nstruct Ids { static constexpr auto a = "a"_intern; };\n'
        'namespace Cmd {\n    inline constexpr auto b = "b"_intern;\n    inline constexpr auto c = "c"_intern;\n}\n',
    )
    assert found(tmp_path) == []


def test_a_header_is_not_also_counted_by_the_loose_rule(tmp_path):
    write(tmp_path, "src/A.h", "".join(f'inline void F{i}() {{ Use("h{i}"_intern); }}\n' for i in range(3)))
    assert [where(p)[2] for p in found(tmp_path)] == ["header"] * 3


def test_a_header_under_tests_passes(tmp_path):
    write(tmp_path, "tests/Common/A.h", 'inline void F() { Use("h0"_intern); }\n')
    assert found(tmp_path) == []


# ---- comments, the operator and vendoring

def test_comments_hold_no_hit(tmp_path):
    write(
        tmp_path,
        "src/A.cpp",
        '// "a"_intern PoolString::Intern("a")\n'
        '/// "b"_intern\n'
        '/* "c"_intern\n   PoolString::Intern("c") */\n'
        'void F() { Use(1); /* "d"_intern */ }\n'
        'void G() { Use(2); } // "e"_intern\n',
    )
    write(tmp_path, "src/B.h", '#pragma once\n// "a"_intern PoolString::Find("x")\n')
    assert found(tmp_path) == []


def test_the_definition_of_the_operator_is_no_hit(tmp_path):
    write(tmp_path, "src/A.h", "#pragma once\nconsteval auto operator\"\"_intern() { return 1; }\nconsteval auto operator \"\"_intern() { return 2; }\n")
    assert found(tmp_path) == []


def test_a_text_followed_by_more_word_characters_is_no_intern(tmp_path):
    write(tmp_path, "src/A.h", '#pragma once\ninline auto a = "x"_internal;\n')
    assert found(tmp_path) == []


def test_a_vendored_file_is_not_scanned(tmp_path):
    write(tmp_path, "src/A.cpp", "// Vendored: fmt\n" + call('"x"'))
    assert found(tmp_path) == []


def test_a_folder_with_a_git_entry_is_not_scanned(tmp_path):
    write(tmp_path, "engine/.git", "gitdir: ../.git/modules/engine\n")
    write(tmp_path, "engine/src/A.cpp", call('"x"'))
    assert found(tmp_path) == []


def test_an_excluded_path_is_not_scanned(tmp_path):
    write(tmp_path, "third/A.cpp", call('"x"'))
    write(tmp_path, "src/B.cpp", call('"x"'))
    assert [where(p)[0] for p in found(tmp_path, excludes=["third"])] == ["src/B.cpp"]


def test_a_raw_string_with_a_quote_does_not_hide_the_next_line(tmp_path):
    write(tmp_path, "src/A.cpp", 'const char* k = R"(a " b)";\n' + call('"x"'))
    assert [where(p) for p in found(tmp_path)] == [("src/A.cpp", "3", "literal")]


# ---- the allowlist

def allowlist(tmp_path: Path, text: str, name: str = "allow.txt") -> Path:
    return write(tmp_path / "lists", name, text)


def test_a_literal_row_with_a_glob_drops_the_matching_file_only(tmp_path):
    write(tmp_path, "tests/PoolTest.cpp", call('StringView{"x"}'))
    write(tmp_path, "tests/OtherTest.cpp", call('StringView{"x"}'))
    rows = allowlist(tmp_path, "literal | tests/Pool*Test.cpp | the pool's own tests\n")
    assert [where(p) for p in found(tmp_path, allowlists=[rows])] == [("tests/OtherTest.cpp", "2", "literal")]


def test_a_star_of_a_glob_crosses_folders(tmp_path):
    write(tmp_path, "tests/Deep/PoolTest.cpp", call('StringView{"x"}'))
    rows = allowlist(tmp_path, "literal | tests/*Test.cpp | all\n")
    assert found(tmp_path, allowlists=[rows]) == []


def test_a_row_of_another_rule_does_not_drop_a_finding(tmp_path):
    write(tmp_path, "src/A.cpp", call('"x"'))
    rows = allowlist(tmp_path, "loose | src/A.cpp | wrong rule\n")
    problems = found(tmp_path, allowlists=[rows])
    assert [where(p)[2] for p in problems] == ["literal", "allowlist"]


def test_a_loose_and_a_header_row_drop_by_path(tmp_path):
    write(tmp_path, "src/A.cpp", loose(3))
    write(tmp_path, "src/B.h", 'inline void F() { Use("h"_intern); }\n')
    rows = allowlist(tmp_path, "loose | src/A.cpp | reason\nheader | src/B.h | reason\n")
    assert found(tmp_path, allowlists=[rows]) == []


def test_a_duplicate_row_names_the_text(tmp_path):
    write(tmp_path, "src/A.cpp", 'void A() { Use("same"_intern); }\n')
    write(tmp_path, "src/B.cpp", 'void B() { Use("same"_intern); }\n')
    write(tmp_path, "src/C.cpp", 'void C() { Use("other"_intern); }\n')
    write(tmp_path, "src/D.cpp", 'void D() { Use("other"_intern); }\n')
    rows = allowlist(tmp_path, "duplicate | same | shared on purpose\n")
    assert [where(p) for p in found(tmp_path, allowlists=[rows])] == [("src/C.cpp", "1", "duplicate")]


def test_a_row_that_matched_nothing_is_a_stale_finding(tmp_path):
    write(tmp_path, "src/A.cpp", "void F() {}\n")
    rows = allowlist(tmp_path, "# header\n\nliteral | src/Gone.cpp | it was fixed\n")
    problems = found(tmp_path, allowlists=[rows])
    assert len(problems) == 1
    assert problems[0].startswith(f"{rows}:3: [allowlist] stale allowlist row")


def test_comments_and_blank_lines_of_an_allowlist_are_ignored(tmp_path):
    write(tmp_path, "src/A.cpp", call('"x"'))
    rows = allowlist(tmp_path, "# comment\n\n   \nliteral | src/A.cpp | reason | with a bar\n# last\n")
    assert found(tmp_path, allowlists=[rows]) == []


def test_several_allowlists_add_up(tmp_path):
    write(tmp_path, "src/A.cpp", call('"x"'))
    write(tmp_path, "src/B.cpp", call('"y"'))
    first = allowlist(tmp_path, "literal | src/A.cpp | reason\n", "first.txt")
    second = allowlist(tmp_path, "literal | src/B.cpp | reason\n", "second.txt")
    assert found(tmp_path, allowlists=[first, second]) == []


@pytest.mark.parametrize(
    "row",
    [
        "literal | src/A.cpp |\n",
        "literal | src/A.cpp\n",
        "literal\n",
        "unknown | src/A.cpp | reason\n",
        "literal |  | reason\n",
    ],
)
def test_a_bad_row_raises_and_main_returns_two(tmp_path, capsys, row):
    write(tmp_path, "src/A.cpp", "void F() {}\n")
    rows = allowlist(tmp_path, row)
    with pytest.raises(pool.AllowlistError):
        found(tmp_path, allowlists=[rows])
    assert pool.main(["--root", str(tmp_path), "--allowlist", str(rows)]) == 2
    assert "Traceback" not in capsys.readouterr().err


def test_a_missing_allowlist_is_a_bad_argument(tmp_path):
    assert pool.main(["--root", str(tmp_path), "--allowlist", str(tmp_path / "none.txt")]) == 2


# ---- main

def test_main_is_zero_on_a_clean_tree(tmp_path, capsys):
    write(tmp_path, "src/A.cpp", "void F() {}\n")
    assert pool.main(["--root", str(tmp_path)]) == 0
    assert capsys.readouterr().out == ""


def test_main_is_one_with_findings_and_the_last_line_holds_the_count(tmp_path, capsys):
    write(tmp_path, "src/A.cpp", call('"x"') + call('"y"'))
    assert pool.main(["--root", str(tmp_path)]) == 1
    lines = capsys.readouterr().out.splitlines()
    assert lines[0].startswith("src/A.cpp:2: [literal]")
    assert lines[-1].startswith("2 ")


def test_main_passes_the_excludes_and_the_allowlists_on(tmp_path):
    write(tmp_path, "third/A.cpp", call('"x"'))
    write(tmp_path, "src/B.cpp", call('"y"'))
    rows = allowlist(tmp_path, "literal | src/B.cpp | reason\n")
    assert pool.main(["--root", str(tmp_path), "--exclude", "third", "--allowlist", str(rows)]) == 0


def test_main_is_two_for_a_missing_root_and_an_unknown_option(tmp_path):
    assert pool.main(["--root", str(tmp_path / "missing")]) == 2
    assert pool.main(["--no-such-option"]) == 2
