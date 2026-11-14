"""check_types.py on small trees made in a temp folder."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_types as types  # noqa: E402
import nostd  # noqa: E402
import pytest  # noqa: E402

ENGINE = Path(__file__).resolve().parents[2]


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def rules_found(root: Path, relative: str = "src/Foo.cpp", **kwargs) -> list[str]:
    """The rule ids of the hits in one file of `root`."""
    return [hit.rule for hit in types.check(root, **kwargs).new if hit.path == relative]


def found(tmp_path: Path, code: str, relative: str = "src/Foo.cpp", **kwargs) -> list[str]:
    write(tmp_path, relative, code)
    return rules_found(tmp_path, relative, **kwargs)


POSITIVES = [
    ("type", "std::string s;"),
    ("type", "eastl::string s;"),
    ("type", "std::string_view v;"),
    ("type", "std::wstring w;"),
    ("type", "std::vector<int> v;"),
    ("type", "eastl::fixed_vector<int, 4> v;"),
    ("type", "eastl::fixed_string<char, 8> s;"),
    ("type", "std::array<int, 4> a;"),
    ("type", "std::span<int> s;"),
    ("type", "std::unordered_map<int, int> m;"),
    ("type", "eastl::hash_map<int, int> m;"),
    ("type", "std::map<int, int> m;"),
    ("type", "std::set<int> s;"),
    ("type", "std::unordered_set<int> s;"),
    ("type", "eastl::hash_set<int> s;"),
    ("type", "std::optional<int> o;"),
    ("type", "std::unique_ptr<int> p;"),
    ("type", "std::shared_ptr<int> p;"),
    ("type", "auto p = std::make_unique<int>();"),
    ("type", "auto p = std::make_shared<int>();"),
    ("type", "std::pair<int, int> p;"),
    ("type", "auto p = std::make_pair(1, 2);"),
    ("type", "std::variant<int, float> v;"),
    ("type", "std::tuple<int, float> t;"),
    ("function", "std::function<void()> f;"),
    ("function", "eastl::function<void()> f;"),
    ("to_string", "auto s = std::to_string(1);"),
    ("sto", "int i = std::stoi(s);"),
    ("sto", "auto i = std::stoull(s);"),
    ("sto", "auto i = std::stold(s);"),
    ("regex", "std::regex r;"),
    ("regex", "std::smatch m;"),
    ("stringstream", "std::ostringstream out;"),
    ("stringstream", "std::stringstream io;"),
    ("fstream", "std::ofstream out;"),
    ("fstream", "std::ifstream in;"),
    ("getline", "std::getline(in, line);"),
    ("async", "auto f = std::async(work);"),
    ("format", "auto s = std::format(\"{}\", 1);"),
    ("format", "auto s = std::vformat(f, args);"),
    ("stable-sort", "std::stable_sort(a, b);"),
    ("stable-sort", "eastl::stable_sort(a, b);"),
    ("stable-sort", "std::inplace_merge(a, m, b);"),
    ("stable-sort", "std::stable_partition(a, b, p);"),
    ("directory-iterator", "for (auto& e : std::filesystem::directory_iterator(p)) {}"),
    ("fmt", "auto s = fmt::format(\"{}\", 1);"),
    ("include-stream", "#include <fstream>"),
    ("include-stream", "#include <sstream>"),
    ("include-stream", "#  include <iostream>"),
]


@pytest.mark.parametrize("rule, code", POSITIVES)
def test_every_rule_finds_its_name(tmp_path, rule, code):
    assert found(tmp_path, code + "\n") == [rule]


def test_every_rule_id_has_a_positive_case():
    assert {rule for rule, _ in POSITIVES} == types.RULE_IDS


@pytest.mark.parametrize(
    "code",
    [
        "std::visit(f, v);",
        "std::monostate m;",
        "constexpr auto n = std::tuple_size_v<T>;",
        "auto o = std::nullopt;",
        "auto x = std::get<0>(v);",
        "std::string_view_like x;",
        "Optional<int> o;",
        "String s; StringView v;",
        "auto p = std::move(x); std::forward<T>(x);",
        "std::atomic<int> a;",
        "std::format_string x;",
        "mystd::string s;",
        "BECore::fmtx::name();",
        "int std_string = 0; int stringx = 1;",
        "#include <vector>",
    ],
)
def test_the_companions_that_stay_and_the_alias_names_are_no_hit(tmp_path, code):
    assert found(tmp_path, code + "\n") == []


def test_a_leading_scope_operator_is_still_a_hit(tmp_path):
    assert found(tmp_path, "::std::string s;\n") == ["type"]


def test_a_hit_in_a_comment_a_string_and_a_raw_string_is_not_found(tmp_path):
    code = (
        "// std::string in a line comment\n"
        "/* std::vector<int>\n   std::function<void()> */\n"
        'const char* a = "std::string";\n'
        'const char* b = R"x(\nstd::map<int, int>\n)x";\n'
        'const char* c = "#include <fstream>";\n'
    )
    assert found(tmp_path, code) == []


def test_the_line_of_a_hit_is_counted_through_comments_and_raw_strings(tmp_path):
    write(tmp_path, "src/Foo.cpp", 'R"x(\n\n)x";\n/* a\n b */\nstd::string s;\n')
    assert [hit.line for hit in types.check(tmp_path).new] == [6]


def test_the_message_names_the_file_the_line_the_rule_and_the_name(tmp_path):
    write(tmp_path, "src/Foo.cpp", "int x;\nstd::string s;\n")
    [finding] = types.check(tmp_path).findings
    assert finding.startswith("src/Foo.cpp:2: [type] std::string: use the `String`")


# ---- the marker ---------------------------------------------------------------------------------------------------------------


def test_a_marker_after_code_keeps_its_line(tmp_path):
    assert found(tmp_path, "std::string a; //-NoStd: SDL boundary\nstd::string b;\n") == ["type"]


def test_a_marker_on_its_own_line_keeps_the_next_line_and_not_the_second_one(tmp_path):
    code = "//-NoStd: the boundary\nstd::string a;\nstd::string b;\n"
    write(tmp_path, "src/Foo.cpp", code)
    assert [hit.line for hit in types.check(tmp_path).new] == [3]


@pytest.mark.parametrize("marker", ["// NoStd: x", "//NoStd: x", "//-nostd: x", "// std: x", "//-NoStd x"])
def test_other_spellings_are_no_marker(tmp_path, marker):
    assert found(tmp_path, f"std::string a; {marker}\n") == ["type"]


def test_the_exempt_lines_are_the_ones_of_the_codemod(tmp_path):
    sys.path.insert(0, str(ENGINE / "tools" / "codemod"))
    import codemod

    source = "a; //-NoStd: x\n//-NoStd: y\nb;\nc;\n  //-NoStd: z\nd;\n// NoStd: no\ne;\n"
    assert nostd.exempt_lines(source) == {1, 2, 3, 5, 6}
    assert codemod.Pass(source, set())._exempt_lines == nostd.exempt_lines(source)


# ---- the skips ----------------------------------------------------------------------------------------------------------------

SAMPLE = "std::string s;\n"


def test_the_type_layer_and_the_format_layer_are_skipped_but_their_tests_are_not(tmp_path):
    write(tmp_path, "src/Modules/BECore/Types/Strings.h", SAMPLE)
    write(tmp_path, "src/Modules/BECore/Format/Format.h", "auto s = fmt::format(\"\");\n")
    write(tmp_path, "tests/BECore/FormatTest.cpp", "auto s = fmt::format(\"\");\n")
    result = types.check(tmp_path)
    assert [hit.path for hit in result.new] == ["tests/BECore/FormatTest.cpp"]


@pytest.mark.parametrize(
    "relative",
    [
        "src/Modules/EnTT/x.h",
        "src/Modules/fmt/x.h",
        "external/lib/x.cpp",
        "build/x.cpp",
        "build-debug/x.cpp",
        ".venv/x.cpp",
        "src/Widgets/Icons/x.h",
        "src/Modules/BECore/pch.h",
        "src/Modules/BECore/Reflection/DataAccessor.h",
        "tests/BECore/TypesTest.cpp",
    ],
)
def test_vendored_build_and_the_codemods_skipped_files_are_skipped(tmp_path, relative):
    write(tmp_path, relative, SAMPLE)
    assert types.check(tmp_path).new == []


def test_a_folder_with_a_git_entry_is_a_submodule_and_skipped(tmp_path):
    write(tmp_path, "engine/.git", "gitdir: ../.git/modules/engine\n")
    write(tmp_path, "engine/src/X.cpp", SAMPLE)
    write(tmp_path, "src/Y.cpp", SAMPLE)
    assert [hit.path for hit in types.check(tmp_path).new] == ["src/Y.cpp"]


def test_exclude_skips_a_folder_and_a_file(tmp_path):
    write(tmp_path, "src/Own/A.cpp", SAMPLE)
    write(tmp_path, "src/B.cpp", SAMPLE)
    write(tmp_path, "src/C.cpp", SAMPLE)
    result = types.check(tmp_path, excludes=["src/Own", "src/B.cpp"])
    assert [hit.path for hit in result.new] == ["src/C.cpp"]


def test_the_other_c_suffixes_are_scanned(tmp_path):
    for suffix in (".hh", ".hxx", ".cc", ".cxx", ".mm", ".hpp", ".inl"):
        write(tmp_path, f"src/F{suffix}", SAMPLE)
    write(tmp_path, "src/F.txt", SAMPLE)
    assert types.check(tmp_path).checked == 7


# ---- the folder allow-list ----------------------------------------------------------------------------------------------------


def test_tests_allow_regex_stringstream_and_the_stream_includes(tmp_path):
    code = "#include <sstream>\n#include <fstream>\nstd::regex r;\nstd::ostringstream out;\n"
    assert found(tmp_path, code, "tests/BECore/X.cpp") == []
    assert found(tmp_path, code, "src/X.cpp") == ["include-stream", "include-stream", "regex", "stringstream"]


def test_tests_do_not_allow_function_or_ofstream(tmp_path):
    code = "std::function<void()> f;\nstd::ofstream out;\n"
    assert found(tmp_path, code, "tests/BECore/X.cpp") == ["function", "fstream"]


def test_a_tests_folder_deeper_in_the_tree_counts_as_tests(tmp_path):
    assert found(tmp_path, "std::regex r;\n", "tools/stress/tests/X.cpp") == []


def test_a_folder_matches_whole_segments_only(tmp_path):
    assert found(tmp_path, "std::regex r;\n", "src/mytests/X.cpp") == ["regex"]
    assert found(tmp_path, "std::regex r;\n", "src/tests.cpp") == ["regex"]


def test_allow_adds_a_folder_to_a_rule(tmp_path):
    allow = types.parse_allow(["tools/dev:fstream,getline"])
    code = "std::ofstream out;\nstd::getline(in, l);\nstd::function<void()> f;\n"
    assert found(tmp_path, code, "tools/dev/X.cpp", allow=allow) == ["function"]
    assert found(tmp_path, code, "tools/other/X.cpp", allow=allow) == ["fstream", "getline", "function"]


@pytest.mark.parametrize("value", ["tools", "tools:", ":regex", "tools:nosuchrule", "tools:regex,nosuchrule"])
def test_a_bad_allow_is_an_error(value):
    with pytest.raises(ValueError):
        types.parse_allow([value])


# ---- the baseline -------------------------------------------------------------------------------------------------------------


def baseline_file(tmp_path: Path, text: str) -> Path:
    return write(tmp_path, "types_baseline.txt", text)


def test_an_equal_count_passes(tmp_path):
    write(tmp_path, "src/Foo.cpp", "std::function<void()> a;\nstd::function<void()> b;\n")
    baseline = types.read_baseline(baseline_file(tmp_path, "src/Foo.cpp function 2\n"))
    result = types.check(tmp_path, baseline=baseline)
    assert result.findings == [] and len(result.hits) == 2


def test_a_higher_count_prints_every_hit_of_the_file_and_rule(tmp_path):
    write(tmp_path, "src/Foo.cpp", "std::function<void()> a;\nstd::function<void()> b;\nstd::string s;\n")
    result = types.check(tmp_path, baseline={("src/Foo.cpp", "function"): 1})
    assert [(hit.line, hit.rule) for hit in result.new] == [(1, "function"), (2, "function"), (3, "type")]


def test_a_lower_count_and_a_path_with_no_hits_are_fixed(tmp_path):
    write(tmp_path, "src/Foo.cpp", "std::function<void()> a;\n")
    baseline = {("src/Foo.cpp", "function"): 3, ("src/Gone.cpp", "fstream"): 1}
    result = types.check(tmp_path, baseline=baseline)
    assert result.new == []
    assert [line.split("[")[0] for line in result.fixed] == ["fixed: src/Foo.cpp ", "fixed: src/Gone.cpp "]
    assert len(result.findings) == 2


def test_a_missing_baseline_file_is_an_empty_one(tmp_path):
    assert types.read_baseline(tmp_path / "nothing.txt") == {}
    assert types.read_baseline(None) == {}


def test_the_baseline_file_takes_comments_and_rejects_a_bad_line(tmp_path):
    good = baseline_file(tmp_path, "# header\n\nsrc/A.cpp function 2  # why\n")
    assert types.read_baseline(good) == {("src/A.cpp", "function"): 2}
    for bad in ("src/A.cpp function\n", "src/A.cpp nosuchrule 1\n", "src/A.cpp function x\n"):
        with pytest.raises(ValueError):
            types.read_baseline(baseline_file(tmp_path, bad))


def test_update_baseline_writes_a_sorted_file_with_the_header_and_exits_0(tmp_path, capsys):
    write(tmp_path, "src/B.cpp", "std::ofstream o;\nstd::function<void()> f;\n")
    write(tmp_path, "src/A.cpp", "std::function<void()> f;\n")
    target = tmp_path / "base.txt"
    assert types.main(["--root", str(tmp_path), "--baseline", str(target), "--update-baseline"]) == 0
    lines = target.read_text().splitlines()
    assert lines[0].startswith("#")
    assert [line for line in lines if not line.startswith("#")] == [
        "src/A.cpp function 1",
        "src/B.cpp fstream 1",
        "src/B.cpp function 1",
    ]
    assert types.main(["--root", str(tmp_path), "--baseline", str(target)]) == 0


def test_update_baseline_keeps_the_head_comment_of_an_existing_file(tmp_path):
    write(tmp_path, "src/A.cpp", "std::function<void()> f;\n")
    target = baseline_file(tmp_path, "# my header\n# line two\nsrc/Old.cpp fstream 4\n")
    assert types.main(["--root", str(tmp_path), "--baseline", str(target), "--update-baseline"]) == 0
    assert target.read_text() == "# my header\n# line two\nsrc/A.cpp function 1\n"


def test_update_baseline_needs_a_baseline(tmp_path):
    assert types.main(["--root", str(tmp_path), "--update-baseline"]) == 2


# ---- the command line ---------------------------------------------------------------------------------------------------------


def test_the_exit_codes_and_the_summary(tmp_path, capsys):
    write(tmp_path, "src/Foo.cpp", "std::string a;\nstd::string b; //-NoStd: x\n")
    assert types.main(["--root", str(tmp_path)]) == 1
    out = capsys.readouterr().out.splitlines()
    assert out[0].startswith("src/Foo.cpp:1: [type] std::string:")
    assert out[-1] == "checked 1 files, hits 1, baselined 0, new 1, fixed 0"
    assert types.main(["--root", str(tmp_path), "--exclude", "src"]) == 0


def test_the_summary_counts_baselined_and_fixed(tmp_path, capsys):
    write(tmp_path, "src/Foo.cpp", "std::function<void()> f;\n")
    target = baseline_file(tmp_path, "src/Foo.cpp function 1\nsrc/Bar.cpp function 1\n")
    assert types.main(["--root", str(tmp_path), "--baseline", str(target)]) == 1
    assert capsys.readouterr().out.splitlines()[-1] == "checked 1 files, hits 1, baselined 1, new 0, fixed 1"


def test_a_bad_root_a_bad_allow_and_an_unknown_option_exit_2(tmp_path, capsys):
    assert types.main(["--root", str(tmp_path / "nope")]) == 2
    assert types.main(["--root", str(tmp_path), "--allow", "tests"]) == 2
    assert types.main(["--root", str(tmp_path), "--nosuch"]) == 2
    broken = baseline_file(tmp_path, "garbage\n")
    assert types.main(["--root", str(tmp_path), "--baseline", str(broken)]) == 2
    capsys.readouterr()
