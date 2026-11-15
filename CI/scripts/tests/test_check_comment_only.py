"""check_comment_only.py: the comment scanner on its own, and the check on small git repositories made in a temp folder."""

import shutil
import subprocess
import sys
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[3]

sys.path.insert(0, str(ENGINE / "CI" / "scripts"))

import check_comment_only as comment_only  # noqa: E402
import pytest  # noqa: E402

pytestmark = pytest.mark.skipif(shutil.which("git") is None, reason="git is not installed")


def same(old: str, new: str) -> bool:
    return comment_only.first_difference(old, new) is None


# ---- the scanner ------------------------------------------------------------------------------------------------------------


def test_a_cut_comment_is_the_same_code():
    assert same("int a; // why\nint b;\n", "int a;\nint b;\n")
    assert same("/** Doxygen\n * lines\n */\nint a;\n", "int a;\n")
    assert same("int a; /* x */ int b;", "int a; int b;")


def test_a_block_comment_between_tokens_separates_them():
    assert same("int/**/a;", "int a;")
    assert not same("int/**/a;", "inta;")


def test_a_line_continuation_extends_a_line_comment():
    assert same("// one \\\ncontinued code-looking text\nint a;\n", "int a;\n")


def test_reflowed_whitespace_is_the_same():
    assert same("void F(int a,\n       int b);\n", "void F(int a, int b);\n")
    assert same("int   a;\n\n\n\nint b;\n", "int a; int b;")


@pytest.mark.parametrize(
    "code",
    [
        'const char* a = "// not a comment";',
        'const char* a = "/* not a comment */";',
        'const char* a = "a \\" // still a string";',
        "char a = '\"'; /* a quote character */ const char* b = \"// not\"; char c = '/';",
        'const char* a = R"x(// raw )" /* still raw */)x";',
        'const char* a = u8R"(// raw)";',
        "int a = 1'000; int b = 2'000;",
    ],
)
def test_a_comment_sign_inside_a_literal_is_code(code):
    # what a literal holds is compared: the same code with a comment behind it passes, with one letter changed it fails
    assert same(code + " // a comment\n", code + "\n")
    changed = code.replace("not", "NOT").replace("still", "STILL").replace("raw)", "RAW)").replace("2'000", "3'000")
    assert changed != code
    assert not same(code, changed)


@pytest.mark.parametrize(
    "old, new",
    [
        ('const char* a = "x  y";', 'const char* a = "x y";'),
        ('const char* a = "x\ty";', 'const char* a = "x y";'),
        ('const char* a = R"(x\n  y)";', 'const char* a = R"(x\ny)";'),
        ('const char* a = R"(x\ny)";', 'const char* a = R"(x y)";'),
        ("char a = ' ';", "char a = '\t';"),
    ],
)
def test_whitespace_inside_a_literal_is_compared(old, new):
    assert not same(old, new)


def test_a_literal_with_spaces_is_one_token_and_reads_back_in_a_finding():
    assert same('f("a  b");\n', 'f("a  b"); // c\n')
    found = comment_only.first_difference('f("a  b");\n', 'f("a b");\n')
    assert found is not None
    assert '`f("a  b");`' in found[1] and '`f("a b");`' in found[1]


def test_a_comment_after_a_quote_character_is_cut():
    assert same("char a = '\"'; // a \" quote in a comment\nint b;", "char a = '\"';\nint b;")


def test_a_changed_token_is_a_difference():
    found = comment_only.first_difference("int a = 1;\nint b = 2;\n", "int a = 1;\nint b = 3;\n")
    assert found is not None
    assert found[0] == 2
    assert "`2;`" in found[1] and "`3;`" in found[1]


def test_a_renamed_identifier_is_a_difference():
    assert not same("int count;\n", "int total;\n")


def test_a_removed_line_of_code_is_a_difference():
    found = comment_only.first_difference("int a;\nint b;\nint c;\n", "int a;\nint c;\n")
    assert found is not None
    assert found[0] == 2


def test_removed_code_at_the_end_is_reported_at_the_last_line():
    found = comment_only.first_difference("int a;\nint b;\n", "int a;\n")
    assert found is not None
    assert found[0] == 1
    assert "removed" in found[1]


def test_added_code_is_reported_at_its_line():
    found = comment_only.first_difference("int a;\n", "int a;\n// a comment\nint b;\n")
    assert found is not None
    assert found[0] == 3
    assert "added" in found[1]


@pytest.mark.parametrize("code_point", [0xF0000, 0xF0020, 0xF3000])
def test_a_character_of_the_shifted_block_is_a_difference_on_either_side(code_point):
    holds = f"int a;\nchar c[] = \"{chr(code_point)}\";\n"
    clean = "int a;\nchar c[] = \"\";\n"
    for old, new, side in ((clean, holds, "now"), (holds, clean, "before"), (holds, holds, "now")):
        found = comment_only.first_difference(old, new)
        assert found is not None
        assert found[0] == 2
        assert f"U+{code_point:X}" in found[1] and side in found[1]


def test_a_character_next_to_the_shifted_block_is_no_difference():
    assert same(f"int a; // {chr(0xEFFFF)}{chr(0xF3001)}\n", "int a;\n")


@pytest.mark.parametrize(
    "old, new",
    [
        ("std::string s; //-NoStd: the SDL API\n", "std::string s;\n"),
        ("std::string s; //-NoStd: the SDL API\n", "std::string s; //-NoStd: the OS API\n"),
        ("int a; // NOLINT(readability-magic-numbers)\n", "int a;\n"),
        ("// NOLINTNEXTLINE\nint a;\n", "int a;\n"),
        ("// clang-format off\nint  a;\n// clang-format on\n", "int a;\n"),
        ("int a; /* NOLINT */\n", "int a;\n"),
    ],
)
def test_a_marker_comment_is_code(old, new):
    assert not same(old, new)


def test_an_unchanged_marker_passes_while_the_comments_around_it_are_cut():
    assert same("// the reason\nstd::string s; //-NoStd: the SDL API\n// more\n", "std::string s; //-NoStd: the SDL API\n")


def test_a_marker_is_found_only_in_a_comment():
    assert same('const char* a = "NOLINT"; // plain\n', 'const char* a = "NOLINT";\n')


def test_two_lines_of_a_directive_are_not_joined():
    assert same("#define X 1 // the value\n", "#define X 1\n")
    assert not same("#define X\n1\n", "#define X 1\n")


def test_a_directive_continued_by_a_backslash_is_one_line():
    assert same("#define X(a) \\\n    ((a) + 1)\n", "#define X(a) \\\n ((a) + 1)\n")


def test_a_block_comment_inside_a_directive_does_not_end_it():
    assert same("#define X /* a\n b */ 1\n", "#define X 1\n")


# ---- the check on git repositories ------------------------------------------------------------------------------------------


def run_git(root: Path, *args: str) -> None:
    subprocess.run(
        ["git", "-C", str(root), "-c", "user.name=t", "-c", "user.email=t@example.com", "-c", "commit.gpgsign=false", *args],
        check=True,
        capture_output=True,
    )


def write(root: Path, relative: str, text: str) -> None:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    run_git(tmp_path, "init", "-q", "-b", "main")
    write(tmp_path, "src/Foo.cpp", "// the file\nint Foo() { return 1; } // one\n")
    write(tmp_path, "src/Foo.h", "// header\nint Foo();\n")
    write(tmp_path, "README.md", "text\n")
    run_git(tmp_path, "add", ".")
    run_git(tmp_path, "commit", "-q", "-m", "base")
    return tmp_path


def run_check(root: Path, *paths: str, capsys) -> tuple:
    code = comment_only.main(["--base", "HEAD", "--root", str(root), *paths])
    return code, capsys.readouterr().out


def test_no_change_passes(repo, capsys):
    code, out = run_check(repo, capsys=capsys)
    assert code == 0
    assert "checked 0 files" in out


def test_a_comment_only_change_passes_committed_or_not(repo, capsys):
    write(repo, "src/Foo.cpp", "int Foo() { return 1; }\n")
    write(repo, "src/Foo.h", "/** The header, more words. */\nint Foo();\n")
    code, out = run_check(repo, capsys=capsys)
    assert code == 0, out
    assert "checked 2 files" in out
    run_git(repo, "commit", "-q", "-am", "comments")
    assert comment_only.main(["--base", "HEAD~1", "--root", str(repo)]) == 0


def test_a_code_change_fails_with_the_file_and_the_line(repo, capsys):
    write(repo, "src/Foo.cpp", "// the file\n\nint Foo() { return 2; }\n")
    code, out = run_check(repo, capsys=capsys)
    assert code == 1
    assert out.splitlines()[0].startswith("src/Foo.cpp:3: code differs")


def test_a_non_c_file_is_not_looked_at(repo, capsys):
    write(repo, "README.md", "other text\n")
    code, out = run_check(repo, capsys=capsys)
    assert code == 0
    assert "checked 0 files" in out


def test_a_new_file_fails(repo, capsys):
    write(repo, "src/Bar.h", "int Bar();\n")
    code, out = run_check(repo, capsys=capsys)
    assert code == 1
    assert "src/Bar.h:1: file added" in out


def test_a_deleted_file_fails(repo, capsys):
    (repo / "src/Foo.h").unlink()
    code, out = run_check(repo, capsys=capsys)
    assert code == 1
    assert "src/Foo.h:1: file deleted" in out


def test_a_shifted_block_character_prints_the_file_and_the_line(repo, capsys):
    write(repo, "src/Foo.cpp", f"// the file\nint Foo() {{ return 1; }} // {chr(0xF0020)}\n")
    code, out = run_check(repo, capsys=capsys)
    assert code == 1
    assert out.splitlines()[0].startswith("src/Foo.cpp:2: the source now holds U+F0020")


def test_paths_narrow_the_files(repo, capsys):
    write(repo, "src/Foo.cpp", "int Foo() { return 2; }\n")
    write(repo, "src/Foo.h", "int Foo(); // more\n")
    code, out = run_check(repo, "src/Foo.h", capsys=capsys)
    assert code == 0, out
    assert "checked 1 files" in out


def test_a_root_below_the_repository_reads_its_own_paths(repo, capsys):
    write(repo, "src/Foo.cpp", "int Foo() { return 2; }\n")
    code, out = run_check(repo / "src", capsys=capsys)
    assert code == 1
    assert out.startswith("Foo.cpp:1: code differs")


def test_a_base_that_moved_on_is_not_a_difference(repo, capsys):
    run_git(repo, "switch", "-q", "-c", "work")
    write(repo, "src/Foo.h", "// the header, reworded\nint Foo();\n")
    run_git(repo, "commit", "-q", "-am", "comments")
    run_git(repo, "switch", "-q", "main")
    write(repo, "src/Foo.cpp", "int Foo() { return 2; }\n")
    run_git(repo, "commit", "-q", "-am", "code on the base")
    run_git(repo, "switch", "-q", "work")
    code = comment_only.main(["--base", "main", "--root", str(repo)])
    out = capsys.readouterr().out
    assert code == 0, out
    assert "checked 1 files" in out


def test_an_unknown_revision_is_an_error(repo, capsys):
    assert comment_only.main(["--base", "nothing-like-this", "--root", str(repo)]) == 2


def test_a_missing_base_is_a_usage_error(repo, capsys):
    assert comment_only.main(["--root", str(repo)]) == 2


def test_help_says_what_is_compared(capsys):
    assert comment_only.main(["--help"]) == 0
    assert "Not compared" in capsys.readouterr().out
