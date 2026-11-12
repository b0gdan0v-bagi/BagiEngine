"""The warnings through the incremental cache and the command line of meta_generator.py."""

import json
import textwrap

import pytest

from core.cache import MetadataCache

CACHE_VERSION = MetadataCache.CACHE_VERSION

BROKEN = textwrap.dedent("""\
    class Foo final : public Bar {
        BE_CLASS(Foo)
        BE_REFLECT_FIELD float _m[16];
    };
""")

FIXED = textwrap.dedent("""\
    struct Foo {
        BE_CLASS(Foo)
        BE_REFLECT_FIELD float _m = 0.0f;
    };
""")


@pytest.fixture
def source(tmp_path):
    directory = tmp_path / "src"
    directory.mkdir()
    return directory


def warning_lines(err):
    return [line for line in err.splitlines() if "warning: reflection:" in line]


# ---------------------------------------------------------------------------
# replay from the cache
# ---------------------------------------------------------------------------

def test_second_run_with_an_unchanged_file_prints_the_same_warnings(run_cli, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    _, out_first, err_first = run_cli(source)
    _, out_second, err_second = run_cli(source)
    assert len(warning_lines(err_first)) == 2
    assert warning_lines(err_second) == warning_lines(err_first)
    assert "Foo.h:2: warning: reflection: BE_CLASS(Foo)" in err_second
    # the second run really came from the cache: nothing was parsed
    assert "0 files processed" in out_second
    assert "2 reflection warnings" in out_first and "2 reflection warnings" in out_second


def test_warning_lines_are_sorted_by_path_and_line(run_cli, source):
    (source / "B.h").write_text("struct B {\n BE_CLASS(B)\n BE_REFLECT_FIELD int a[2];\n BE_REFLECT_FIELD int b[2];\n};\n", encoding="utf-8")
    (source / "A.h").write_text("BE_REFLECT_FIELD int lonely;\n", encoding="utf-8")
    _, _, err = run_cli(source)
    lines = warning_lines(err)
    assert [line.split(": warning")[0].rsplit("/", 1)[1] for line in lines] == ["A.h:1", "B.h:3", "B.h:4"]


def test_a_file_fixed_by_an_edit_stops_printing(run_cli, source):
    header = source / "Foo.h"
    header.write_text(BROKEN, encoding="utf-8")
    _, _, err = run_cli(source)
    assert warning_lines(err)
    header.write_text(FIXED, encoding="utf-8")
    _, _, err = run_cli(source)
    assert warning_lines(err) == []
    _, _, err = run_cli(source)
    assert warning_lines(err) == []


def test_a_file_broken_by_an_edit_starts_printing(run_cli, source):
    header = source / "Foo.h"
    header.write_text(FIXED, encoding="utf-8")
    _, _, err = run_cli(source)
    assert warning_lines(err) == []
    header.write_text(BROKEN, encoding="utf-8")
    _, _, err = run_cli(source)
    assert len(warning_lines(err)) == 2


def test_a_deleted_file_stops_printing(run_cli, source):
    header = source / "Foo.h"
    header.write_text(BROKEN, encoding="utf-8")
    run_cli(source)
    header.unlink()
    _, _, err = run_cli(source)
    assert warning_lines(err) == []


def test_force_prints_the_same_warnings(run_cli, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    _, _, first = run_cli(source)
    _, _, forced = run_cli(source, "--force")
    assert warning_lines(forced) == warning_lines(first)


# ---------------------------------------------------------------------------
# which files report
# ---------------------------------------------------------------------------

def test_reference_dirs_print_no_warnings(run_cli, tmp_path, source):
    reference = tmp_path / "reference"
    reference.mkdir()
    (reference / "Engine.h").write_text(BROKEN, encoding="utf-8")
    (source / "Game.h").write_text(FIXED, encoding="utf-8")
    code, out, err = run_cli(source, "--reference-dir", str(reference))
    assert code == 0
    assert warning_lines(err) == []
    assert "0 reflection warnings" in out


def test_scan_dirs_do_report(run_cli, tmp_path, source):
    scan = tmp_path / "scan"
    scan.mkdir()
    (scan / "Derived.h").write_text(BROKEN, encoding="utf-8")
    (source / "Game.h").write_text(FIXED, encoding="utf-8")
    _, _, err = run_cli(source, "--scan-dir", str(scan))
    assert len(warning_lines(err)) == 2
    assert all("Derived.h" in line for line in warning_lines(err))


# ---------------------------------------------------------------------------
# --quiet and --warnings-as-errors
# ---------------------------------------------------------------------------

def test_quiet_prints_nothing(run_cli, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    code, out, err = run_cli(source, "--quiet")
    assert (code, out, err) == (0, "", "")


def test_quiet_with_warnings_as_errors_exits_with_one_and_says_nothing(run_cli, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    code, out, err = run_cli(source, "--quiet", "--warnings-as-errors")
    assert (code, out, err) == (1, "", "")


def test_warnings_as_errors_is_decided_from_the_cache_too(run_cli, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    assert run_cli(source)[0] == 0
    assert run_cli(source, "--warnings-as-errors")[0] == 1


def test_warnings_as_errors_exits_with_zero_once_the_file_is_fixed(run_cli, source):
    header = source / "Foo.h"
    header.write_text(BROKEN, encoding="utf-8")
    assert run_cli(source, "--warnings-as-errors")[0] == 1
    header.write_text(FIXED, encoding="utf-8")
    assert run_cli(source, "--warnings-as-errors")[0] == 0


def test_warnings_do_not_stop_the_generation(run_cli, tmp_path, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    (source / "Ok.h").write_text("struct Ok {\n BE_CLASS(Ok)\n BE_REFLECT_FIELD int a = 0;\n};\n", encoding="utf-8")
    run_cli(source)
    assert (tmp_path / "out" / "Ok.gen.hpp").exists()


# ---------------------------------------------------------------------------
# the cache file
# ---------------------------------------------------------------------------

def test_cache_version_is_2_4_or_later():
    assert tuple(int(p) for p in CACHE_VERSION.split(".")) >= (2, 4)


def test_old_cache_version_is_discarded_without_an_error_and_the_warnings_come_back(run_cli, tmp_path, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    run_cli(source)
    cache_path = tmp_path / "cache" / "metadata_cache.json"
    data = json.loads(cache_path.read_text(encoding="utf-8"))
    data["version"] = "2.1"
    for entry in data["files"].values():
        entry.pop("warnings", None)
    cache_path.write_text(json.dumps(data), encoding="utf-8")

    code, out, err = run_cli(source)
    assert code == 0
    assert "Error" not in out and "Error" not in err
    assert len(warning_lines(err)) == 2
    assert json.loads(cache_path.read_text(encoding="utf-8"))["version"] == CACHE_VERSION


def test_cache_of_the_current_version_without_a_warnings_key_loads(tmp_path):
    path = tmp_path / "metadata_cache.json"
    path.write_text(json.dumps({
        "version": CACHE_VERSION,
        "files": {"a.h": {"path": "a.h", "content_hash": "x", "last_scanned": "now"}},
    }), encoding="utf-8")
    cache = MetadataCache(path)
    assert cache.load()
    assert cache.files["a.h"].warnings == []


def test_corrupt_cache_file_is_discarded(run_cli, tmp_path, source):
    (source / "Foo.h").write_text(BROKEN, encoding="utf-8")
    cache_dir = tmp_path / "cache"
    cache_dir.mkdir()
    (cache_dir / "metadata_cache.json").write_text("{not json", encoding="utf-8")
    code, _, err = run_cli(source)
    assert code == 0
    assert len(warning_lines(err)) == 2


# ---------------------------------------------------------------------------
# generated output of a method with an inline body
# ---------------------------------------------------------------------------

def test_inline_body_method_and_the_next_one_are_both_in_the_generated_table(run_cli, tmp_path, source):
    (source / "IFoo.h").write_text(textwrap.dedent("""\
        struct IFoo {
            BE_CLASS(IFoo)
            BE_FUNCTION virtual void Update() = 0;
            BE_FUNCTION virtual void DrawScreen() {}
            BE_FUNCTION virtual void Draw() = 0;
        };
    """), encoding="utf-8")
    code, _, err = run_cli(source, "--warnings-as-errors")
    assert (code, warning_lines(err)) == (0, [])
    generated = (tmp_path / "out" / "IFoo.gen.hpp").read_text(encoding="utf-8")
    for name in ("Update", "DrawScreen", "Draw"):
        assert f'eastl::string_view{{"{name}"}}, &IFoo::{name}' in generated
    assert "BE_FUNCTION" not in generated


# ---------------------------------------------------------------------------
# generated ClassMeta of a class with ICON=
# ---------------------------------------------------------------------------

def test_icon_reaches_the_generated_class_meta(run_cli, tmp_path, source):
    (source / "Pic.h").write_text(textwrap.dedent("""\
        struct Pic {
            BE_CLASS(Pic, ICON=image)
        };
        struct Plain {
            BE_CLASS(Plain)
        };
    """), encoding="utf-8")
    code, _, err = run_cli(source, "--warnings-as-errors")
    assert (code, warning_lines(err)) == (0, [])
    generated = (tmp_path / "out" / "Pic.gen.hpp").read_text(encoding="utf-8")
    assert 'eastl::string_view{"Pic"},\n        eastl::string_view{"image"}' in generated
    assert 'eastl::string_view{"Plain"},\n        eastl::string_view{""}' in generated


# ---------------------------------------------------------------------------
# a FACTORY_BASE whose enum name is already taken
# ---------------------------------------------------------------------------

def test_factory_base_whose_generated_enum_name_is_declared_by_hand_warns(run_cli, source):
    (source / "IThing.h").write_text(textwrap.dedent("""\
        struct IThing {
            BE_CLASS(IThing, FACTORY_BASE)
        };
    """), encoding="utf-8")
    (source / "ThingType.h").write_text("CORE_ENUM(ThingType, uint8_t, A, B)\n", encoding="utf-8")
    _, _, err = run_cli(source)
    lines = warning_lines(err)
    assert len(lines) == 1
    assert "IThing.h:1" in lines[0] and "'ThingType'" in lines[0]


def test_factory_base_without_a_clash_does_not_warn(run_cli, source):
    (source / "IThing.h").write_text("struct IThing {\n BE_CLASS(IThing, FACTORY_BASE)\n};\n", encoding="utf-8")
    (source / "Other.h").write_text("CORE_ENUM(OtherType, uint8_t, A, B)\n", encoding="utf-8")
    _, _, err = run_cli(source)
    assert warning_lines(err) == []
