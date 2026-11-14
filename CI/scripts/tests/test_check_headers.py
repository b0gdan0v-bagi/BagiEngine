"""check_headers.py on small trees made in a temp folder; the compile step is replaced by a fake except in the real-clang cases."""

import json
import shutil
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import check_headers as headers  # noqa: E402


def write(root: Path, relative: str, text: str = "#pragma once\n") -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def entry(root: Path, source: str, command: str) -> dict:
    return {"directory": str(root / "build"), "command": command, "file": str(root / source)}


def make_build(root: Path, entries: list[dict]) -> Path:
    build = root / "build"
    build.mkdir(exist_ok=True)
    (build / "compile_commands.json").write_text(json.dumps(entries))
    return build


def default_build(root: Path) -> Path:
    return make_build(root, [entry(root, "src/A/a.cpp", "/usr/bin/clang++ -DA=1 -I/x/src -c /x/a.cpp")])


def failing_by_name(*names: str):
    """A fake compile: a header whose file name is in `names` fails."""

    def fake(compiler, flags, header, cwd):
        return f"{header.name}:1:1: error: broken" if header.name in names else None

    return fake


def run(root: Path, args: list[str], compile_fn, capsys) -> tuple[int, str]:
    code = headers.main(["--root", str(root), *args], compile_fn)
    return code, capsys.readouterr().out


def test_failing_headers_without_a_baseline_are_new(tmp_path, capsys):
    write(tmp_path, "src/A/Good.h")
    write(tmp_path, "src/A/Bad.h")
    build = default_build(tmp_path)
    code, out = run(tmp_path, ["--build", str(build)], failing_by_name("Bad.h"), capsys)
    assert code == 1
    assert "new: src/A/Bad.h: Bad.h:1:1: error: broken" in out
    assert out.splitlines()[-1] == "checked 2, failing 1, new 1, fixed 0"


def test_the_baseline_equal_to_the_failures_passes(tmp_path, capsys):
    write(tmp_path, "src/A/Bad.h")
    build = default_build(tmp_path)
    baseline = write(tmp_path, "baseline.txt", "# a comment\nsrc/A/Bad.h  # why\n\n")
    code, out = run(tmp_path, ["--build", str(build), "--baseline", str(baseline)], failing_by_name("Bad.h"), capsys)
    assert code == 0
    assert out.splitlines()[-1] == "checked 1, failing 1, new 0, fixed 0"


def test_a_listed_header_that_passes_is_fixed_and_fails_the_run(tmp_path, capsys):
    write(tmp_path, "src/A/Now.h")
    build = default_build(tmp_path)
    baseline = write(tmp_path, "baseline.txt", "src/A/Now.h\n")
    code, out = run(tmp_path, ["--build", str(build), "--baseline", str(baseline)], failing_by_name(), capsys)
    assert code == 1
    assert "fixed: src/A/Now.h" in out
    assert "remove it from the baseline" in out
    assert out.splitlines()[-1] == "checked 1, failing 0, new 0, fixed 1"


def test_a_new_failure_beside_a_baseline_is_new(tmp_path, capsys):
    write(tmp_path, "src/A/Old.h")
    write(tmp_path, "src/A/Fresh.h")
    build = default_build(tmp_path)
    baseline = write(tmp_path, "baseline.txt", "src/A/Old.h\n")
    code, out = run(tmp_path, ["--build", str(build), "--baseline", str(baseline)], failing_by_name("Old.h", "Fresh.h"), capsys)
    assert code == 1
    assert "new: src/A/Fresh.h" in out
    assert "new: src/A/Old.h" not in out


def test_without_compile_commands_the_check_is_skipped(tmp_path, capsys):
    write(tmp_path, "src/A/Bad.h")
    code, out = run(tmp_path, ["--build", str(tmp_path / "nowhere")], failing_by_name("Bad.h"), capsys)
    assert code == 77
    assert "compile_commands.json" in out


def test_an_msvc_compiler_is_skipped(tmp_path, capsys):
    write(tmp_path, "src/A/Bad.h")
    build = make_build(tmp_path, [entry(tmp_path, "src/A/a.cpp", "C:/VS/bin/cl.exe /nologo /c a.cpp")])
    code, _ = run(tmp_path, ["--build", str(build)], failing_by_name("Bad.h"), capsys)
    assert code == 77


def test_a_bad_argument_is_exit_2(tmp_path, capsys):
    assert headers.main(["--root", str(tmp_path)], failing_by_name()) == 2
    assert headers.main(["--root", str(tmp_path / "missing"), "--build", "x"], failing_by_name()) == 2
    build = default_build(tmp_path)
    assert headers.main(["--root", str(tmp_path), "--build", str(build), "--update-baseline"], failing_by_name()) == 2


def test_exclude_skips_a_folder_and_a_file(tmp_path, capsys):
    write(tmp_path, "src/A/Bad.h")
    write(tmp_path, "src/Frag/Bad.h")
    write(tmp_path, "src/A/List.h")
    build = default_build(tmp_path)
    code, out = run(
        tmp_path, ["--build", str(build), "--exclude", "src/Frag", "--exclude", "src/A/List.h"], failing_by_name("Bad.h", "List.h"), capsys
    )
    assert code == 1
    assert "new: src/A/Bad.h" in out
    assert "src/Frag" not in out
    assert "List.h" not in out
    assert out.splitlines()[-1] == "checked 1, failing 1, new 1, fixed 0"


def test_update_baseline_writes_the_sorted_failures(tmp_path, capsys):
    write(tmp_path, "src/A/Zed.h")
    write(tmp_path, "src/A/Alpha.h")
    write(tmp_path, "src/A/Good.h")
    build = default_build(tmp_path)
    baseline = tmp_path / "baseline.txt"
    code, _ = run(tmp_path, ["--build", str(build), "--baseline", str(baseline), "--update-baseline"], failing_by_name("Zed.h", "Alpha.h"), capsys)
    assert code == 0
    assert headers.read_baseline(baseline) == {"src/A/Alpha.h", "src/A/Zed.h"}
    lines = [line for line in baseline.read_text().splitlines() if not line.startswith("#")]
    assert lines == ["src/A/Alpha.h", "src/A/Zed.h"]
    code, _ = run(tmp_path, ["--build", str(build), "--baseline", str(baseline)], failing_by_name("Zed.h", "Alpha.h"), capsys)
    assert code == 0


def test_a_vendored_module_and_a_submodule_are_not_scanned(tmp_path, capsys):
    write(tmp_path, "src/Modules/Lib/Bad.h")
    write(tmp_path, "src/Modules/Own/CLAUDE.md", "x")
    write(tmp_path, "src/Modules/Own/Bad.h")
    write(tmp_path, "sub/.git", "gitdir: x")
    write(tmp_path, "sub/Bad.h")
    build = default_build(tmp_path)
    code, out = run(tmp_path, ["--build", str(build)], failing_by_name("Bad.h"), capsys)
    assert out.splitlines()[-1] == "checked 1, failing 1, new 1, fixed 0"
    assert "new: src/Modules/Own/Bad.h" in out


def test_keep_flags_drops_include_pch_and_output_flags():
    command = (
        "/usr/bin/clang++ -DX=1 -D Y -I/a -isystem /b -std=gnu++2b -arch arm64 -isysroot /sdk -fPIC -Wall -g "
        "-Xclang -include -Xclang /build/cmake_pch.hxx -include BECore/pch.h -include-pch /b/x.pch -Winvalid-pch "
        "-x c++-header -o a.o -c /src/a.cpp"
    )
    kept = headers.keep_flags(headers.shlex.split(command))
    assert kept == ["-DX=1", "-D", "Y", "-I/a", "-isystem", "/b", "-std=gnu++2b", "-arch", "arm64", "-isysroot", "/sdk", "-fPIC"]


def test_the_flags_come_from_the_nearest_translation_unit(tmp_path):
    entries = [
        entry(tmp_path, "src/Modules/A/a.cpp", "/usr/bin/clang++ -DA=1 -c a.cpp"),
        entry(tmp_path, "src/Modules/B/b.cpp", "/usr/bin/clang++ -DB=1 -c b.cpp"),
        entry(tmp_path, "tests/B/t.cpp", "/usr/bin/clang++ -DT=1 -c t.cpp"),
        entry(tmp_path, "src/Modules/B/cmake_pch.hxx.cxx", "/usr/bin/clang++ -DPCH=1 -x c++-header -c p.cxx"),
    ]
    flags = headers.Flags(entries)
    for relative in ("src/Modules/A/Sub/h.h", "src/Modules/B/deep/er/h.h", "tests/B/h.h", "src/Other/h.h"):
        write(tmp_path, relative)
    assert flags.nearest(tmp_path / "src/Modules/A/Sub/h.h")[1] == ["-DA=1"]
    assert flags.nearest(tmp_path / "src/Modules/B/deep/er/h.h")[1] == ["-DB=1"]
    assert flags.nearest(tmp_path / "tests/B/h.h")[1] == ["-DT=1"]
    assert flags.nearest(tmp_path / "src/Other/h.h")[1] == ["-DA=1"]


def test_the_fake_receives_the_nearest_flags(tmp_path, capsys):
    write(tmp_path, "src/A/H.h")
    build = default_build(tmp_path)
    seen = []

    def fake(compiler, flags, header, cwd):
        seen.append((compiler, flags, header.name, cwd))

    run(tmp_path, ["--build", str(build)], fake, capsys)
    assert seen == [("/usr/bin/clang++", ["-DA=1", "-I/x/src"], "H.h", tmp_path / "build")]


def test_first_error_prefers_an_error_line():
    assert headers.first_error("note: x\nh.h:1:1: error: boom\nmore", 1) == "h.h:1:1: error: boom"
    assert headers.first_error("warning: w\n", 1) == "warning: w"
    assert headers.first_error("", 3) == "exit code 3"


clang = shutil.which("clang++")


@pytest.mark.skipif(clang is None, reason="no clang++ on PATH")
def test_real_clang_a_header_using_size_t_without_an_include_fails(tmp_path, capsys):
    write(tmp_path, "src/A/NoInclude.h", "#pragma once\nstruct S { size_t count; };\n")
    write(tmp_path, "src/A/WithInclude.h", "#pragma once\n#include <cstddef>\nstruct T { std::size_t count; };\n")
    build = make_build(tmp_path, [entry(tmp_path, "src/A/a.cpp", f"{clang} -std=c++20 -c a.cpp")])
    code, out = run(tmp_path, ["--build", str(build), "--jobs", "2"], headers.compile_header, capsys)
    assert code == 1
    assert "new: src/A/NoInclude.h" in out
    assert "WithInclude.h" not in out
    assert out.splitlines()[-1] == "checked 2, failing 1, new 1, fixed 0"
