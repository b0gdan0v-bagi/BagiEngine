"""check_includes.py on small trees made in a temp folder."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_includes as includes  # noqa: E402


def write(root: Path, relative: str, text: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def found(root: Path, excludes=()) -> list[str]:
    return includes.check(root, list(excludes))


def test_own_header_is_allowed(tmp_path):
    write(tmp_path, "src/Foo.cpp", '#include "Foo.h"\n#include <Bar/Baz.h>\n')
    assert found(tmp_path) == []


def test_own_header_of_an_objective_cpp_file_is_allowed(tmp_path):
    write(tmp_path, "src/Foo.mm", '#include "Foo.h"\n')
    assert found(tmp_path) == []


def test_a_test_file_including_the_header_it_tests_is_found(tmp_path):
    write(tmp_path, "tests/FooTest.cpp", '#include "Foo.h"\n')
    assert found(tmp_path) == ['tests/FooTest.cpp:1: #include "Foo.h": use <...> from an include root']


def test_own_header_with_a_folder_is_found(tmp_path):
    write(tmp_path, "src/Foo.cpp", '#include "Sub/Foo.h"\n')
    assert len(found(tmp_path)) == 1


def test_a_header_with_a_quoted_include_is_found(tmp_path):
    write(tmp_path, "src/Foo.h", '#pragma once\n#include "Foo.h"\n#include "Bar.h"\n')
    assert [p.split(":")[1] for p in found(tmp_path)] == ["2", "3"]


def test_angle_includes_are_never_found(tmp_path):
    write(tmp_path, "src/Foo.h", "#include <Foo/Bar.h>\n#  include <vector>\n")
    assert found(tmp_path) == []


def test_import_is_found(tmp_path):
    write(tmp_path, "src/Foo.mm", '#import "x.h"\n')
    assert found(tmp_path) == ['src/Foo.mm:1: #include "x.h": use <...> from an include root']


def test_spaces_after_the_hash_are_found(tmp_path):
    write(tmp_path, "src/Foo.h", '  #  include "x.h"\n')
    assert len(found(tmp_path)) == 1


def test_a_line_comment_is_not_found(tmp_path):
    write(tmp_path, "src/Foo.h", '// #include "x.h"\n    //#include "y.h"\n')
    assert found(tmp_path) == []


def test_a_block_comment_is_not_found(tmp_path):
    write(tmp_path, "src/Foo.h", '/*\n#include "x.h"\n*/\n#include "y.h"\n/* a */ #include "z.h"\n')
    assert [p.split(":")[1] for p in found(tmp_path)] == ["4", "5"]


def test_an_inline_block_comment_closes_on_its_line(tmp_path):
    write(tmp_path, "src/Foo.h", '/* a */\n#include "x.h"\n')
    assert len(found(tmp_path)) == 1


def test_a_comment_opener_in_a_string_does_not_open_a_comment(tmp_path):
    write(tmp_path, "src/Foo.cpp", 'const char* a = "/*";\n#include "x.h"\n')
    assert len(found(tmp_path)) == 1


def test_a_quote_character_literal_does_not_open_a_string(tmp_path):
    write(tmp_path, "src/Foo.cpp", "char a = '\"'; /*\n#include \"x.h\"\n*/\n#include \"y.h\"\n")
    assert [p.split(":")[1] for p in found(tmp_path)] == ["4"]


def test_files_of_other_suffixes_are_not_scanned(tmp_path):
    write(tmp_path, "src/notes.md", '#include "x.h"\n')
    write(tmp_path, "src/Foo.txt", '#include "x.h"\n')
    assert found(tmp_path) == []


def test_skipped_folders(tmp_path):
    for folder in ("build", "external", ".venv", "node_modules", "__pycache__", ".git", "src/external"):
        write(tmp_path, f"{folder}/A.h", '#include "x.h"\n')
    assert found(tmp_path) == []


def test_a_submodule_folder_is_skipped(tmp_path):
    write(tmp_path, "engine/.git", "gitdir: ../.git/modules/engine\n")
    write(tmp_path, "engine/A.h", '#include "x.h"\n')
    write(tmp_path, "src/B.h", '#include "x.h"\n')
    assert [p.split(":")[0] for p in found(tmp_path)] == ["src/B.h"]


def test_a_vendored_module_without_claude_md_is_skipped(tmp_path):
    write(tmp_path, "src/Modules/fmt/A.h", '#include "x.h"\n')
    write(tmp_path, "src/Modules/Own/CLAUDE.md", "own")
    write(tmp_path, "src/Modules/Own/B.h", '#include "x.h"\n')
    assert [p.split(":")[0] for p in found(tmp_path)] == ["src/Modules/Own/B.h"]


def test_a_vendored_first_line_skips_the_file(tmp_path):
    write(tmp_path, "src/A.h", '// Vendored: do not edit by hand.\n#include "x.h"\n')
    write(tmp_path, "src/B.h", '\n\n\n\n\n// Vendored\n#include "x.h"\n')
    assert [p.split(":")[0] for p in found(tmp_path)] == ["src/B.h"]


def test_exclude_skips_a_folder_and_a_file(tmp_path):
    write(tmp_path, "src/Vendor/A.h", '#include "x.h"\n')
    write(tmp_path, "src/B.h", '#include "x.h"\n')
    write(tmp_path, "src/C.h", '#include "x.h"\n')
    assert [p.split(":")[0] for p in found(tmp_path, ["src/Vendor", "src/B.h"])] == ["src/C.h"]


def test_exit_code_and_message_format(tmp_path, capsys):
    write(tmp_path, "src/A.h", '#include "x.h"\n')
    write(tmp_path, "src/B.cpp", '#include "B.h"\n#include "y/z.h"\n')
    assert includes.main(["--root", str(tmp_path)]) == 1
    lines = capsys.readouterr().out.splitlines()
    assert lines[0] == 'src/A.h:1: #include "x.h": use <...> from an include root'
    assert lines[1] == 'src/B.cpp:2: #include "y/z.h": use <...> from an include root'
    assert lines[-1].startswith("2 ")


def test_a_clean_tree_exits_zero_and_prints_nothing(tmp_path, capsys):
    write(tmp_path, "src/A.cpp", '#include "A.h"\n#include <B/C.h>\n')
    assert includes.main(["--root", str(tmp_path)]) == 0
    assert capsys.readouterr().out == ""


def test_exclude_argument_is_repeatable(tmp_path):
    write(tmp_path, "a/A.h", '#include "x.h"\n')
    write(tmp_path, "b/B.h", '#include "x.h"\n')
    assert includes.main(["--root", str(tmp_path), "--exclude", "a", "--exclude", "b"]) == 0


def test_bad_arguments_exit_two(tmp_path):
    assert includes.main(["--root", str(tmp_path / "missing")]) == 2
    assert includes.main(["--nonsense"]) == 2
