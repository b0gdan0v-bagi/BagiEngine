"""A header whose Generated/<Stem>.gen.hpp no .cpp of the target includes is reported: the macro declares
Serialize / Deserialize and only that file defines them."""

import json

import pytest

HEADER = "struct Foo {\n    BE_CLASS(Foo)\n    BE_REFLECT_FIELD int _a = 0;\n};\n"
TWO_CLASSES = "struct Foo {\n    BE_CLASS(Foo)\n};\n\nstruct Bar {\n    BE_CLASS(Bar)\n};\n"


@pytest.fixture
def source(tmp_path):
    directory = tmp_path / "src"
    directory.mkdir()
    return directory


def gen_warnings(err):
    return [line for line in err.splitlines() if "gen.hpp is included by no .cpp" in line]


def test_a_header_without_an_include_warns_on_the_line_of_its_marker(run_cli, source):
    (source / "Foo.h").write_text("// comment\n\nstruct Foo\n{\n    BE_CLASS(Foo)\n};\n", encoding="utf-8")
    code, out, err = run_cli(source)
    lines = gen_warnings(err)
    assert code == 0
    assert len(lines) == 1
    assert lines[0].endswith(
        "Foo.h:5: warning: reflection: Foo.gen.hpp is included by no .cpp of this target; "
        "Serialize / Deserialize of Foo are never defined (link error)")
    assert "1 reflection warnings" in out


def test_the_warning_names_every_class_of_the_header(run_cli, source):
    (source / "Foo.h").write_text(TWO_CLASSES, encoding="utf-8")
    _, _, err = run_cli(source)
    assert "Serialize / Deserialize of Foo, Bar are never defined" in gen_warnings(err)[0]
    assert "Foo.h:2:" in gen_warnings(err)[0]


def test_an_event_header_warns_too(run_cli, source):
    (source / "Ping.h").write_text("struct Ping {\n    BE_EVENT(Ping)\n};\n", encoding="utf-8")
    _, _, err = run_cli(source)
    assert "Ping.h:2:" in gen_warnings(err)[0]


def test_the_include_in_the_cpp_silences_it(run_cli, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    include_gen(source, "Foo")
    code, out, err = run_cli(source, "--warnings-as-errors")
    assert (code, gen_warnings(err)) == (0, [])
    assert "0 reflection warnings" in out


def test_an_include_in_quotes_counts(run_cli, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    include_gen(source, "Foo", quoted=True)
    assert gen_warnings(run_cli(source)[2]) == []


def test_an_include_behind_an_if_counts(run_cli, source):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    (source / "Foo.cpp").write_text('#include "Foo.h"\n#if defined(X)\n  #  include <Generated/Foo.gen.hpp>\n#endif\n', encoding="utf-8")
    assert gen_warnings(run_cli(source)[2]) == []


def test_an_include_in_a_comment_does_not_count(run_cli, source):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    (source / "Foo.cpp").write_text("// #include <Generated/Foo.gen.hpp>\nint x;\n", encoding="utf-8")
    assert len(gen_warnings(run_cli(source)[2])) == 1


def test_the_include_of_another_header_does_not_count(run_cli, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    include_gen(source, "Other")
    assert len(gen_warnings(run_cli(source)[2])) == 1


def test_a_cpp_in_another_folder_of_the_target_counts(run_cli, tmp_path, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    sub = source / "impl" / "deeper"
    sub.mkdir(parents=True)
    include_gen(sub, "Foo")
    assert gen_warnings(run_cli(source)[2]) == []


def test_a_cpp_in_a_scan_dir_counts(run_cli, tmp_path, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    scan = tmp_path / "scan"
    scan.mkdir()
    include_gen(scan, "Foo")
    assert gen_warnings(run_cli(source, "--scan-dir", str(scan), "--source-dir", str(source))[2]) == []


def test_a_cpp_in_an_include_dir_counts(run_cli, tmp_path, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    tests = tmp_path / "tests" / "Fuzz"
    tests.mkdir(parents=True)
    include_gen(tests, "Foo")
    assert gen_warnings(run_cli(source, "--include-dir", str(tmp_path / "tests"))[2]) == []


def test_a_cpp_outside_the_target_does_not_count(run_cli, tmp_path, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    elsewhere = tmp_path / "elsewhere"
    elsewhere.mkdir()
    include_gen(elsewhere, "Foo")
    assert len(gen_warnings(run_cli(source)[2])) == 1


def test_a_header_without_reflected_classes_has_nothing_to_include(run_cli, source):
    (source / "Plain.h").write_text("struct Plain { int _x; };\nCORE_ENUM(Mode, uint8_t, A, B)\n", encoding="utf-8")
    assert gen_warnings(run_cli(source)[2]) == []


def test_the_reference_dirs_are_not_reported(run_cli, tmp_path, source, include_gen):
    reference = tmp_path / "reference"
    reference.mkdir()
    (reference / "Engine.h").write_text(HEADER, encoding="utf-8")
    code, out, err = run_cli(source, "--reference-dir", str(reference))
    assert gen_warnings(err) == []


def test_warnings_as_errors_covers_it(run_cli, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    assert run_cli(source, "--warnings-as-errors")[0] == 1
    include_gen(source, "Foo")
    assert run_cli(source, "--warnings-as-errors")[0] == 0


def test_quiet_with_warnings_as_errors_exits_with_one(run_cli, source):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    assert run_cli(source, "--quiet", "--warnings-as-errors") == (1, "", "")


def test_the_incremental_run_prints_it_again_and_an_added_include_removes_it(run_cli, source, include_gen):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    _, out_first, err_first = run_cli(source)
    _, out_second, err_second = run_cli(source)
    assert "0 files processed" in out_second
    assert gen_warnings(err_second) == gen_warnings(err_first) != []

    include_gen(source, "Foo")  # the header is unchanged, the answer is not
    _, out_third, err_third = run_cli(source)
    assert "0 files processed" in out_third
    assert gen_warnings(err_third) == []
    assert "0 reflection warnings" in out_third

    (source / "Foo.cpp").unlink()
    assert len(gen_warnings(run_cli(source)[2])) == 1


def test_the_answer_is_not_stored_in_the_cache(run_cli, tmp_path, source):
    (source / "Foo.h").write_text(HEADER, encoding="utf-8")
    run_cli(source)
    data = json.loads((tmp_path / "cache" / "metadata_cache.json").read_text(encoding="utf-8"))
    assert all(not entry["warnings"] for entry in data["files"].values())
