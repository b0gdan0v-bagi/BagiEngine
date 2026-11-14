"""strip_pch_includes.py: only source files are processed, a header keeps its includes."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import strip_pch_includes as strip  # noqa: E402

PCH_INCLUDES = {"string"}
TEXT = "#include <string>\n#include <vector>\nint x;\n"


def write(root: Path, relative: str, text: str = TEXT) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def test_keep_sources_drops_headers_and_counts_them(tmp_path):
    files = [tmp_path / name for name in ("Foo.h", "Foo.cpp", "Bar.hpp", "Baz.cc", "Q.cxx", "R.c", "S.mm", "T.hxx")]
    kept, skipped = strip.keep_sources(files)
    assert [p.name for p in kept] == ["Foo.cpp", "Baz.cc", "Q.cxx", "R.c", "S.mm"]
    assert skipped == 3


def test_a_dry_run_reports_the_source_only(tmp_path, capsys, monkeypatch):
    write(tmp_path, "src/Foo.h")
    write(tmp_path, "src/Foo.cpp")
    pch = write(tmp_path, "src/Modules/BECore/pch.h", "#include <string>\n")
    ignore = write(tmp_path, ".clang-format-ignore", "build/\n")
    assert ignore.exists()
    monkeypatch.setattr(strip, "ENGINE_ROOT", tmp_path)
    monkeypatch.setattr(sys, "argv", ["strip", "--dry-run", "--all", "--root", str(tmp_path), "--dir", "src"])
    assert strip.main() == 0
    out = capsys.readouterr().out
    assert "Foo.cpp: removed 1 include(s)" in out
    assert "Foo.h:" not in out
    assert "header(s) skipped" in out
    assert pch.exists()


def test_a_real_run_changes_the_source_only(tmp_path, monkeypatch):
    header = write(tmp_path, "src/Foo.h")
    source = write(tmp_path, "src/Foo.cpp")
    write(tmp_path, "src/Modules/BECore/pch.h", "#include <string>\n")
    write(tmp_path, ".clang-format-ignore", "build/\n")
    monkeypatch.setattr(strip, "ENGINE_ROOT", tmp_path)
    monkeypatch.setattr(sys, "argv", ["strip", "--all", "--root", str(tmp_path), "--dir", "src"])
    assert strip.main() == 0
    assert source.read_text() == "#include <vector>\nint x;\n"
    assert header.read_text() == TEXT


def test_process_file_still_strips_a_source(tmp_path):
    source = write(tmp_path, "Foo.cpp")
    removed, wrote = strip.process_file(source, PCH_INCLUDES, dry_run=False)
    assert removed == ["#include <string>"]
    assert wrote
